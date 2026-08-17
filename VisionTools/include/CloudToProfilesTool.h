#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// PointCloud3D → 행(Y bin)별 Profile[]. 한 컬럼에 여러 높이 포인트가 있는 클라우드를
// 행 단위 단면(Profile)으로 분해. 다운스트림 ProfileFeature가 행별 분석.
//   reduce=None : 행의 모든 점을 Profile 샘플로 보존(다중 Z 전부 유지). x 오름차순.
//   reduce=Max/Min/Mean : X-bin(xStep)으로 컬럼 축약 → 정규 1D 신호(빈 컬럼 z=NaN).
class CloudToProfilesTool : public IAlgorithmTool {
public:
    enum class Reduce { None, Max, Min, Mean };
    struct Params {
        double yStepMm   = 0.1;   // 행(Y bin) 크기
        Reduce reduce    = Reduce::None;
        double xStepMm   = 0.1;   // reduce!=None 일 때 컬럼(X bin) 크기
        int    minPoints = 1;     // 행 채택 최소 점수
    };

    explicit CloudToProfilesTool(Params p = {}) : m_p(p) {}
    std::string name() const override { return "CloudToProfiles"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    Params m_p;
};

} // namespace vision
