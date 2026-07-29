#pragma once

#include "VisionData.h"
#include "HeightMap.h"
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

// ── HeightMap → base64 PNG grayscale (normalized, 원본 해상도 유지) ─────────────

// outMin/outMax/outHasRange(옵션): 정규화하며 구한 실제 z범위를 반환 → 호출부 중복 스캔 방지
inline std::string heightmapToBase64(const HeightMap& heightmap,
                                float* outMin = nullptr, float* outMax = nullptr,
                                bool* outHasRange = nullptr) {
    if (outHasRange) *outHasRange = false;
    if (heightmap.empty()) return {};

    // Normalize float → uint8
    float zMin =  std::numeric_limits<float>::max();
    float zMax = -std::numeric_limits<float>::max();
    for (float v : heightmap.data) {
        if (!std::isnan(v)) { zMin = std::min(zMin, v); zMax = std::max(zMax, v); }
    }
    if (zMin <= zMax) {   // 유효 픽셀 존재
        if (outMin) *outMin = zMin;
        if (outMax) *outMax = zMax;
        if (outHasRange) *outHasRange = true;
    }
    float range = (zMax > zMin) ? (zMax - zMin) : 1.f;

    std::vector<uint8_t> gray(static_cast<size_t>(heightmap.width) * heightmap.height);
    for (int i = 0; i < heightmap.width * heightmap.height; ++i) {
        float v = heightmap.data[i];
        gray[i] = std::isnan(v) ? 0
                : static_cast<uint8_t>((v - zMin) / range * 255.f);
    }

    std::vector<uint8_t> jpg;
    stbi_write_jpg_to_func(stbiCallback, &jpg, heightmap.width, heightmap.height, 1,
                           gray.data(), 85);
    return base64Encode(jpg);
}

// ── Region → base64 grayscale (mask 1→255, 0→0) ─────────────────────────────
//  마스크 시각화용 프리뷰. 기존 파이프라인·UI mime(jpeg) 일관을 위해 JPG 사용.
//  (이진 마스크라 JPG 경계 fuzz는 시각화 한정 — 무손실 PNG는 후속 개선.)
inline std::string regionToBase64(const Region& rgn) {
    if (rgn.empty()) return {};
    std::vector<uint8_t> gray(rgn.mask.size());
    for (size_t i = 0; i < rgn.mask.size(); ++i) gray[i] = rgn.mask[i] ? 255 : 0;
    std::vector<uint8_t> jpg;
    stbi_write_jpg_to_func(stbiCallback, &jpg, rgn.width, rgn.height, 1, gray.data(), 90);
    return base64Encode(jpg);
}

} // namespace vision
