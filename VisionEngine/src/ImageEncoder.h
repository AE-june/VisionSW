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

    // gray 0 = NaN 전용 예약. 유효값은 [1,255]로 매핑 → 뷰어가 gray==0을
    // 무손실로 NaN과 구별(최소값 픽셀이 NaN처럼 오판되던 버그 방지).
    // 무손실 PNG 사용 필수: JPG 압축은 경계에서 gray값을 흔들어 0/1 구분을 깨뜨림.
    std::vector<uint8_t> gray(static_cast<size_t>(heightmap.width) * heightmap.height);
    for (int i = 0; i < heightmap.width * heightmap.height; ++i) {
        float v = heightmap.data[i];
        gray[i] = std::isnan(v) ? 0
                : static_cast<uint8_t>(1.f + (v - zMin) / range * 254.f);
    }

    std::vector<uint8_t> png;
    stbi_write_png_to_func(stbiCallback, &png, heightmap.width, heightmap.height, 1,
                           gray.data(), heightmap.width /*stride bytes*/);
    return base64Encode(png);
}

// ── Region → base64 grayscale (mask 1→255, 0→0) ─────────────────────────────
//  마스크 시각화용 프리뷰. 무손실 PNG — 이진 마스크 경계 fuzz 방지 + heightmap
//  프리뷰와 UI mime(png) 일관. gray 0 = 마스크 밖(=영역 비멤버) → hover null 정상.
inline std::string regionToBase64(const Region& rgn) {
    if (rgn.empty()) return {};
    std::vector<uint8_t> gray(rgn.mask.size());
    for (size_t i = 0; i < rgn.mask.size(); ++i) gray[i] = rgn.mask[i] ? 255 : 0;
    std::vector<uint8_t> png;
    stbi_write_png_to_func(stbiCallback, &png, rgn.width, rgn.height, 1, gray.data(), rgn.width);
    return base64Encode(png);
}

} // namespace vision
