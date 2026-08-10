#pragma once
#include "IAlgorithmTool.h"
#include <vector>
#include <array>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  CreateRoiTool — (HeightMap) → Region[]
//  ROI 하나당 Region 하나씩 출력 (Aurora Sequence<Region> 개념).
//  출력 Region.label = ROI.id → RegionMeasure 측정값 이름으로 전달.
//  PlaneFit 등 단수 Region만 받는 도구는 regions[0]만 사용하거나 union.
// ─────────────────────────────────────────────────────────────────────
struct CreateRoiParams {
    struct ROI {
        std::string id;                               // ROI 식별자 → Region.label
        float xPct = 0.f, yPct = 0.f, wPct = 1.f, hPct = 1.f;
        float angleDeg = 0.f;
        bool  isCircle = false;
        std::vector<std::array<float, 2>> poly;
    };
    std::vector<ROI> rois;

    // ── 라인 밴드 모드 — 포트1에 Line(LineModel) 연결 시 활성 ──────────
    //   라인 좌/우로 회전 사각형 밴드 ROI 생성. 정적 rois 대신 사용.
    enum class BandSide { Left, Right, Both };
    enum class BandLen  { Line, Fixed };
    float    bandWidthMm  = 5.f;              // 각 밴드 폭(라인 수직 방향)
    float    bandOffsetMm = 3.f;              // 라인 중심~밴드 중심 거리(수직)
    BandSide bandSide     = BandSide::Both;
    BandLen  bandLenMode  = BandLen::Line;    // 길이: 라인 실제 / 고정
    float    bandLengthMm = 10.f;             // Fixed일 때
};

class CreateRoiTool : public IAlgorithmTool {
public:
    explicit CreateRoiTool(CreateRoiParams params = {});
    std::string name() const override { return "CreateROI"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    CreateRoiParams m_params;
};

} // namespace vision
