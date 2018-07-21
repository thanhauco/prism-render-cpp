#include "mesh.h"

namespace prism {

Mesh makeSphere(int segments, int rings) {
    Mesh mesh;
    for (int r = 0; r <= rings; ++r) {
        float phi = kPi * static_cast<float>(r) / rings;
        for (int s = 0; s <= segments; ++s) {
            float theta = 2.0f * kPi * static_cast<float>(s) / segments;
            float3 p{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            mesh.vertices.push_back({p, p});
        }
    }
    const uint32_t stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            uint32_t a = r * stride + s;
            uint32_t b = a + stride;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b + 1, a, b + 1, b});
        }
    }
    mesh.boundsMin = float3(-1.0f);
    mesh.boundsMax = float3(1.0f);
    return mesh;
}

} // namespace prism
