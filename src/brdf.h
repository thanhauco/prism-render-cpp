#pragma once

// Cook-Torrance microfacet BRDF, following the formulation in Filament's
// "Physically Based Rendering in Filament" document:
//   specular = D(h) * V(v, l) * F(v, h)
//   diffuse  = Lambert
#include "math.h"

namespace prism {

// Perceptual roughness below this aliases badly and underflows in fp16/fp32 math.
constexpr float kMinPerceptualRoughness = 0.045f;

// Normal distribution function: GGX / Trowbridge-Reitz.
inline float D_GGX(float NoH, float roughness) {
    float a2 = roughness * roughness;
    float f = (NoH * a2 - NoH) * NoH + 1.0f;
    return a2 / (kPi * f * f);
}

// Height-correlated Smith-GGX visibility term (G / (4 NoV NoL) folded in).
inline float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
    float a2 = roughness * roughness;
    float ggxL = NoV * std::sqrt((-NoL * a2 + NoL) * NoL + a2);
    float ggxV = NoL * std::sqrt((-NoV * a2 + NoV) * NoV + a2);
    return 0.5f / (ggxV + ggxL);
}

inline float3 F_Schlick(float3 f0, float VoH) {
    float f = std::pow(1.0f - VoH, 5.0f);
    return float3(f) + f0 * (1.0f - f);
}

inline float Fd_Lambert() { return 1.0f / kPi; }

// Per-pixel material inputs derived from a Material, as in Filament's PixelParams.
struct PixelParams {
    float3 diffuseColor;
    float3 f0;
    float perceptualRoughness;
    float roughness; // perceptualRoughness^2
};

inline PixelParams makePixelParams(float3 baseColor, float metallic, float perceptualRoughness,
                                   float reflectance) {
    PixelParams p;
    p.diffuseColor = baseColor * (1.0f - metallic);
    float dielectricF0 = 0.16f * reflectance * reflectance * (1.0f - metallic);
    p.f0 = float3(dielectricF0) + baseColor * metallic;
    p.perceptualRoughness = std::clamp(perceptualRoughness, kMinPerceptualRoughness, 1.0f);
    p.roughness = p.perceptualRoughness * p.perceptualRoughness;
    return p;
}

// Returns BRDF * NoL for a single punctual light direction.
inline float3 surfaceShading(const PixelParams& p, float3 n, float3 v, float3 l, float NoV) {
    float NoL = saturate(dot(n, l));
    if (NoL <= 0.0f) return float3(0.0f);
    float3 h = normalize(v + l);
    float NoH = saturate(dot(n, h));
    float LoH = saturate(dot(l, h));

    float D = D_GGX(NoH, p.roughness);
    float V = V_SmithGGXCorrelated(NoV, NoL, p.roughness);
    float3 F = F_Schlick(p.f0, LoH);

    float3 Fr = F * (D * V);
    float3 Fd = p.diffuseColor * Fd_Lambert();
    return (Fd + Fr) * NoL;
}

} // namespace prism
