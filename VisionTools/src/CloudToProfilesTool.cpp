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

    const bool scanX = (m_p.scanAxis == Axis::X);
    // 스캔축 = 프로파일이 늘어선 방향(행). 횡(lateral)축 = 프로파일 내부 자유축(플롯 X).
    auto scanC = [&](const Point3f& p) -> double { return scanX ? p.x : p.y; };
    auto latC  = [&](const Point3f& p) -> double { return scanX ? p.y : p.x; };

    const double scanStep = m_p.scanStepMm > 1e-9 ? m_p.scanStepMm : 0.1;
    const double latStep  = m_p.latStepMm  > 1e-9 ? m_p.latStepMm  : 0.1;

    // 범위 스캔
    double scanMin = 1e300, scanMax = -1e300, latMin = 1e300, latMax = -1e300;
    for (const auto& p : cloud.points) {
        double sc = scanC(p), lt = latC(p);
        if (sc < scanMin) scanMin = sc; if (sc > scanMax) scanMax = sc;
        if (lt < latMin)  latMin  = lt; if (lt > latMax)  latMax  = lt;
    }
    const int nRows = std::max(1, (int)std::floor((scanMax - scanMin) / scanStep) + 1);

    // 스캔축 bin으로 행 버킷
    std::vector<std::vector<const Point3f*>> rows((size_t)nRows);
    for (const auto& p : cloud.points) {
        int r = (int)std::floor((scanC(p) - scanMin) / scanStep);
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
        const double scanCenter = scanMin + (r + 0.5) * scanStep;

        auto prof = std::make_shared<Profile>();
        prof->frameId = cloud.frameId;
        prof->label   = "row:" + std::to_string(r);

        if (m_p.reduce == Reduce::None) {
            // 행의 모든 점 보존. 횡좌표 오름차순. (같은 lat에 여러 z = 여러 샘플, 다중 높이 유지)
            std::sort(pts.begin(), pts.end(),
                      [&](const Point3f* a, const Point3f* b){ return latC(*a) < latC(*b); });
            prof->x.reserve(pts.size()); prof->y.reserve(pts.size());
            prof->z.reserve(pts.size()); prof->s.reserve(pts.size());
            for (const auto* p : pts) {
                const double lt = latC(*p);
                prof->x.push_back(lt);          // 플롯 축 = 횡(lateral)
                prof->y.push_back(scanCenter);  // 스캔 위치(행 고정)
                prof->z.push_back(p->z);
                prof->s.push_back(lt - latMin);
            }
        } else {
            // 횡축 bin으로 축약 → 정규 1D 신호
            const int nLat = std::max(1, (int)std::floor((latMax - latMin) / latStep) + 1);
            std::vector<double> acc((size_t)nLat, 0.0);
            std::vector<int>    cnt((size_t)nLat, 0);
            std::vector<double> rep((size_t)nLat, NaN);
            for (const auto* p : pts) {
                int c = (int)std::floor((latC(*p) - latMin) / latStep);
                if (c < 0) c = 0; if (c >= nLat) c = nLat - 1;
                if (m_p.reduce == Reduce::Mean) { acc[(size_t)c] += p->z; cnt[(size_t)c]++; }
                else {
                    double& v = rep[(size_t)c];
                    if (std::isnan(v)) v = p->z;
                    else if (m_p.reduce == Reduce::Max) v = std::max(v, (double)p->z);
                    else                                v = std::min(v, (double)p->z);
                }
            }
            prof->x.resize((size_t)nLat); prof->y.resize((size_t)nLat);
            prof->z.resize((size_t)nLat); prof->s.resize((size_t)nLat);
            for (int c = 0; c < nLat; ++c) {
                prof->x[(size_t)c] = latMin + (c + 0.5) * latStep;
                prof->y[(size_t)c] = scanCenter;
                prof->s[(size_t)c] = c * latStep;
                prof->z[(size_t)c] = (m_p.reduce == Reduce::Mean)
                    ? (cnt[(size_t)c] > 0 ? acc[(size_t)c] / cnt[(size_t)c] : NaN)
                    : rep[(size_t)c];
            }
        }

        out->profiles.push_back(std::move(prof));
        ++emitted;
    }

    if (out->profiles.empty())
        return { ToolStatus::Fail, "CloudToProfiles: 생성된 행 Profile이 없습니다 (minPoints/scanStep 확인)" };

    VISION_LOG_INFO("CloudToProfiles: scanAxis={} {} rows → {} profiles (step={:.3f}, reduce={})",
                    scanX ? "X" : "Y", nRows, emitted, scanStep, (int)m_p.reduce);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
