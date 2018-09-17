#include "rasterizer.h"

#include <cfloat>
#include <utility>

#include "parallel.h"

namespace prism {
namespace {

constexpr int kTileSize = 32;

inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

} // namespace

RasterTarget::RasterTarget(int w, int h, bool withAttributes)
    : width(w), height(h), depth(static_cast<size_t>(w) * h, FLT_MAX) {
    if (withAttributes) {
        position.resize(depth.size());
        normal.resize(depth.size());
        material.assign(depth.size(), -1);
    }
}

void Rasterizer::submit(const Mesh& mesh, const mat4& model, const mat4& viewProj,
                        int32_t material, CullMode cull) {
    std::vector<ClipVertex> verts(mesh.vertices.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const Vertex& v = mesh.vertices[i];
        float3 world = model.transformPoint(v.position);
        verts[i] = {viewProj * float4(world, 1.0f), world, normalize(model.transformVector(v.normal))};
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const ClipVertex tri[3] = {verts[mesh.indices[i]], verts[mesh.indices[i + 1]],
                                   verts[mesh.indices[i + 2]]};
        clipAndEmit(tri, material, cull);
    }
}

// Sutherland-Hodgman against the near plane (z >= -w). One plane turns a triangle into at
// most a quad. The other planes are handled by the screen-space bounding box and depth range.
void Rasterizer::clipAndEmit(const ClipVertex (&in)[3], int32_t material, CullMode cull) {
    ClipVertex out[4];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipVertex& cur = in[i];
        const ClipVertex& nxt = in[(i + 1) % 3];
        float dc = cur.clip.z + cur.clip.w;
        float dn = nxt.clip.z + nxt.clip.w;
        if (dc >= 0.0f) out[n++] = cur;
        if ((dc >= 0.0f) != (dn >= 0.0f)) {
            float t = dc / (dc - dn);
            out[n++] = {cur.clip + (nxt.clip - cur.clip) * t, mix(cur.world, nxt.world, t),
                        normalize(mix(cur.normal, nxt.normal, t))};
        }
    }
    if (n >= 3) emit(out[0], out[1], out[2], material, cull);
    if (n == 4) emit(out[0], out[2], out[3], material, cull);
}

void Rasterizer::emit(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c,
                      int32_t material, CullMode cull) {
    Triangle t;
    const ClipVertex* v[3] = {&a, &b, &c};
    for (int k = 0; k < 3; ++k) {
        float invW = 1.0f / v[k]->clip.w;
        t.x[k] = (v[k]->clip.x * invW * 0.5f + 0.5f) * width_;
        t.y[k] = (0.5f - v[k]->clip.y * invW * 0.5f) * height_;
        t.z[k] = v[k]->clip.z * invW;
        t.invW[k] = invW;
        t.world[k] = v[k]->world * invW;
        t.normal[k] = v[k]->normal * invW;
    }

    float area = edge(t.x[0], t.y[0], t.x[1], t.y[1], t.x[2], t.y[2]);
    if (std::abs(area) < 1e-9f) return;
    // Screen space has y pointing down, so counter-clockwise front faces have negative area.
    if (cull == CullMode::Back && area > 0.0f) return;
    if (area < 0.0f) {
        std::swap(t.x[1], t.x[2]);
        std::swap(t.y[1], t.y[2]);
        std::swap(t.z[1], t.z[2]);
        std::swap(t.invW[1], t.invW[2]);
        std::swap(t.world[1], t.world[2]);
        std::swap(t.normal[1], t.normal[2]);
        area = -area;
    }
    t.invArea = 1.0f / area;
    t.material = material;

    t.minX = std::max(0, static_cast<int>(std::floor(std::min({t.x[0], t.x[1], t.x[2]}))));
    t.minY = std::max(0, static_cast<int>(std::floor(std::min({t.y[0], t.y[1], t.y[2]}))));
    t.maxX = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({t.x[0], t.x[1], t.x[2]}))));
    t.maxY = std::min(height_ - 1, static_cast<int>(std::ceil(std::max({t.y[0], t.y[1], t.y[2]}))));
    if (t.minX > t.maxX || t.minY > t.maxY) return;
    if (std::min({t.z[0], t.z[1], t.z[2]}) > 1.0f) return;

    triangles_.push_back(t);
}

void Rasterizer::resolve(RasterTarget& target) const {
    const int tilesX = (width_ + kTileSize - 1) / kTileSize;
    const int tilesY = (height_ + kTileSize - 1) / kTileSize;

    // Binning: each tile gets the list of triangles overlapping it, in submission order.
    std::vector<std::vector<uint32_t>> bins(static_cast<size_t>(tilesX) * tilesY);
    for (uint32_t i = 0; i < triangles_.size(); ++i) {
        const Triangle& t = triangles_[i];
        for (int ty = t.minY / kTileSize; ty <= t.maxY / kTileSize; ++ty)
            for (int tx = t.minX / kTileSize; tx <= t.maxX / kTileSize; ++tx)
                bins[static_cast<size_t>(ty) * tilesX + tx].push_back(i);
    }

    // Tiles are disjoint, so threads never write the same pixel.
    parallelFor(static_cast<int>(bins.size()), [&](int b) {
        int x0 = (b % tilesX) * kTileSize;
        int y0 = (b / tilesX) * kTileSize;
        int x1 = std::min(x0 + kTileSize, width_) - 1;
        int y1 = std::min(y0 + kTileSize, height_) - 1;
        for (uint32_t i : bins[b]) rasterize(triangles_[i], x0, y0, x1, y1, target);
    });
}

void Rasterizer::rasterize(const Triangle& t, int x0, int y0, int x1, int y1, RasterTarget& target) {
    x0 = std::max(x0, t.minX);
    y0 = std::max(y0, t.minY);
    x1 = std::min(x1, t.maxX);
    y1 = std::min(y1, t.maxY);
    const bool attributes = target.hasAttributes();

    for (int y = y0; y <= y1; ++y) {
        float py = y + 0.5f;
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f;
            float e0 = edge(t.x[1], t.y[1], t.x[2], t.y[2], px, py);
            float e1 = edge(t.x[2], t.y[2], t.x[0], t.y[0], px, py);
            float e2 = edge(t.x[0], t.y[0], t.x[1], t.y[1], px, py);
            if (e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) continue;

            float l0 = e0 * t.invArea, l1 = e1 * t.invArea, l2 = e2 * t.invArea;
            float z = l0 * t.z[0] + l1 * t.z[1] + l2 * t.z[2];  // z/w is affine in screen space
            if (z < -1.0f || z > 1.0f) continue;

            size_t idx = static_cast<size_t>(y) * target.width + x;
            if (z >= target.depth[idx]) continue;
            target.depth[idx] = z;

            if (attributes) {
                float w = 1.0f / (l0 * t.invW[0] + l1 * t.invW[1] + l2 * t.invW[2]);
                target.position[idx] = (t.world[0] * l0 + t.world[1] * l1 + t.world[2] * l2) * w;
                target.normal[idx] = normalize((t.normal[0] * l0 + t.normal[1] * l1 + t.normal[2] * l2) * w);
                target.material[idx] = t.material;
            }
        }
    }
}

} // namespace prism
