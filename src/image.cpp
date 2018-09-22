#include "image.h"

#include <array>
#include <fstream>

namespace prism {
namespace {

uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t adler32(const std::vector<uint8_t>& data) {
    uint32_t a = 1, b = 0;
    for (uint8_t byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.insert(out.end(), {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)});
}

void writeChunk(std::ofstream& file, const char* type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> chunk;
    putU32(chunk, static_cast<uint32_t>(payload.size()));
    chunk.insert(chunk.end(), type, type + 4);
    chunk.insert(chunk.end(), payload.begin(), payload.end());
    putU32(chunk, crc32(chunk.data() + 4, chunk.size() - 4));
    file.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
}

} // namespace

bool writePNG(const std::string& path, const Image& image) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    file.write(reinterpret_cast<const char*>(signature), sizeof(signature));

    std::vector<uint8_t> header;
    putU32(header, static_cast<uint32_t>(image.width));
    putU32(header, static_cast<uint32_t>(image.height));
    header.insert(header.end(), {8, 2, 0, 0, 0});  // 8-bit, truecolor, deflate, no filter, no interlace
    writeChunk(file, "IHDR", header);

    // Each scanline is prefixed with filter type 0 (none).
    const size_t rowBytes = static_cast<size_t>(image.width) * 3;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * image.height);
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        auto row = image.rgb.begin() + static_cast<std::ptrdiff_t>(y * rowBytes);
        raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(rowBytes));
    }

    std::vector<uint8_t> zlib = {0x78, 0x01};
    for (size_t offset = 0;;) {
        size_t len = std::min<size_t>(65535, raw.size() - offset);
        bool last = offset + len >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.insert(zlib.end(), {uint8_t(len), uint8_t(len >> 8), uint8_t(~len), uint8_t(~len >> 8)});
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + len));
        offset += len;
        if (last) break;
    }
    putU32(zlib, adler32(raw));
    writeChunk(file, "IDAT", zlib);
    writeChunk(file, "IEND", {});
    return static_cast<bool>(file);
}

} // namespace prism
