#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "mesh.h"

namespace prism {

// Filament-style "metallic/roughness" material.
struct Material {
    float3 baseColor{0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;    // perceptual roughness
    float reflectance = 0.5f;  // maps to f0 = 0.16 * reflectance^2 for dielectrics (4% at 0.5)
    float3 emissive{0.0f};     // luminance in cd/m^2
};

struct Renderable {
    const Mesh* mesh = nullptr;
    mat4 model;  // translation + uniform scale only (normals are not inverse-transposed)
    uint32_t material = 0;
    bool castShadows = true;
    // When > 0, the renderable also acts as an analytic sphere occluder for ambient light.
    float occluderRadius = 0.0f;
};

struct DirectionalLight {
    float3 direction;   // direction the light travels
    float3 color{1.0f};
    float illuminance;  // lux
    bool castShadows = true;
};

struct Scene {
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::vector<Material> materials;
    std::vector<Renderable> renderables;
    std::optional<DirectionalLight> sun;

    const Mesh* addMesh(Mesh mesh) {
        meshes.push_back(std::make_unique<Mesh>(std::move(mesh)));
        return meshes.back().get();
    }
    uint32_t addMaterial(const Material& m) {
        materials.push_back(m);
        return static_cast<uint32_t>(materials.size() - 1);
    }
};

} // namespace prism
