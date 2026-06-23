#pragma once
#include "IAlgorithmTool.h"
#include "ZMap.h"
#include <vector>
#include <array>
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  PlaneFitParams
// ─────────────────────────────────────────────────────────────────────
struct PlaneFitParams {
    // ROI in percentage of ZMap dimensions (0.0 ~ 1.0)
    struct ROI {
        float xPct = 0.f, yPct = 0.f, wPct = 1.f, hPct = 1.f;
        bool valid() const { return wPct > 0.f && hPct > 0.f; }
    };

    std::vector<ROI> refRois;   // reference regions for plane fitting (>=1)

    enum class Algorithm {
        LeastSquares,   // 최소제곱법 (normal equations)
        RANSAC,         // Random Sample Consensus
        SVD             // PCA via Jacobi eigendecomposition
    } algorithm = Algorithm::LeastSquares;

    float ransacThresholdMm = 0.05f;
    int   ransacIterations  = 200;
    int   maxCloudPoints    = 200000;   // 3D 뷰 포인트클라우드 목표 개수
};

// ─────────────────────────────────────────────────────────────────────
//  PlaneFitResult
// ─────────────────────────────────────────────────────────────────────
struct PlaneFitResult {
    double a = 0, b = 0, c = 0;    // fitted plane: z = a*x + b*y + c  (mm)
    double rmse = 0;                // RMS of point-to-plane residuals (mm)
    double tiltDeg = 0;             // 평면 법선과 z축 사이 기울기 각도 (deg)
    int    refPointCount = 0;       // 평면 피팅에 사용된 전체 ref 포인트 수
    int    inlierCount = 0;         // (RANSAC) 인라이어 수, 그 외 = refPointCount
    bool   valid  = false;
    std::string message;
    // 3D 뷰용: 전체 ZMap을 격자 다운샘플한 포인트클라우드 (x_mm, y_mm, z_mm)
    std::vector<std::array<double, 3>> cloudPoints;
};

// ─────────────────────────────────────────────────────────────────────
//  PlaneFitTool
// ─────────────────────────────────────────────────────────────────────
class PlaneFitTool : public IAlgorithmTool {
public:
    explicit PlaneFitTool(PlaneFitParams params = {});
    std::string name() const override { return "PlaneFit"; }
    ToolResult  execute(VisionDataPtr input) override;

    const PlaneFitResult& lastResult() const { return m_result; }

private:
    PlaneFitParams m_params;
    PlaneFitResult m_result;

    using Pt3 = std::array<double, 3>;   // {x_mm, y_mm, z_mm}

    std::vector<Pt3> extractPoints(const ZMap& map,
                                   const PlaneFitParams::ROI& roi) const;

    struct Plane { double a = 0, b = 0, c = 0; bool valid = false; int inliers = 0; };
    Plane fitLS    (const std::vector<Pt3>& pts) const;
    Plane fitRANSAC(const std::vector<Pt3>& pts) const;
    Plane fitSVD   (const std::vector<Pt3>& pts) const;

    // Jacobi eigenvalue decomposition for 3×3 symmetric matrix
    // evals: ascending eigenvalues, evecs: columns are eigenvectors
    static void jacobi3(double A[3][3], double evals[3], double evecs[3][3]);
};

} // namespace vision
