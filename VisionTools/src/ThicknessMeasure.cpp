#include "ThicknessMeasure.h"
#include "Logger.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace vision {

ThicknessMeasure::ThicknessMeasure(Params params) : m_params(params) {}

ToolResult ThicknessMeasure::execute(VisionDataPtr input) {
    if (!input || !input->hasCloud())
        return { ToolStatus::Fail, "ThicknessMeasure requires PointCloud3D" };

    const auto& pts = input->cloud->points;
    const auto& roi = m_params.roi;

    float zMin =  std::numeric_limits<float>::max();
    float zMax = -std::numeric_limits<float>::max();

    for (const auto& p : pts) {
        if (p.x < roi.xMin || p.x > roi.xMax) continue;
        if (p.y < roi.yMin || p.y > roi.yMax) continue;
        zMin = std::min(zMin, p.z);
        zMax = std::max(zMax, p.z);
    }

    if (zMin > zMax) {
        m_lastResult = {};
        return { ToolStatus::Fail, "no points in ROI" };
    }

    m_lastResult.thicknessMm = zMax - zMin;
    m_lastResult.minMm       = zMin;
    m_lastResult.maxMm       = zMax;

    if (m_params.nominalMm > 0.f) {
        float diff = std::fabs(m_lastResult.thicknessMm - m_params.nominalMm);
        m_lastResult.pass = (diff <= m_params.toleranceMm);
    } else {
        m_lastResult.pass = true;
    }

    VISION_LOG_INFO("ThicknessMeasure: {:.4f} mm, pass={}",
        m_lastResult.thicknessMm, m_lastResult.pass);

    auto out = std::make_shared<VisionData>(*input);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
