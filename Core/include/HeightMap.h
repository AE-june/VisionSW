#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <stdexcept>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  HeightMap  — 규칙적인 격자(grid)의 N채널 정규 이미지 (통합 iconic 타입)
//
//  본래 단일채널 높이맵이었으나, HALCON Image 개념에 맞춰 N개의 "역할(role)
//  채널"을 담는 통합 타입으로 확장됨. 각 채널은 같은 W×H 격자에 정렬된다.
//    - 채널0 = height (기본). 기존 단일채널 HeightMap과 완전히 동일하게 동작.
//    - 예: {"height","intensity","thickness"} — SDC의 Z/I/L 세트가 한 이미지의 3채널.
//  별칭 `Image`로도 참조 가능(동일 실체). HeightMap은 "높이 채널 관점의 이름"으로 유지.
//
//  데이터 배치(planar): 채널 c는 data[c*width*height ...], 각 채널 내부는
//    [row*width + col]. 채널0 접근은 확장 전과 바이트 단위로 동일.
//
//  좌표 변환:
//    실제 X (mm) = (col - originCol) * xResMm
//    실제 Y (mm) = (row - originRow) * yResMm
//    실제 Z (mm) = (rawAt(col,row) - zZeroCount) * zResMm  (16bit 중간값=높이0)
//
//  originCol/originRow: 좌표계 원점(px). 기본 0 = 좌상단.
//    Align 노드가 검출 기준점으로 설정하면 그 점이 (0,0)mm가 되고
//    이 HeightMap을 입력받는 하류 툴이 변환된 좌표계를 그대로 사용한다.
//
//  NaN → 유효하지 않은 픽셀
// ─────────────────────────────────────────────────────────────────────
struct HeightMap {
    int   width   = 0;
    int   height  = 0;
    int   channels = 1;    // 채널 수. 채널0=height 기본. planar 배치.
    float xResMm  = 1.f;   // X 분해능 (mm/pixel)
    float yResMm  = 1.f;   // Y 분해능 (mm/pixel)
    float zResMm  = 1.f;   // Z 분해능 (mm/count) — raw → mm 변환 계수
    float zZeroCount = 0.f;// 높이 0에 해당하는 raw count (16bit 센서: 32768). zMm에서 차감.
    float originCol = 0.f; // 좌표계 원점 X (px) — Align이 설정
    float originRow = 0.f; // 좌표계 원점 Y (px) — Align이 설정
    std::string frameId;   // 이 HeightMap이 속한 좌표계 프레임 id. "" = 미지정(검사 생략)

    // 채널별 역할 라벨(선택). 비어있으면 채널0="height"로 간주.
    // 예: {"height"}, {"height","intensity","thickness"}
    std::vector<std::string> channelRoles;

    std::vector<float> data;   // planar: [ch*width*height + row*width + col], NaN = 무효

    // ── 접근자 ──────────────────────────────────────────────────────
    bool empty() const { return data.empty(); }

    // 한 채널의 픽셀 수(= width*height).
    size_t channelStride() const {
        return static_cast<size_t>(width) * static_cast<size_t>(height);
    }

    // raw 값 (기본 채널0=height). 기존 2-인자 호출과 완전 호환.
    float rawAt(int col, int row, int ch = 0) const {
        return data[static_cast<size_t>(ch) * channelStride()
                    + static_cast<size_t>(row) * width + col];
    }

    // mm 단위 Z 반환 (중간값 기준: raw count zZeroCount가 높이 0). 채널0(height) 기준.
    float zMm(int col, int row) const {
        return (rawAt(col, row) - zZeroCount) * zResMm;
    }

    // mm 단위 X 반환 (원점 기준 상대 좌표)
    float xMm(int col) const { return (col - originCol) * xResMm; }

    // mm 단위 Y 반환 (원점 기준 상대 좌표)
    float yMm(int row) const { return (row - originRow) * yResMm; }

    bool valid(int col, int row) const {
        return !std::isnan(rawAt(col, row));
    }

    bool inBounds(int col, int row) const {
        return col >= 0 && col < width && row >= 0 && row < height;
    }

    // 역할 이름 → 채널 인덱스. 없으면 -1. (channelRoles 비었으면 "height"만 채널0.)
    int channelByRole(const std::string& role) const {
        if (channelRoles.empty()) return role == "height" ? 0 : -1;
        for (int i = 0; i < static_cast<int>(channelRoles.size()); ++i)
            if (channelRoles[i] == role) return i;
        return -1;
    }

    // ── 생성 헬퍼 ───────────────────────────────────────────────────
    static HeightMap makeFlat(int w, int h, float zValue,
                         float xRes = 1.f, float yRes = 1.f, float zRes = 1.f) {
        HeightMap m;
        m.width  = w;
        m.height = h;
        m.channels = 1;
        m.xResMm = xRes;
        m.yResMm = yRes;
        m.zResMm = zRes;
        m.data.assign(static_cast<size_t>(w) * h, zValue);
        return m;
    }
};

using HeightMapPtr = std::shared_ptr<HeightMap>;

// ─────────────────────────────────────────────────────────────────────
//  Image — 통합 N채널 iconic 타입의 정규 이름 (HeightMap과 동일 실체).
//  높이 채널 관점에선 HeightMap, 일반 다채널 이미지 관점에선 Image로 참조.
//  (B1: 통합 N채널 Image. 향후 Image2D 흡수·툴 단계적 이행의 토대.)
// ─────────────────────────────────────────────────────────────────────
using Image    = HeightMap;
using ImagePtr = HeightMapPtr;

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
    bool fitsIn(const HeightMap& m) const {
        return x >= 0 && y >= 0 && right() <= m.width && bottom() <= m.height;
    }
};

} // namespace vision
