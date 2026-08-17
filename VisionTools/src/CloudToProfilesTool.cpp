#include "CloudToProfilesTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace vision {

ToolResult CloudToProfilesTool::execute(VisionDataPtr input) {
    if (!input || !input->inCloud(0))
        return { ToolStatus::Fail, "CloudToProfiles: PointCloud 입력이 필요합니다" };
    const auto& cloud = *input->inCloud(0);
    if (cloud.empty())
        return { ToolStatus::Fail, "CloudToProfiles: 빈 PointCloud" };

    const double yStep = m_p.yStepMm > 1e-9 ? m_p.yStepMm : 0.1;
    const double xStep = m_p.xStepMm > 1e-9 ? m_p.xStepMm : 0.1;

    // Y·X 범위 스캔
    double yMin = 1e300, yMax = -1e300, xMin = 1e300, xMax = -1e300;
    for (const auto& p : cloud.points) {
        if (p.y < yMin) yMin = p.y; if (p.y > yMax) yMax = p.y;
        if (p.x < xMin) xMin = p.x; if (p.x > xMax) xMax = p.x;
    }
    const int nRows = std::max(1, (int)std::floor((yMax - yMin) / yStep) + 1);

    // 점을 행 버킷으로
    std::vector<std::vector<const Point3f*>> rows((size_t)nRows);
    for (const auto& p : cloud.points) {
        int r = (int)std::floor((p.y - yMin) / yStep);
        if (r < 0) r = 0; if (r >= nRows) r = nRows - 1;
        rows[(size_t)r].push_back(&p);
    }

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    const double NaN = std::numeric_limits<double>::quiet_NaN();

    long emitted = 0;
    for (int r = 0; r < nRows; ++r) {
        auto& pts = rows[(size_t)r];
        if ((int)pts.size() < m_p.minPoints) continue;
        const double yCenter = yMin + (r + 0.5) * yStep;

        auto prof = std::make_shared<Profile>();
        prof->frameId = cloud.frameId;
        prof->label   = "row:" + std::to_string(r);

        if (m_p.reduce == Reduce::None) {
            // 모든 점을 샘플로 보존 (다중 Z 유지). x 오름차순 정렬.
            std::sort(pts.begin(), pts.end(),
                      [](const Point3f* a, const Point3f* b){ return a->x < b->x; });
            prof->x.reserve(pts.size()); prof->y.reserve(pts.size());
            prof->z.reserve(pts.size()); prof->s.reserve(pts.size());
            for (const auto* p : pts) {
                prof->x.push_back(p->x);
                prof->y.push_back(p->y);
                prof->z.push_back(p->z);
                prof->s.push_back(p->x - xMin);
            }
        } else {
            // X-bin 축약 → 정규 1D 신호
            const int nCol = std::max(1, (int)std::floor((xMax - xMin) / xStep) + 1);
            std::vector<double> acc((size_t)nCol, 0.0);
            std::vector<int>    cnt((size_t)nCol, 0);
            std::vector<double> rep((size_t)nCol, NaN);   // Max/Min 대표값
            for (const auto* p : pts) {
                int c = (int)std::floor((p->x - xMin) / xStep);
                if (c < 0) c = 0; if (c >= nCol) c = nCol - 1;
                if (m_p.reduce == Reduce::Mean) { acc[(size_t)c] += p->z; cnt[(size_t)c]++; }
                else {
                    double& v = rep[(size_t)c];
                    if (std::isnan(v)) v = p->z;
                    else if (m_p.reduce == Reduce::Max) v = std::max(v, (double)p->z);
                    else                                v = std::min(v, (double)p->z);
                }
            }
            prof->x.resize((size_t)nCol); prof->y.resize((size_t)nCol);
            prof->z.resize((size_t)nCol); prof->s.resize((size_t)nCol);
            for (int c = 0; c < nCol; ++c) {
                prof->x[(size_t)c] = xMin + (c + 0.5) * xStep;
                prof->y[(size_t)c] = yCenter;
                prof->s[(size_t)c] = c * xStep;
                prof->z[(size_t)c] = (m_p.reduce == Reduce::Mean)
                    ? (cnt[(size_t)c] > 0 ? acc[(size_t)c] / cnt[(size_t)c] : NaN)
                    : rep[(size_t)c];
            }
        }

        out->profiles.push_back(std::move(prof));
        ++emitted;
    }

    if (out->profiles.empty())
        return { ToolStatus::Fail, "CloudToProfiles: 생성된 행 Profile이 없습니다 (minPoints/yStep 확인)" };

    VISION_LOG_INFO("CloudToProfiles: {} rows → {} profiles (yStep={:.3f}, reduce={})",
                    nRows, emitted, yStep, (int)m_p.reduce);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
