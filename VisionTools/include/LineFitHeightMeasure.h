#pragma once

#include "IAlgorithmTool.h"
#include "ZMap.h"
#include <string>
#include <vector>
#include <tuple>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  Z 집계 방식 (Fit ROI: 각 X 컬럼에서 Y축 방향으로 여러 Z 중 하나 선택)
// ─────────────────────────────────────────────────────────────────────
enum class ZAggregation {
    Max,       // 최대값
    Mean,      // 평균
    HighTail   // 상위 N% 평균
};

// ─────────────────────────────────────────────────────────────────────
//  기준면 모드
//    Line  — Fit ROI에서 (X, Z) 점 추출 → XZ 평면 직선 피팅
//    Plane — Fit ROI에서 (X, Y, Z) 점 추출 → XYZ 공간 평면 피팅
// ─────────────────────────────────────────────────────────────────────
enum class ReferenceMode {
    Line,   // z = slope*x + intercept
    Plane   // z = a*x + b*y + c
};

// ─────────────────────────────────────────────────────────────────────
//  LineFitHeightMeasure  파라미터
// ─────────────────────────────────────────────────────────────────────
struct LineFitParams {
    // 1. 기준면 피팅용 ROI (픽셀 단위, 2개)
    Rect2D roiFit1;
    Rect2D roiFit2;

    // 2. 측정 대상 ROI
    Rect2D roiMeasure;

    // 3. Fit ROI 컬럼별 Z 집계 방식
    ZAggregation aggregation       = ZAggregation::Max;
    float        highTailPct       = 10.f;   // HighTail 상위 %

    // 4. Measure ROI Q 포인트 선택 (전체 포인트 HighTail)
    float        measureHighTailPct = 10.f;

    // 5. 기준면 모드 (Line / Plane)
    ReferenceMode referenceMode    = ReferenceMode::Line;

    // 6. RANSAC (Line 모드 전용)
    bool  useRansac            = false;
    int   ransacIterations     = 200;
    float ransacThresholdMm    = 0.05f;
};

// ─────────────────────────────────────────────────────────────────────
//  측정 결과
// ─────────────────────────────────────────────────────────────────────
struct LineFitResult {
    // ── 피팅 결과 (Line 모드) ──
    double slope      = 0.0;   // z = slope*x + intercept
    double intercept  = 0.0;
    int    inlierCount = 0;

    // ── 피팅 결과 (Plane 모드) ──
    double planeA = 0.0;   // z = planeA*x + planeB*y + planeC
    double planeB = 0.0;
    double planeC = 0.0;

    // ── 측정점 Q ──
    double Qx = 0.0;
    double Qy = 0.0;   // Plane 모드에서 유효
    double Qz = 0.0;

    // ── 기준면 위 Z값 ──
    double refZatQ = 0.0;   // Line: slope*Qx+intercept / Plane: a*Qx+b*Qy+c

    // ── 높이차 (mm) ──
    double heightDiff = 0.0;  // Qz - refZatQ

    // ── 수선의 발 (Line 모드 참고용) ──
    double Fx = 0.0;
    double Fz = 0.0;

    bool        valid   = false;
    std::string message;
};

// ─────────────────────────────────────────────────────────────────────
//  LineFitHeightMeasure Tool
// ─────────────────────────────────────────────────────────────────────
class LineFitHeightMeasure : public IAlgorithmTool {
public:
    explicit LineFitHeightMeasure(LineFitParams params = {});

    std::string name() const override { return "LineFitHeightMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;

    const LineFitResult& lastResult() const { return m_lastResult; }
    const LineFitParams& params()     const { return m_params; }
    void setParams(const LineFitParams& p)  { m_params = p; }

private:
    LineFitParams  m_params;
    LineFitResult  m_lastResult;

    // ── 타입 정의 ────────────────────────────────────────────────────
    using XZPair    = std::pair<double, double>;           // (x_mm, z_mm)
    using XYZTriplet = std::tuple<double, double, double>; // (x_mm, y_mm, z_mm)

    // ── 모드별 실행 ─────────────────────────────────────────────────
    ToolResult executeLine (const ZMap& map, VisionDataPtr input);
    ToolResult executePlane(const ZMap& map, VisionDataPtr input);

    // ── Line 모드 헬퍼 ───────────────────────────────────────────────
    std::vector<XZPair> extractPoints  (const ZMap& map, const Rect2D& roi) const;
    double              aggregateColumn(const ZMap& map, const Rect2D& roi, int col) const;
    XZPair              selectMeasurePoint(const ZMap& map) const;

    struct LineParams { double slope=0, intercept=0; int inliers=0; bool valid=false; };
    LineParams fitLS    (const std::vector<XZPair>& pts) const;
    LineParams fitRansac(const std::vector<XZPair>& pts) const;

    static void computeFoot(double Qx, double Qz,
                            double slope, double intercept,
                            double& Fx, double& Fz);

    // ── Plane 모드 헬퍼 ──────────────────────────────────────────────
    // Fit ROI에서 col별 (x_mm, y_mm, z_mm) 추출 (aggregation 적용)
    std::vector<XYZTriplet> extractPoints3D  (const ZMap& map, const Rect2D& roi) const;
    // Measure ROI 전체 포인트 → HighTail → (Qx, Qy, Qz)
    XYZTriplet              selectMeasurePoint3D(const ZMap& map) const;

    struct PlaneParams { double a=0, b=0, c=0; bool valid=false; };
    // z = a*x + b*y + c  최소제곱 피팅 (3×3 정규방정식)
    PlaneParams fitPlane(const std::vector<XYZTriplet>& pts) const;
};

} // namespace vision
