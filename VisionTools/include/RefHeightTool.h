#pragma once
#include "IAlgorithmTool.h"
#include "ZMap.h"
#include <vector>
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RefHeightParams
//  ROI(들) 내 Z값을 풀링해 아웃라이어를 제거한 뒤 평균 높이를 구하고,
//  이를 수평 기준평면(a=0,b=0,c=avgHeightMm)으로 출력한다.
//  → HeightMeasure의 Plane 입력에 PlaneFit과 동일하게 꽂힌다
//    (signedDistance = z - avgHeightMm, PlaneFit의 기울어진 평면 대신 수평 기준높이).
// ─────────────────────────────────────────────────────────────────────
struct RefHeightParams {
    // ROI in percentage of ZMap dimensions (0.0 ~ 1.0)
    struct ROI {
        float xPct = 0.f, yPct = 0.f, wPct = 1.f, hPct = 1.f;
        bool valid() const { return wPct > 0.f && hPct > 0.f; }
    };

    std::vector<ROI> rois;   // 평균 계산 영역들 (>=1). 전부 풀링해서 점 하나의 세트로 합침.

    enum class OutlierMode {
        Sor,             // 전체 표본의 mean/stddev 기준 sigma 밖 제거 (공간 무관, global 통계)
        PercentileTrim   // z 정렬 후 상/하위 tail % 절삭
    } mode = OutlierMode::Sor;

    float sorSigma      = 2.0f;   // SOR: |z-mean| > sorSigma*stddev 면 제거
    float lowTailPct    = 5.f;    // PercentileTrim: 하위 절삭 비율 (%)
    float highTailPct   = 5.f;    // PercentileTrim: 상위 절삭 비율 (%)
};

// ─────────────────────────────────────────────────────────────────────
//  RefHeightResult
// ─────────────────────────────────────────────────────────────────────
struct RefHeightResult {
    double avgHeightMm  = 0;
    int    sampleCount  = 0;   // 아웃라이어 제거 후 평균에 쓰인 점 개수
    int    rejectedCount = 0;  // 제거된 점 개수
    bool   valid = false;
    std::string message;
};

// ─────────────────────────────────────────────────────────────────────
//  RefHeightTool
// ─────────────────────────────────────────────────────────────────────
class RefHeightTool : public IAlgorithmTool {
public:
    explicit RefHeightTool(RefHeightParams params = {});
    std::string name() const override { return "RefHeight"; }
    ToolResult  execute(VisionDataPtr input) override;

    const RefHeightResult& lastResult() const { return m_result; }

private:
    RefHeightParams m_params;
    RefHeightResult m_result;

    std::vector<float> extractZ(const ZMap& map, const RefHeightParams::ROI& roi,
                                 int offCol, int offRow) const;
};

} // namespace vision
