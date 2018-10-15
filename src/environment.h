#pragma once

#include <array>
#include <vector>

#include "math.h"

namespace prism {

// Procedural sky, in physical units (cd/m^2 after multiplying by intensity).
struct SkyParams {
    float3 zenith{0.12f, 0.3f, 0.8f};
    float3 horizon{0.5f, 0.65f, 0.9f};
    float3 ground{0.25f, 0.22f, 0.2f};
    float intensity = 14000.0f;

    float3 sunDirection{0, 1, 0};  // points toward the sun
    float3 sunColor{1.0f, 0.95f, 0.85f};
    float sunGlow = 0.5f;           // halo around the sun, part of the IBL
    float sunDiskLuminance = 2e6f;  // only drawn in the background, never in the IBL
};

// Image based lighting baked from the sky, in the spirit of Filament's cmgen tool:
//  - diffuse irradiance is projected onto 3 bands of spherical harmonics
//  - specular radiance is prefiltered with GGX importance sampling at several roughness levels
class Environment {
public:
    explicit Environment(const SkyParams& params);

    float3 background(float3 dir) const;
    // Lambertian irradiance divided by pi: multiply by diffuse color to get outgoing radiance.
    float3 diffuse(float3 n) const;
    float3 specular(float3 r, float perceptualRoughness) const;

private:
    struct LatLong {
        int width = 0, height = 0;
        std::vector<float3> texels;
        float3 sample(float3 dir) const;
    };

    static constexpr int kSpecularLevels = 6;

    float3 radiance(float3 dir) const;
    void bakeSphericalHarmonics();
    LatLong bakePrefiltered(float perceptualRoughness, int width, int height, int samples) const;

    SkyParams params_;
    std::array<float3, 9> sh_{};
    std::vector<LatLong> prefiltered_;  // levels 1..kSpecularLevels-1; level 0 is the sky itself
};

} // namespace prism
