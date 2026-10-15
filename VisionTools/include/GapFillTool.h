#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// GapFill: HeightMap의 결측(NaN) 픽셀을 보간해 메움.
//   가장 가까운 유효 픽셀까지 거리 ≤ maxGap 인 결측만 채우고, 큰 구멍은 NaN 유지
//   (검사에서 가짜 표면을 지어내지 않기 위함).
//   method: neighbor(반복 이웃)/median/laplace(PDE)/nearest(최근접)/idw(역거리)/linear(행·열 선형)/anisotropic
//   출력 단계(stages): 1.메운 결과 / 2.원본 / 3.메운 영역(마스크)
class GapFillTool : public IAlgorithmTool {
public:
    enum class Method { Neighbor, Median, Laplace, Nearest, Idw, Linear, Anisotropic };

    GapFillTool(Method m, int maxGap, int minValid, int idwRadius,
                float idwPower, float edgeSigma, bool noPreview)
        : m_method(m), m_maxGap(maxGap < 1 ? 1 : maxGap), m_minValid(minValid < 1 ? 1 : minValid),
          m_idwRadius(idwRadius < 1 ? 1 : idwRadius), m_idwPower(idwPower),
          m_edgeSigma(edgeSigma), m_noPreview(noPreview) {}

    std::string name() const override { return "GapFill"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    Method m_method;
    int    m_maxGap, m_minValid, m_idwRadius;
    float  m_idwPower, m_edgeSigma;
    bool   m_noPreview;
};

} // namespace vision
