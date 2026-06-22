#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <stdexcept>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ZMap  — 규칙적인 격자(grid)의 높이맵
//
//  좌표 변환:
//    실제 X (mm) = col * xResMm
//    실제 Y (mm) = row * yResMm
//    실제 Z (mm) = data[row * width + col] * zResMm
//
//  NaN → 유효하지 않은 픽셀
// ─────────────────────────────────────────────────────────────────────
struct ZMap {
    int   width   = 0;
    int   height  = 0;
    float xResMm  = 1.f;   // X 분해능 (mm/pixel)
    float yResMm  = 1.f;   // Y 분해능 (mm/pixel)
    float zResMm  = 1.f;   // Z 분해능 (mm/count) — raw → mm 변환 계수

    std::vector<float> data;   // [row * width + col], NaN = 무효

    // ── 접근자 ──────────────────────────────────────────────────────
    bool empty() const { return data.empty(); }

    float rawAt(int col, int row) const {
        return data[static_cast<size_t>(row) * width + col];
    }

    // mm 단위 Z 반환
    float zMm(int col, int row) const {
        return rawAt(col, row) * zResMm;
    }

    // mm 단위 X 반환
    float xMm(int col) const { return col * xResMm; }

    // mm 단위 Y 반환
    float yMm(int row) const { return row * yResMm; }

    bool valid(int col, int row) const {
        return !std::isnan(rawAt(col, row));
    }

    bool inBounds(int col, int row) const {
        return col >= 0 && col < width && row >= 0 && row < height;
    }

    // ── 생성 헬퍼 ───────────────────────────────────────────────────
    static ZMap makeFlat(int w, int h, float zValue,
                         float xRes = 1.f, float yRes = 1.f, float zRes = 1.f) {
        ZMap m;
        m.width  = w;
        m.height = h;
        m.xResMm = xRes;
        m.yResMm = yRes;
        m.zResMm = zRes;
        m.data.assign(static_cast<size_t>(w) * h, zValue);
        return m;
    }
};

using ZMapPtr = std::shared_ptr<ZMap>;

// ─────────────────────────────────────────────────────────────────────
//  Rect2D — 픽셀 단위 ROI
// ─────────────────────────────────────────────────────────────────────
struct Rect2D {
    int x = 0, y = 0;
    int w = 0, h = 0;

    bool valid()                   const { return w > 0 && h > 0; }
    int  right()                   const { return x + w; }
    int  bottom()                  const { return y + h; }
    bool contains(int cx, int cy)  const {
        return cx >= x && cx < right() && cy >= y && cy < bottom();
    }
    bool fitsIn(const ZMap& m) const {
        return x >= 0 && y >= 0 && right() <= m.width && bottom() <= m.height;
    }
};

} // namespace vision
