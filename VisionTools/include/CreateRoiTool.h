#pragma once
#include "IAlgorithmTool.h"
#include <vector>
#include <array>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  CreateRoiTool — (HeightMap) → Region
//  사각/원(내접타원)/폴리곤 ROI(들)을 래스터화해 이진 마스크(Region)를 생산.
//  입력 HeightMap은 마스크 크기(W×H)와 %→px 변환 기준. 여러 ROI는 합집합(OR).
//  (좌표계: HeightFromPlaneTool과 동일하게 originCol/Row 오프셋 적용.)
// ─────────────────────────────────────────────────────────────────────
struct CreateRoiParams {
    struct ROI {
        float xPct = 0.f, yPct = 0.f, wPct = 1.f, hPct = 1.f;
        float angleDeg = 0.f;                         // 중심 기준 회전(도, 시계방향). 사각/타원에 적용
        bool  isCircle = false;                       // true면 사각에 내접하는 타원
        std::vector<std::array<float, 2>> poly;       // 폴리곤 꼭짓점(pct). 있으면 폴리곤 우선
    };
    std::vector<ROI> rois;
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
