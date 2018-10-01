#pragma once

#include "math.h"

namespace prism {

// Pinhole camera with a physically based exposure model: aperture, shutter speed and
// ISO sensitivity determine how scene luminance (cd/m^2) maps to pixel values.
struct Camera {
    float3 position{0, 2, 6};
    float3 target{0, 0, 0};
    float3 up{0, 1, 0};
    float fovY = radians(45.0f);
    float near = 0.1f;
    float far = 100.0f;

    float aperture = 16.0f;              // f-stops
    float shutterSpeed = 1.0f / 125.0f;  // seconds
    float sensitivity = 100.0f;          // ISO

    mat4 view() const { return lookAt(position, target, up); }
    mat4 projection(float aspect) const { return perspective(fovY, aspect, near, far); }

    float ev100() const {
        return std::log2((aperture * aperture) / shutterSpeed * 100.0f / sensitivity);
    }

    // Scale applied to luminance so that a saturating sensor maps to 1.0.
    float exposure() const { return 1.0f / (1.2f * std::exp2(ev100())); }
};

} // namespace prism
