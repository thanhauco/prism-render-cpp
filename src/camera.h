#pragma once

#include "math.h"

namespace prism {

// Pinhole camera aimed with a look-at target. Aspect ratio comes from the render target.
struct Camera {
    float3 position{0, 2, 6};
    float3 target{0, 0, 0};
    float3 up{0, 1, 0};
    float fovY = radians(45.0f);
    float near = 0.1f;
    float far = 100.0f;

    mat4 view() const { return lookAt(position, target, up); }
    mat4 projection(float aspect) const { return perspective(fovY, aspect, near, far); }
};

} // namespace prism
