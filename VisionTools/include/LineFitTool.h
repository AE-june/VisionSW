#pragma once
#include "IAlgorithmTool.h"
#include "HeightMap.h"

namespace vision {

// 라인 검출 대상 특징
enum class LineFeature {
    Ridge,   // 높이 최대 (능선)
    Valley,  // 높이 최소 (골)
    Edge,    // 임계 교차 (계단 엣지)
};

// 캘리퍼 스캔 방향 — 스캔축을 따라 각 라인에서 특징점 1개 검출
enum class LineScanDir { Lr, Rl, Tb, Bt };

// 직선 피팅 방식
enum class LineFitMethod {
    LeastSquares,  // PCA 총최소제곱 — 전체 점, 빠름, 이상점 약함
    Ransac,        // 무작위 표본 → 인라이어 최다 모델 → 인라이어 재피팅. 이상점 강함
};

struct LineFitParams {
    LineFeature   feature   = LineFeature::Ridge;
    LineScanDir   scanDir   = LineScanDir::Lr;
    float         threshold = 0.f;   // Edge 모드: raw 임계값
    bool          risingEdge = true; // Edge 모드: true=상승(<thr→≥thr), false=하강
    LineFitMethod fitMethod = LineFitMethod::LeastSquares;
    float         ransacTolMm = 0.5f; // RANSAC 인라이어 허용 수직거리 (mm)
    int           ransacIters = 100;  // RANSAC 반복수
};

// 높이맵에서 능선/골/엣지 점들을 검출해 XY 평면 직선으로 피팅.
// 출력: LineModel(cx,cy,angle) + RefPoint + Overlay(Lines) + 측정값.
class LineFitTool : public IAlgorithmTool {
public:
    explicit LineFitTool(LineFitParams params = {}) : m_params(params) {}
    std::string name() const override { return "LineFit"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    LineFitParams m_params;
};

} // namespace vision
