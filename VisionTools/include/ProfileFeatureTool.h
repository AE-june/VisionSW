#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct ProfileFeatureParams {
    // ── Phase 5 집계 계열 ──────────────────────────────────────────────
    std::string kind        = "maxZ"; // maxZ|minZ|maxS|minS|mean|median|stdDev|percentile|highTail
    double searchFromMm     = 0;      // 검색 구간 s 시작. 둘 다 0이면 전체.
    double searchToMm       = 0;      // 검색 구간 s 끝
    int    nth              = 0;      // 조건 만족 중 몇 번째(0-based). 음수=뒤에서
    double percentile       = 50;     // kind=percentile|highTail 에서 쓰는 값

    // ── Phase 6 검출 계열 (현재 미사용) ────────────────────────────────
    std::string edgeDir     = "any";  // rising|falling|any
    double edgeThresholdMm  = 0.05;
    int    smoothWindow     = 3;
};

class ProfileFeatureTool : public IAlgorithmTool {
public:
    explicit ProfileFeatureTool(ProfileFeatureParams params = {});
    std::string name() const override { return "ProfileFeature"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    ProfileFeatureParams m_params;
    // 단일 Profile 분석 → measurements/points 담은 ToolResult. execute가 label 프리픽스 붙여 병합.
    ToolResult analyzeOne(const Profile& prof) const;
};

} // namespace vision
