#pragma once

#include "VisionData.h"
#include "ZMap.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace vision {

// ── Base64 ────────────────────────────────────────────────────────────────

inline std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) v |= data[i + 2];
        out += chars[(v >> 18) & 0x3F];
        out += chars[(v >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? chars[(v >> 6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? chars[v & 0x3F] : '=';
    }
    return out;
}

// ── PNG write callback ────────────────────────────────────────────────────

inline void stbiCallback(void* ctx, void* data, int size) {
    auto* buf = static_cast<std::vector<uint8_t>*>(ctx);
    const auto* p = static_cast<uint8_t*>(data);
    buf->insert(buf->end(), p, p + size);
}

// ── Nearest-neighbor downscale ────────────────────────────────────────────

inline void downscale(const uint8_t* src, int sw, int sh, int ch,
                      std::vector<uint8_t>& dst, int dw, int dh) {
    dst.resize(static_cast<size_t>(dw) * dh * ch);
    for (int y = 0; y < dh; ++y) {
        int sy = y * sh / dh;
        for (int x = 0; x < dw; ++x) {
            int sx = x * sw / dw;
            for (int c = 0; c < ch; ++c)
                dst[(y * dw + x) * ch + c] = src[(sy * sw + sx) * ch + c];
        }
    }
}

// ── Image2D → base64 PNG (max 320px wide) ────────────────────────────────

inline std::string imageToBase64(const Image2D& img, int maxW = 320) {
    int dw = img.width, dh = img.height;
    if (dw > maxW) { dh = dh * maxW / dw; dw = maxW; }

    std::vector<uint8_t> pixels;
    if (dw == img.width && dh == img.height) {
        pixels = img.data;
    } else {
        downscale(img.data.data(), img.width, img.height, img.channels, pixels, dw, dh);
    }

    std::vector<uint8_t> png;
    stbi_write_png_to_func(stbiCallback, &png, dw, dh, img.channels,
                           pixels.data(), dw * img.channels);
    return base64Encode(png);
}

// ── ZMap → base64 PNG grayscale (normalized, max 320px wide) ─────────────

inline std::string zmapToBase64(const ZMap& zmap, int maxW = 320) {
    if (zmap.empty()) return {};

    // Normalize float → uint8
    float zMin =  std::numeric_limits<float>::max();
    float zMax = -std::numeric_limits<float>::max();
    for (float v : zmap.data) {
        if (!std::isnan(v)) { zMin = std::min(zMin, v); zMax = std::max(zMax, v); }
    }
    float range = (zMax > zMin) ? (zMax - zMin) : 1.f;

    std::vector<uint8_t> gray(static_cast<size_t>(zmap.width) * zmap.height);
    for (int i = 0; i < zmap.width * zmap.height; ++i) {
        float v = zmap.data[i];
        gray[i] = std::isnan(v) ? 0
                : static_cast<uint8_t>((v - zMin) / range * 255.f);
    }

    int dw = zmap.width, dh = zmap.height;
    std::vector<uint8_t> pixels;
    if (dw > maxW) {
        int ndh = dh * maxW / dw; int ndw = maxW;
        downscale(gray.data(), dw, dh, 1, pixels, ndw, ndh);
        dw = ndw; dh = ndh;
    } else {
        pixels = gray;
    }

    std::vector<uint8_t> png;
    stbi_write_png_to_func(stbiCallback, &png, dw, dh, 1, pixels.data(), dw);
    return base64Encode(png);
}

} // namespace vision
