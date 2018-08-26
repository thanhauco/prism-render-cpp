#pragma once

#include "camera.h"
#include "image.h"
#include "scene.h"

namespace prism {

struct RenderSettings {
    int width = 1280;
    int height = 720;
    int ssaa = 2;  // supersampling factor per axis
};

struct RenderStats {
    size_t triangles = 0;
    double geometryMs = 0, shadingMs = 0, resolveMs = 0;
};

// Frame pipeline:
//   1. geometry pass  - rasterize the scene into a G-buffer
//   2. shading pass   - deferred PBR lighting and tone mapping per sample
//   3. resolve        - downsample supersamples and encode to sRGB
class Renderer {
public:
    explicit Renderer(RenderSettings settings) : settings_(settings) {}

    Image render(const Scene& scene, const Camera& camera);
    const RenderStats& stats() const { return stats_; }

private:
    RenderSettings settings_;
    RenderStats stats_;
};

} // namespace prism
