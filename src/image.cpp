#include "image.h"

#include <fstream>

namespace prism {

bool writePPM(const std::string& path, const Image& image) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "P6\n" << image.width << " " << image.height << "\n255\n";
    file.write(reinterpret_cast<const char*>(image.rgb.data()),
               static_cast<std::streamsize>(image.rgb.size()));
    return static_cast<bool>(file);
}

} // namespace prism
