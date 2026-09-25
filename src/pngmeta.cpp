#include "pngmeta.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

static const char* kKeyword = "fract3d-par";

static uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}

void pngFastSettings() {
    // stb tries all five PNG filters on every row by default. Always using "Sub"
    // with zlib level 5 writes an 8K image about 1.2 s faster and ~5% smaller.
    stbi_write_png_compression_level = 5;
    stbi_write_force_png_filter = 1;
}

bool writePngWithText(const std::string& path, int w, int h, const uint8_t* rgba, const std::string& text) {
    pngFastSettings();
    stbi_flip_vertically_on_write(1);
    int len = 0;
    unsigned char* png = stbi_write_png_to_mem(rgba, w * 4, w, h, 4, &len);
    if (!png) return false;
    // iTXt: keyword \0 compression-flag compression-method language \0 translated-keyword \0 text (UTF-8)
    std::vector<uint8_t> chunk;
    const char type[4] = {'i', 'T', 'X', 't'};
    std::vector<uint8_t> data(kKeyword, kKeyword + strlen(kKeyword));
    data.insert(data.end(), {0, 0, 0, 0, 0});
    data.insert(data.end(), text.begin(), text.end());
    put32(chunk, (uint32_t)data.size());
    chunk.insert(chunk.end(), type, type + 4);
    chunk.insert(chunk.end(), data.begin(), data.end());
    put32(chunk, crc32(chunk.data() + 4, data.size() + 4));
    // insert just before the final IEND chunk (always the last 12 bytes)
    std::ofstream out(path, std::ios::binary);
    bool ok = len > 12 && out.write((const char*)png, len - 12) && out.write((const char*)chunk.data(), chunk.size()) &&
              out.write((const char*)png + len - 12, 12);
    free(png);  // stb's default allocator
    return ok;
}

std::string readPngText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (f.size() < 8 || memcmp(f.data(), sig, 8) != 0) return "";
    size_t p = 8;
    while (p + 12 <= f.size()) {
        uint32_t n = (uint32_t)f[p] << 24 | (uint32_t)f[p + 1] << 16 | (uint32_t)f[p + 2] << 8 | f[p + 3];
        if (p + 12 + (size_t)n > f.size()) break;
        const char* type = (const char*)&f[p + 4];
        const uint8_t* d = &f[p + 8];
        bool isText = !memcmp(type, "tEXt", 4), isIText = !memcmp(type, "iTXt", 4);
        if ((isText || isIText) && n > strlen(kKeyword) && !memcmp(d, kKeyword, strlen(kKeyword)) && d[strlen(kKeyword)] == 0) {
            size_t q = strlen(kKeyword) + 1;
            if (isIText) {
                if (q + 2 > n || d[q] != 0) return "";  // compressed text isn't written by us
                q += 2;
                while (q < n && d[q]) q++;  // language tag
                q++;
                while (q < n && d[q]) q++;  // translated keyword
                q++;
            }
            return q <= n ? std::string((const char*)d + q, n - q) : "";
        }
        if (!memcmp(type, "IEND", 4)) break;
        p += 12 + n;
    }
    return "";
}
