#include "CloudSorFilterTool.h"
#include "Logger.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace vision {

namespace {

// 3D 격자 셀 좌표(ix,iy,iz)를 하나의 키로 패킹 — 21bit/축(각 ±1,048,576 셀 범위,
// cellSizeMm=0.02mm 기준 ±21m 커버). 이 범위를 넘는 극단적 좌표/셀크기 조합은 가정하지 않음.
constexpr int64_t kBias = 1 << 20;
inline int64_t makeCellKey(int ix, int iy, int iz) {
    int64_t x = (int64_t)ix + kBias;
    int64_t y = (int64_t)iy + kBias;
    int64_t z = (int64_t)iz + kBias;
    return (x << 42) | (y << 21) | z;
}

} // namespace

ToolResult CloudSorFilterTool::execute(VisionDataPtr input) {
    if (!input || !input->inCloud(0))
        return { ToolStatus::Fail, "CloudSorFilter: PointCloud3D 입력이 없습니다" };

    const auto& src = *input->inCloud(0);
    const auto& pts = src.points;
    const size_t n = pts.size();
    if (n == 0)
        return { ToolStatus::Fail, "CloudSorFilter: 입력 클라우드에 점이 없습니다" };

    if (m_p.cellSizeMm <= 0)
        return { ToolStatus::Fail, "CloudSorFilter: cellSizeMm은 0보다 커야 합니다" };
    const int k = std::max(1, m_p.kNeighbors);
    const double cell = m_p.cellSizeMm;

    auto cellOf = [&](const Point3f& p) {
        return std::array<int, 3>{
            (int)std::floor((double)p.x / cell),
            (int)std::floor((double)p.y / cell),
            (int)std::floor((double)p.z / cell)
        };
    };

    // 1) 공간 해시 격자 구축 — 셀 키 → 점 인덱스 목록
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n * 2);
    for (uint32_t i = 0; i < (uint32_t)n; ++i) {
        auto c = cellOf(pts[i]);
        grid[makeCellKey(c[0], c[1], c[2])].push_back(i);
    }

    // 2) 점마다 k-최근접 이웃까지의 평균거리 계산 — 셀 링을 넓혀가며 후보 수집
    std::vector<float> meanDist(n, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> distBuf;
    distBuf.reserve((size_t)k * 8);

    for (size_t i = 0; i < n; ++i) {
        const Point3f& p = pts[i];
        auto c = cellOf(p);
        distBuf.clear();
        int ring = 0;
        bool haveEnoughRing = false;   // k개 이상 모인 링을 찾았는지
        for (; ring <= 64; ++ring) {   // 상한(64링)은 무한루프 방지용 안전장치
            distBuf.clear();
            for (int dz = -ring; dz <= ring; ++dz)
                for (int dy = -ring; dy <= ring; ++dy)
                    for (int dx = -ring; dx <= ring; ++dx) {
                        auto it = grid.find(makeCellKey(c[0] + dx, c[1] + dy, c[2] + dz));
                        if (it == grid.end()) continue;
                        for (uint32_t j : it->second) {
                            if (j == i) continue;
                            const Point3f& q = pts[j];
                            float ddx = p.x - q.x, ddy = p.y - q.y, ddz = p.z - q.z;
                            distBuf.push_back(ddx*ddx + ddy*ddy + ddz*ddz);
                        }
                    }
            if ((int)distBuf.size() >= k) {
                if (haveEnoughRing) break;   // 이미 한 번 더 넓혀봤다 → 종료
                haveEnoughRing = true;        // 넉넉한 링을 한 번 더 넓혀 더 가까운 점 확인
            }
        }
        if (!distBuf.empty()) {
            const int kk = std::min((int)distBuf.size(), k);
            std::partial_sort(distBuf.begin(), distBuf.begin() + kk, distBuf.end());
            double sum = 0.0;
            for (int t = 0; t < kk; ++t) sum += std::sqrt((double)distBuf[t]);
            meanDist[i] = (float)(sum / kk);
        }
        // distBuf가 끝까지 비어있으면(고립점) meanDist=NaN 유지 → 아래에서 이상치로 처리
    }

    // 3) 전역 평균·표준편차 (유효한 meanDist만)
    double sum = 0.0, sumSq = 0.0;
    size_t validCnt = 0;
    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(meanDist[i])) continue;
        sum += meanDist[i]; sumSq += (double)meanDist[i] * meanDist[i];
        ++validCnt;
    }
    if (validCnt == 0)
        return { ToolStatus::Fail, "CloudSorFilter: 이웃을 찾은 점이 없습니다(cellSizeMm을 늘려보세요)" };
    const double mean = sum / validCnt;
    const double var  = std::max(0.0, sumSq / validCnt - mean * mean);
    const double sd   = std::sqrt(var);
    const double threshold = mean + m_p.stdRatio * sd;

    // 4) 임계값 이하만 통과 (고립점=NaN은 자동 이상치)
    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;
    auto outCloud = std::make_shared<PointCloud3D>();
    outCloud->frameId = src.frameId;
    outCloud->points.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(meanDist[i]) && meanDist[i] <= threshold)
            outCloud->points.push_back(pts[i]);
    }
    out->setCloud(outCloud);

    VISION_LOG_INFO("CloudSorFilter: {} → {} points (k={}, cellSizeMm={:.4f}, threshold={:.5f}mm)",
                     n, outCloud->points.size(), k, cell, threshold);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
