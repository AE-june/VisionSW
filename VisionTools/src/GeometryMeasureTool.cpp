#include "GeometryMeasureTool.h"
#include <cmath>
#include <limits>
#include <utility>

namespace vision {

static constexpr double kPi = 3.14159265358979323846;

GeometryMeasureTool::GeometryMeasureTool(GeometryMeasureParams params)
    : m_params(std::move(params)) {}

ToolResult GeometryMeasureTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "GeometryMeasure: 입력이 없습니다." };

    const std::string& kind = m_params.kind;
    double result = 0;
    std::string unit;

    // ── Plane 연산 ────────────────────────────────────────────────────
    if (kind == "planeAngle" || kind == "planeDistance" || kind == "planePointDistance") {
        const auto pA = input->inPlane(0);
        if (!pA || !pA->valid)
            return { ToolStatus::Fail, "GeometryMeasure: 포트0 Plane이 없습니다." };

        if (kind == "planePointDistance") {
            // 포트1: RegionMeasure의 cxMm, cyMm, zMm Measurement
            // 거리 = |zMm - (a·x + b·y + c)| / sqrt(1 + a² + b²)
            const auto& meas = input->inputs.size() > 1 && input->inputs[1]
                                ? input->inputs[1]->measurements
                                : std::vector<Measurement>{};
            auto findMeas = [&](const std::string& name) -> double {
                for (const auto& m : meas)
                    if (m.name == name && m.valid) return m.value;
                return std::numeric_limits<double>::quiet_NaN();
            };
            const double px = findMeas("cxMm");
            const double py = findMeas("cyMm");
            const double pz = findMeas("zMm");
            if (std::isnan(px) || std::isnan(py) || std::isnan(pz))
                return { ToolStatus::Fail,
                    "GeometryMeasure: 포트1에 cxMm/cyMm/zMm Measurement가 없습니다. "
                    "RegionMeasure 출력을 포트1에 연결하세요." };
            const double norm = std::sqrt(1.0 + pA->a*pA->a + pA->b*pA->b);
            result = std::abs(pz - (pA->a * px + pA->b * py + pA->c)) / norm;
            unit = "mm";
        } else {
            const auto pB = input->inPlane(1);
            if (!pB || !pB->valid)
                return { ToolStatus::Fail, "GeometryMeasure: 포트1 Plane이 필요합니다." };

            if (kind == "planeAngle") {
                const double nax = -pA->a, nay = -pA->b, naz = 1.0;
                const double nbx = -pB->a, nby = -pB->b, nbz = 1.0;
                const double n1  = std::sqrt(nax*nax + nay*nay + naz*naz);
                const double n2  = std::sqrt(nbx*nbx + nby*nby + nbz*nbz);
                double dot = std::abs(nax*nbx + nay*nby + naz*nbz) / (n1 * n2);
                dot = std::min(1.0, dot);
                result = std::acos(dot) * 180.0 / kPi;
                unit = "deg";
            } else {   // planeDistance
                const double norm = std::sqrt(1.0 + pA->a*pA->a + pA->b*pA->b);
                result = std::abs(pA->c - pB->c) / norm;
                unit = "mm";
            }
        }
    }
    // ── Circle 연산 ───────────────────────────────────────────────────
    else if (kind == "circlePosX" || kind == "circlePosY" ||
             kind == "circleRadius" || kind == "circleDiameter" ||
             kind == "circleResidual") {
        const auto gA = input->inGeometry(0);
        if (!gA || !gA->valid || gA->kind != GeoKind::Circle)
            return { ToolStatus::Fail, "GeometryMeasure: 포트0 Circle Geometry가 없습니다. CircleFit 출력을 연결하세요." };
        if      (kind == "circlePosX")    { result = gA->cxMm;             unit = "mm"; }
        else if (kind == "circlePosY")    { result = gA->cyMm;             unit = "mm"; }
        else if (kind == "circleRadius")  { result = gA->radiusMm;         unit = "mm"; }
        else if (kind == "circleDiameter"){ result = gA->radiusMm * 2.0;   unit = "mm"; }
        else                              { result = gA->residualMm;        unit = "mm"; }
    }
    // ── Line 연산 ─────────────────────────────────────────────────────
    else {
        const auto lA = input->inLine(0);
        const auto lB = input->inLine(1);   // 일부 kind에서만 필요

        if (!lA || !lA->valid)
            return { ToolStatus::Fail, "GeometryMeasure: 포트0 Line이 없습니다." };

        if (kind == "posX") {
            result = lA->cxMm; unit = "mm";
        } else if (kind == "posY") {
            result = lA->cyMm; unit = "mm";
        } else {
            if (!lB || !lB->valid)
                return { ToolStatus::Fail, "GeometryMeasure: 포트1 Line이 필요합니다." };

            // 방향 단위벡터 (angleDeg 기준)
            const double a1 = lA->angleDeg * kPi / 180.0;
            const double a2 = lB->angleDeg * kPi / 180.0;
            const double dx1 = std::cos(a1), dy1 = std::sin(a1);
            const double dx2 = std::cos(a2), dy2 = std::sin(a2);

            if (kind == "angle") {
                double dot = std::abs(dx1 * dx2 + dy1 * dy2);
                dot = std::min(1.0, dot);
                result = std::acos(dot) * 180.0 / kPi;
                unit = "deg";
            } else if (kind == "lineDistance") {
                const double dx = lB->cxMm - lA->cxMm;
                const double dy = lB->cyMm - lA->cyMm;
                result = std::abs(-dy1 * dx + dx1 * dy);
                unit = "mm";
            } else if (kind == "intersectX" || kind == "intersectY") {
                const double denom = dx1 * dy2 - dy1 * dx2;
                if (std::abs(denom) < 1e-10)
                    return { ToolStatus::Fail, "GeometryMeasure: 두 라인이 평행합니다(교점 없음)." };
                const double bax = lB->cxMm - lA->cxMm;
                const double bay = lB->cyMm - lA->cyMm;
                const double t   = (bax * dy2 - bay * dx2) / denom;
                result = (kind == "intersectX") ? (lA->cxMm + t * dx1)
                                                : (lA->cyMm + t * dy1);
                unit = "mm";
            } else if (kind == "centerDistance") {
                const double dx = lB->cxMm - lA->cxMm;
                const double dy = lB->cyMm - lA->cyMm;
                result = std::hypot(dx, dy);
                unit = "mm";
            } else {
                return { ToolStatus::Fail, "GeometryMeasure: 알 수 없는 kind: " + kind };
            }
        }
    }

    const std::string outName = m_params.outputName.empty() ? kind : m_params.outputName;
    const std::string outUnit = m_params.outputUnit.empty() ? unit : m_params.outputUnit;

    Measurement m;
    m.name  = outName;
    m.value = result;
    m.unit  = outUnit;
    m.valid = true;

    auto vd = std::make_shared<VisionData>();
    vd->measurements.push_back(m);
    vd->sourceId = input->sourceId;
    vd->frames   = input->frames;
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
