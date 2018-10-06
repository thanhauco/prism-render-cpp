#pragma once

#include "camera.h"
#include "image.h"
#include "scene.h"

namespace prism {

struct RenderSettings {
    int width = 1280;
    int height = 720;
    int ssaa = 2;  // supersampling factor per axis
    int shadowMapSize = 2048;
};

struct RenderStats {
    size_t triangles = 0;
    double shadowMs = 0, geometryMs = 0, shadingMs = 0, resolveMs = 0;
};

// Frame pipeline:
//   1. shadow pass    - depth-only render of casters from the sun
//   2. geometry pass  - rasterize the scene into a G-buffer
//   3. shading pass   - deferred PBR lighting + IBL, exposure and tone mapping per sample
//   4. resolve        - downsample supersamples and encode to sRGB
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
