#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math.h"

namespace prism {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;  // sRGB encoded, row-major, 3 bytes per pixel
};

// ACES filmic curve (Narkowicz fit), maps HDR exposed values to [0, 1] display-linear.
inline float3 tonemapACES(float3 x) {
    auto f = [](float v) { return saturate((v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f)); };
    return {f(x.x), f(x.y), f(x.z)};
}

inline float linearToSRGB(float c) {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Writes an 8-bit RGB PNG using uncompressed (stored) deflate blocks: no zlib dependency.
bool writePNG(const std::string& path, const Image& image);

} // namespace prism
