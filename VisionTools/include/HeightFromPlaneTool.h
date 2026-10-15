#pragma once
#include "IAlgorithmTool.h"
#include "HeightMap.h"
#include <vector>
#include <array>
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  HeightFromPlaneParams
//  입력으로 받은 평면(PlaneModel)을 기준으로, 여러 measure ROI에서
//  대표 Z를 추출하고 평면까지의 수직거리를 측정한다.
// ─────────────────────────────────────────────────────────────────────
struct HeightFromPlaneParams {
    // ROI in percentage of HeightMap dimensions (0.0 ~ 1.0)
    struct ROI {
        float xPct = 0.f, yPct = 0.f, wPct = 1.f, hPct = 1.f;
        bool  isCircle = false;   // true면 ROI 사각 영역에 내접하는 타원
        std::vector<std::array<float, 2>> poly;   // 폴리곤 꼭짓점(pct). 비어있지 않으면 폴리곤으로 판정
        bool valid() const { return wPct > 0.f && hPct > 0.f; }
    };

    std::vector<ROI> measureRois;   // 높이를 측정할 영역들 (>=1)
    std::vector<ROI> maskRois;      // 측정에서 제외할 영역들 (사각/원/폴리곤)

    enum class Aggregation {
        Mean,       // ROI 내 유효 Z 평균
        Max,        // ROI 내 최대 Z
        HighTail,   // 상위 highTailPct% Z의 평균
        Percentile  // material ratio highTailPct%에서의 높이 = 상위 highTailPct% 백분위 "실측 점 하나". 표준(ISO 25178). 값·위치 동일 픽셀 → 결과 위치의 zmap값과 정확히 일치.
    } aggregation = Aggregation::Mean;

    float highTailPct = 20.f;       // HighTail: 평균낼 상위 비율(%). Percentile: material ratio(상위 %) 지점.

    // 합부 판정 (선택)
    bool  useTolerance = false;
    float nominalMm    = 0.f;       // 기준 수직거리 (mm)
    float toleranceMm  = 0.05f;     // 허용 오차 (± mm)
};

// ─────────────────────────────────────────────────────────────────────
//  측정 결과 (ROI 1개당 1개)
// ─────────────────────────────────────────────────────────────────────
struct HeightMeasure {
    double cx = 0, cy = 0;   // 대표 위치 (mm)
    double z  = 0;           // 집계된 대표 Z (mm)
    double distance = 0;     // 평면까지의 부호 있는 수직거리 (mm)
    int    pointCount = 0;
    bool   pass = true;      // tolerance 통과 여부 (useTolerance=false면 항상 true)
};

struct HeightFromPlaneResult {
    std::vector<HeightMeasure> measures;
    bool        valid   = false;
    bool        allPass = true;
    std::string message;
};

// ─────────────────────────────────────────────────────────────────────
//  HeightFromPlaneTool
// ─────────────────────────────────────────────────────────────────────
class HeightFromPlaneTool : public IAlgorithmTool {
public:
    explicit HeightFromPlaneTool(HeightFromPlaneParams params = {});
    std::string name() const override { return "HeightMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    HeightFromPlaneParams m_params;

    using Pt3 = std::array<double, 3>;   // {x_mm, y_mm, z_mm}

    // 마스크 ROI를 픽셀 좌표로 미리 해석한 형태 (execute당 1회 계산 → 픽셀 루프에서 재사용)
    struct MaskPx {
        bool isPoly = false;
        int  x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // 사각 경계 (px)
        bool isCircle = false;
        double cx = 0, cy = 0, rx = 1, ry = 1; // 내접 타원 (px)
        std::vector<std::array<double, 2>> poly;   // 폴리곤 꼭짓점 (px)
    };
    std::vector<MaskPx> resolveMasks(const HeightMap& map, int offCol, int offRow) const;

    std::vector<Pt3> extractPoints(const HeightMap& map,
                                   const HeightFromPlaneParams::ROI& roi,
                                   int offCol, int offRow,
                                   const std::vector<MaskPx>& masks) const;

    // (col,row)가 마스크(제외) 영역 안이면 true — 측정에서 제외
    bool masked(const std::vector<MaskPx>& masks, int col, int row) const;

    // ROI 내 점들 → (대표 x, 대표 y, 대표 z). HighTail은 pts를 부분정렬하므로 non-const.
    Pt3 aggregate(std::vector<Pt3>& pts) const;
};

} // namespace vision
