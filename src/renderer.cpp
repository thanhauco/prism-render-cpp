#include "renderer.h"

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

struct ShadingContext {
    const Scene& scene;
    float3 cameraPosition;
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
        color += surfaceShading(pp, n, v, l, NoV) * sun->color * sun->illuminance;
    }

    return color + m.emissive;
}

} // namespace

Image Renderer::render(const Scene& scene, const Camera& camera) {
    stats_ = {};
    const int ssaa = std::max(1, settings_.ssaa);
    const int W = settings_.width * ssaa;
    const int H = settings_.height * ssaa;
    const float aspect = static_cast<float>(settings_.width) / settings_.height;

    // 1. Geometry pass.
    auto start = Clock::now();
    RasterTarget gbuffer(W, H, true);
    Rasterizer raster(W, H);
    const mat4 viewProj = camera.projection(aspect) * camera.view();
    for (const Renderable& r : scene.renderables)
        raster.submit(*r.mesh, r.model, viewProj, static_cast<int32_t>(r.material), CullMode::Back);
    raster.resolve(gbuffer);
    stats_.triangles = raster.triangleCount();
    stats_.geometryMs = elapsedMs(start);

    // 2. Shading pass: exposure + ACES per sample, stored display-linear.
    start = Clock::now();
    ShadingContext ctx{scene, camera.position};

    const float exposure = camera.exposure();
    std::vector<float3> samples(static_cast<size_t>(W) * H);
    parallelFor(H, [&](int y) {
        for (int x = 0; x < W; ++x) {
            size_t i = static_cast<size_t>(y) * W + x;
            float3 luminance{0.0f};
            int32_t id = gbuffer.material[i];
            if (id >= 0)
                luminance = shade(ctx, scene.materials[id], gbuffer.position[i], gbuffer.normal[i]);
            samples[i] = tonemapACES(luminance * exposure);
        }
    });
    stats_.shadingMs = elapsedMs(start);

    // 3. Resolve: box-filter supersamples, then encode to sRGB.
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
