#pragma once

#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────
//  NoiseFilter
//  HeightMap: Mean / Median / Gaussian smoothing + SOR(격자 통계 이상치 제거) — 모두 NaN 인지
//  2D 이미지: Gaussian blur (레거시)
//  3D 클라우드: 반경 이상치 제거 (레거시)
// ─────────────────────────────────────────────
class NoiseFilter : public IAlgorithmTool {
public:
    enum class Type { Mean, Median, Gaussian, SOR, Bilateral };

    struct Params {
        Type  type          = Type::Median;  // HeightMap 필터 종류
        int   kernelSizeX   = 3;             // X 커널 크기 (홀수, ≥3)
        int   kernelSizeY   = 3;             // Y 커널 크기 (홀수, ≥3)
        float stdRatio      = 2.0f;          // SOR: |z-mean| > stdRatio*std 이면 제거
        float sigmaRangeMm  = 0.02f;         // Bilateral: Z값 유사도 허용범위 (mm)
        // 포트 1에 Region을 연결하면 해당 영역만 필터. 없으면 전체 이미지에 적용.
        // (레거시 3D 클라우드 SOR용)
        float radius        = 1.0f;
        int   minNeighbors  = 5;
    };

    explicit NoiseFilter(Params params = {});

    std::string name() const override { return "NoiseFilter"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    Params m_params;

    ToolResult filterHeightMap(VisionDataPtr input);   // HeightMap 필터 (mean/median/gaussian/sor)
    ToolResult filter3D(VisionDataPtr input);     // 3D 클라우드 반경 이상치 (레거시)
};

} // namespace vision
