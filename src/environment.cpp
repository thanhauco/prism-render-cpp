#include "environment.h"

#include "parallel.h"

namespace prism {
namespace {

// Real spherical harmonics basis, bands 0..2.
std::array<float, 9> shBasis(float3 d) {
    return {
        0.282095f,
        0.488603f * d.y,
        0.488603f * d.z,
        0.488603f * d.x,
        1.092548f * d.x * d.y,
        1.092548f * d.y * d.z,
        0.315392f * (3.0f * d.z * d.z - 1.0f),
        1.092548f * d.x * d.z,
        0.546274f * (d.x * d.x - d.y * d.y),
    };
}

float3 dirFromLatLong(float u, float v) {
    float phi = (u - 0.5f) * 2.0f * kPi;
    float theta = v * kPi;
    return {std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi)};
}

float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

// Samples a GGX half vector around the +z axis of a local tangent frame.
float3 importanceSampleGGX(float u1, float u2, float roughness) {
    float a2 = roughness * roughness;
    float phi = 2.0f * kPi * u1;
    float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a2 - 1.0f) * u2));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
    return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
}

} // namespace

Environment::Environment(const SkyParams& params) : params_(params) {
    params_.sunDirection = normalize(params_.sunDirection);
    bakeSphericalHarmonics();
    for (int level = 1; level < kSpecularLevels; ++level) {
        float roughness = static_cast<float>(level) / (kSpecularLevels - 1);
        prefiltered_.push_back(bakePrefiltered(roughness, 128, 64, 256));
    }
}

float3 Environment::radiance(float3 d) const {
    const SkyParams& p = params_;
    float3 c = d.y >= 0.0f ? mix(p.horizon, p.zenith, std::pow(d.y, 0.5f))
                           : mix(p.horizon, p.ground, saturate(-d.y * 5.0f));
    float s = std::max(dot(d, p.sunDirection), 0.0f);
    c += p.sunColor * (p.sunGlow * std::pow(s, 8.0f));
    return c * p.intensity;
}

float3 Environment::background(float3 d) const {
    float cosAngle = dot(d, params_.sunDirection);
    float disk = smoothstep(std::cos(radians(0.9f)), std::cos(radians(0.7f)), cosAngle);
    return radiance(d) + params_.sunColor * (params_.sunDiskLuminance * disk);
}

void Environment::bakeSphericalHarmonics() {
    constexpr int kWidth = 256, kHeight = 128;
    const float dPhi = 2.0f * kPi / kWidth;
    const float dTheta = kPi / kHeight;
    for (int y = 0; y < kHeight; ++y) {
        float v = (y + 0.5f) / kHeight;
        float solidAngle = dPhi * dTheta * std::sin(v * kPi);
        for (int x = 0; x < kWidth; ++x) {
            float3 d = dirFromLatLong((x + 0.5f) / kWidth, v);
            float3 L = radiance(d) * solidAngle;
            auto basis = shBasis(d);
            for (int i = 0; i < 9; ++i) sh_[i] += L * basis[i];
        }
    }
    // Convolve with the clamped cosine lobe (Ramamoorthi & Hanrahan) and fold in the
    // Lambertian 1/pi so diffuse() returns radiance per unit albedo.
    const float band[3] = {kPi, 2.0f * kPi / 3.0f, kPi / 4.0f};
    for (int i = 0; i < 9; ++i) {
        int l = i == 0 ? 0 : (i < 4 ? 1 : 2);
        sh_[i] *= band[l] / kPi;
    }
}

float3 Environment::diffuse(float3 n) const {
    auto basis = shBasis(n);
    float3 e{0.0f};
    for (int i = 0; i < 9; ++i) e += sh_[i] * basis[i];
    return max(e, float3(0.0f));
}

Environment::LatLong Environment::bakePrefiltered(float perceptualRoughness, int width, int height,
                                                  int samples) const {
    LatLong map;
    map.width = width;
    map.height = height;
    map.texels.resize(static_cast<size_t>(width) * height);
    const float roughness = perceptualRoughness * perceptualRoughness;

    parallelFor(height, [&](int y) {
        for (int x = 0; x < width; ++x) {
            // Assume n = v = r, as in the split-sum approximation.
            float3 n = dirFromLatLong((x + 0.5f) / width, (y + 0.5f) / height);
            float3 up = std::abs(n.y) < 0.999f ? float3{0, 1, 0} : float3{1, 0, 0};
            float3 t = normalize(cross(up, n));
            float3 b = cross(n, t);

            float3 sum{0.0f};
            float weight = 0.0f;
            for (int i = 0; i < samples; ++i) {
                float3 hl = importanceSampleGGX(static_cast<float>(i) / samples,
                                                radicalInverse(static_cast<uint32_t>(i)), roughness);
                float3 h = t * hl.x + b * hl.y + n * hl.z;
                float3 l = h * (2.0f * dot(n, h)) - n;
                float NoL = dot(n, l);
                if (NoL > 0.0f) {
                    sum += radiance(l) * NoL;
                    weight += NoL;
                }
            }
            map.texels[static_cast<size_t>(y) * width + x] = weight > 0 ? sum / weight : float3(0.0f);
        }
    });
    return map;
}

float3 Environment::LatLong::sample(float3 d) const {
    float u = std::atan2(d.x, -d.z) / (2.0f * kPi) + 0.5f;
    float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
    float fx = u * width - 0.5f;
    float fy = v * height - 0.5f;
    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    float tx = fx - x0, ty = fy - y0;
    auto texel = [&](int x, int y) {
        x = ((x % width) + width) % width;
        y = std::clamp(y, 0, height - 1);
        return texels[static_cast<size_t>(y) * width + x];
    };
    float3 top = mix(texel(x0, y0), texel(x0 + 1, y0), tx);
    float3 bottom = mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx);
    return mix(top, bottom, ty);
}

float3 Environment::specular(float3 r, float perceptualRoughness) const {
    float lod = saturate(perceptualRoughness) * (kSpecularLevels - 1);
    int lo = std::min(static_cast<int>(lod), kSpecularLevels - 1);
    int hi = std::min(lo + 1, kSpecularLevels - 1);
    auto fetch = [&](int level) { return level == 0 ? radiance(r) : prefiltered_[level - 1].sample(r); };
    return mix(fetch(lo), fetch(hi), lod - lo);
}

} // namespace prism
