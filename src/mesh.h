#pragma once

#include <cstdint>
#include <vector>

#include "math.h"

namespace prism {

struct Vertex {
    float3 position;
    float3 normal;
};

// Indexed triangle list. Front faces wind counter-clockwise.
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float3 boundsMin;
    float3 boundsMax;
};

Mesh makeSphere(int segments, int rings);  // unit radius, centered at origin
Mesh makePlane(float halfSize);            // y = 0, facing +y

} // namespace prism
