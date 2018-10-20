#include "renderer.h"

#include <cfloat>
#include <chrono>

#include "brdf.h"
#include "parallel.h"
#include "rasterizer.h"

namespace prism {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct ShadowMap {
    mat4 viewProj;
    RasterTarget depth{1, 1, false};
    float texelWorld = 0;    // world-space size of one texel
    float depthPerMeter = 0; // NDC depth units per world meter

    // Fraction of the light that reaches p: 5x5 percentage-closer filtering with
    // normal-offset and constant depth bias to avoid shadow acne.
    float visibility(float3 p, float3 n) const {
        float3 offset = p + n * (texelWorld * 1.5f);
        float4 clip = viewProj * float4(offset, 1.0f);
        float sx = (clip.x * 0.5f + 0.5f) * depth.width;
        float sy = (0.5f - clip.y * 0.5f) * depth.height;
        float z = clip.z - 2.0f * texelWorld * depthPerMeter;
        int cx = static_cast<int>(std::floor(sx));
        int cy = static_cast<int>(std::floor(sy));

        float lit = 0.0f;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= depth.width || y >= depth.height) {
                    lit += 1.0f;
                    continue;
                }
                lit += z <= depth.depth[static_cast<size_t>(y) * depth.width + x] ? 1.0f : 0.0f;
            }
        }
        return lit / 25.0f;
    }
};

// Fits an orthographic light frustum around the bounding sphere of all shadow casters.
ShadowMap renderShadowMap(const Scene& scene, const DirectionalLight& light, int size) {
    float3 lo(FLT_MAX), hi(-FLT_MAX);
    for (const Renderable& r : scene.renderables) {
        if (!r.castShadows) continue;
        for (int i = 0; i < 8; ++i) {
            float3 corner{(i & 1) ? r.mesh->boundsMax.x : r.mesh->boundsMin.x,
                          (i & 2) ? r.mesh->boundsMax.y : r.mesh->boundsMin.y,
                          (i & 4) ? r.mesh->boundsMax.z : r.mesh->boundsMin.z};
            float3 w = r.model.transformPoint(corner);
            lo = min(lo, w);
            hi = max(hi, w);
        }
    }

    ShadowMap shadow;
    if (lo.x > hi.x) return shadow;  // no casters

    float3 center = (lo + hi) * 0.5f;
    float radius = length(hi - lo) * 0.5f;
    float3 toLight = -normalize(light.direction);
    float3 up = std::abs(toLight.y) > 0.99f ? float3{1, 0, 0} : float3{0, 1, 0};
    mat4 view = lookAt(center + toLight * (2.0f * radius), center, up);
    float far = 4.0f * radius;
    mat4 proj = orthographic(-radius, radius, -radius, radius, 0.0f, far);

    shadow.viewProj = proj * view;
    shadow.depth = RasterTarget(size, size, false);
    shadow.texelWorld = 2.0f * radius / size;
    shadow.depthPerMeter = 2.0f / far;

    Rasterizer raster(size, size);
    for (const Renderable& r : scene.renderables)
        if (r.castShadows) raster.submit(*r.mesh, r.model, shadow.viewProj, 0, CullMode::Back);
    raster.resolve(shadow.depth);
    return shadow;
}

struct ShadingContext {
    const Scene& scene;
    const Environment& env;
    float3 cameraPosition;
    const ShadowMap* shadow;
};

float3 shade(const ShadingContext& ctx, const Material& m, float3 p, float3 n) {
    float3 baseColor = m.baseColor;
    if (m.checkerSize > 0.0f) {
        int cx = static_cast<int>(std::floor(p.x / m.checkerSize));
        int cz = static_cast<int>(std::floor(p.z / m.checkerSize));
        if ((cx + cz) & 1) baseColor = m.checkerColor;
    }

    const PixelParams pp = makePixelParams(baseColor, m.metallic, m.roughness, m.reflectance);
    const float3 v = normalize(ctx.cameraPosition - p);
    if (dot(n, v) < 0.0f) n = -n;
    const float NoV = std::max(dot(n, v), 1e-4f);

    float3 color{0.0f};

    if (const auto& sun = ctx.scene.sun) {
        float3 l = -normalize(sun->direction);
        float visibility = ctx.shadow ? ctx.shadow->visibility(p, n) : 1.0f;
        if (visibility > 0.0f)
            color += surfaceShading(pp, n, v, l, NoV) * sun->color * (sun->illuminance * visibility);
    }

    // Image based lighting (split-sum approximation).
    float3 r = reflect(-v, n);
    float3 diffuseIBL = pp.diffuseColor * ctx.env.diffuse(n);
    float3 specularIBL = ctx.env.specular(r, pp.perceptualRoughness) *
                         EnvBRDFApprox(pp.f0, pp.perceptualRoughness, NoV);
    color += diffuseIBL + specularIBL;

    return color + m.emissive;
}

} // namespace

Image Renderer::render(const Scene& scene, const Camera& camera) {
    stats_ = {};
    const int ssaa = std::max(1, settings_.ssaa);
    const int W = settings_.width * ssaa;
    const int H = settings_.height * ssaa;
    const float aspect = static_cast<float>(settings_.width) / settings_.height;

    // 1. Shadow pass.
    auto start = Clock::now();
    ShadowMap shadow;
    const bool hasShadow = scene.sun && scene.sun->castShadows;
    if (hasShadow) shadow = renderShadowMap(scene, *scene.sun, settings_.shadowMapSize);
    stats_.shadowMs = elapsedMs(start);

    // 2. Geometry pass.
    start = Clock::now();
    RasterTarget gbuffer(W, H, true);
    Rasterizer raster(W, H);
    const mat4 viewProj = camera.projection(aspect) * camera.view();
    for (const Renderable& r : scene.renderables)
        raster.submit(*r.mesh, r.model, viewProj, static_cast<int32_t>(r.material), CullMode::Back);
    raster.resolve(gbuffer);
    stats_.triangles = raster.triangleCount();
    stats_.geometryMs = elapsedMs(start);

    // 3. Shading pass: exposure + ACES per sample, stored display-linear.
    start = Clock::now();
    ShadingContext ctx{scene, *scene.environment, camera.position, hasShadow ? &shadow : nullptr};

    const float exposure = camera.exposure();
    const float3 forward = normalize(camera.target - camera.position);
    const float3 right = normalize(cross(forward, camera.up));
    const float3 up = cross(right, forward);
    const float tanHalfFov = std::tan(camera.fovY * 0.5f);

    std::vector<float3> samples(static_cast<size_t>(W) * H);
    parallelFor(H, [&](int y) {
        for (int x = 0; x < W; ++x) {
            size_t i = static_cast<size_t>(y) * W + x;
            float3 luminance;
            int32_t id = gbuffer.material[i];
            if (id < 0) {
                float ndcX = 2.0f * (x + 0.5f) / W - 1.0f;
                float ndcY = 1.0f - 2.0f * (y + 0.5f) / H;
                float3 dir = normalize(forward + right * (ndcX * tanHalfFov * aspect) + up * (ndcY * tanHalfFov));
                luminance = ctx.env.background(dir);
            } else {
                luminance = shade(ctx, scene.materials[id], gbuffer.position[i], gbuffer.normal[i]);
            }
            samples[i] = tonemapACES(luminance * exposure);
        }
    });
    stats_.shadingMs = elapsedMs(start);

    // 4. Resolve: box-filter supersamples, then encode to sRGB.
    start = Clock::now();
    Image image;
    image.width = settings_.width;
    image.height = settings_.height;
    image.rgb.resize(static_cast<size_t>(image.width) * image.height * 3);
    const float norm = 1.0f / (ssaa * ssaa);
    parallelFor(image.height, [&](int y) {
        for (int x = 0; x < image.width; ++x) {
            float3 sum{0.0f};
            for (int sy = 0; sy < ssaa; ++sy)
                for (int sx = 0; sx < ssaa; ++sx)
                    sum += samples[static_cast<size_t>(y * ssaa + sy) * W + (x * ssaa + sx)];
            sum *= norm;
            uint8_t* out = &image.rgb[(static_cast<size_t>(y) * image.width + x) * 3];
            out[0] = static_cast<uint8_t>(std::lround(linearToSRGB(saturate(sum.x)) * 255.0f));
            out[1] = static_cast<uint8_t>(std::lround(linearToSRGB(saturate(sum.y)) * 255.0f));
            out[2] = static_cast<uint8_t>(std::lround(linearToSRGB(saturate(sum.z)) * 255.0f));
        }
    });
    stats_.resolveMs = elapsedMs(start);
    return image;
}

} // namespace prism
