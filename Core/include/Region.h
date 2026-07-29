#pragma once

#include "HeightMap.h"   // Rect2D
#include <vector>
#include <memory>
#include <cstdint>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  Region — 임의 픽셀 집합/마스크 (1급 iconic 타입, HALCON Region 개념)
//
//  같은 W×H 격자 위에서 "관심 픽셀 집합"을 표현한다. Image(HeightMap)와
//  격자가 정렬되므로 "이 Region 안에서만 측정" 워크플로우가 자명해진다.
//    - ROI(사각/원/폴리곤) → Region 생산
//    - Threshold(Image→Region), ConnectedComponents(Region→Region[])
//    - RegionMeasure(면적/무게중심/BBox → control)
//
//  표현: 이진 마스크(1=내부, 0=외부), 크기 width*height, [row*width+col].
//  (향후 대용량 최적화 시 run-length 표현으로 교체 가능 — 접근자는 유지.)
// ─────────────────────────────────────────────────────────────────────
struct Region {
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> mask;   // [row*width + col], 1=내부
    std::string frameId;         // 이 Region이 속한 HeightMap의 프레임 id. "" = 미지정

    bool empty() const { return mask.empty(); }

    bool inBounds(int col, int row) const {
        return col >= 0 && col < width && row >= 0 && row < height;
    }

    // (col,row)가 Region 내부인가.
    bool contains(int col, int row) const {
        return inBounds(col, row)
            && mask[static_cast<size_t>(row) * width + col] != 0;
    }

    // 내부 픽셀 수.
    size_t area() const {
        size_t n = 0;
        for (uint8_t v : mask) n += (v != 0);
        return n;
    }

    // 내부 픽셀을 감싸는 최소 사각(bounding box). 비어있으면 무효 Rect2D.
    Rect2D boundingBox() const {
        int minC = width, minR = height, maxC = -1, maxR = -1;
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c)
                if (mask[static_cast<size_t>(r) * width + c]) {
                    if (c < minC) minC = c;
                    if (c > maxC) maxC = c;
                    if (r < minR) minR = r;
                    if (r > maxR) maxR = r;
                }
        if (maxC < 0) return Rect2D{};
        return Rect2D{ minC, minR, maxC - minC + 1, maxR - minR + 1 };
    }

    // 빈(전부 0) Region 생성 헬퍼.
    static Region makeEmpty(int w, int h) {
        Region rg;
        rg.width = w;
        rg.height = h;
        rg.mask.assign(static_cast<size_t>(w) * h, 0);
        return rg;
    }
};

using RegionPtr = std::shared_ptr<Region>;

} // namespace vision
