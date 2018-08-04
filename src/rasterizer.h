#pragma once

#include <cstdint>
#include <vector>

#include "math.h"
#include "mesh.h"

namespace prism {

enum class CullMode { None, Back };

// Depth buffer plus an optional G-buffer (world position, normal, material id).
struct RasterTarget {
    RasterTarget(int width, int height, bool withAttributes);

    int width, height;
    std::vector<float> depth;  // NDC z; cleared to +inf
    std::vector<float3> position;
    std::vector<float3> normal;
    std::vector<int32_t> material;  // -1 = nothing rendered

    bool hasAttributes() const { return !material.empty(); }
};

// Rasterizes indexed triangle meshes into a RasterTarget, interpolating attributes with
// perspective correction.
class Rasterizer {
public:
    Rasterizer(int width, int height) : width_(width), height_(height) {}

    void submit(const Mesh& mesh, const mat4& model, const mat4& viewProj, int32_t material,
                CullMode cull);
    void resolve(RasterTarget& target) const;
    size_t triangleCount() const { return triangles_.size(); }

private:
    struct ClipVertex {
        float4 clip;
        float3 world;
        float3 normal;
    };

    struct Triangle {
        float x[3], y[3], z[3], invW[3];
        float3 world[3];   // pre-divided by w
        float3 normal[3];  // pre-divided by w
        float invArea;
        int minX, minY, maxX, maxY;
        int32_t material;
    };

    void emit(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, int32_t material,
              CullMode cull);
    static void rasterize(const Triangle& t, RasterTarget& target);

    int width_, height_;
    std::vector<Triangle> triangles_;
};

} // namespace prism
