#include "CircleFitTool.h"
#include <Eigen/Dense>
#include <cmath>
#include <vector>

namespace vision {

ToolResult CircleFitTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "CircleFit: 입력이 없습니다." };

    const auto& regions = input->inRegions(0);
    if (regions.empty() || !regions[0] || regions[0]->empty())
        return { ToolStatus::Fail, "CircleFit: Region 입력이 없습니다." };

    const Region& rg  = *regions[0];
    const int     w   = rg.width;
    const int     h   = rg.height;
    const auto    hm  = input->inHeightMap(1);   // mm 변환용 (선택)
    const bool    hasMm = (hm != nullptr);

    // 경계 픽셀 수집 (4-connected)
    std::vector<double> bx, by;
    bx.reserve(1024); by.reserve(1024);

    auto inside = [&](int c, int r) -> bool {
        if (c < 0 || c >= w || r < 0 || r >= h) return false;
        return rg.mask[static_cast<size_t>(r) * w + c] != 0;
    };

    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            if (!inside(c, r)) continue;
            if (!inside(c-1,r) || !inside(c+1,r) || !inside(c,r-1) || !inside(c,r+1)) {
                // 경계 픽셀 → mm 좌표로 저장
                if (hasMm) {
                    bx.push_back((c - hm->originCol) * hm->xResMm);
                    by.push_back((r - hm->originRow) * hm->yResMm);
                } else {
                    bx.push_back(c);
                    by.push_back(r);
                }
            }
        }
    }

    const int N = static_cast<int>(bx.size());
    if (N < 3)
        return { ToolStatus::Fail, "CircleFit: 경계 픽셀이 3개 미만입니다." };

    // 선형 최소제곱: 2*cx*xi + 2*cy*yi + d = xi² + yi²
    // d = r² - cx² - cy²
    Eigen::MatrixXd A(N, 3);
    Eigen::VectorXd b(N);
    for (int i = 0; i < N; ++i) {
        A(i, 0) = 2.0 * bx[i];
        A(i, 1) = 2.0 * by[i];
        A(i, 2) = 1.0;
        b(i)    = bx[i]*bx[i] + by[i]*by[i];
    }
    Eigen::Vector3d sol = A.jacobiSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(b);
    const double cx = sol(0), cy = sol(1), d = sol(2);
    const double r2 = cx*cx + cy*cy + d;
    if (r2 <= 0) return { ToolStatus::Fail, "CircleFit: 피팅 실패 (r² ≤ 0)." };
    const double radius = std::sqrt(r2);

    // 잔차 RMS
    double rss = 0;
    for (int i = 0; i < N; ++i) {
        double dist = std::sqrt((bx[i]-cx)*(bx[i]-cx) + (by[i]-cy)*(by[i]-cy));
        double e = dist - radius; rss += e * e;
    }
    const double residual = std::sqrt(rss / N);

    auto geo = std::make_shared<Geometry>();
    geo->kind     = GeoKind::Circle;
    geo->valid    = true;
    geo->cxMm     = cx;
    geo->cyMm     = cy;
    geo->radiusMm = radius;
    geo->residualMm = residual;

    auto out = std::make_shared<VisionData>();
    out->setGeometry(geo);
    out->measurements.push_back({"radius",   radius,       "mm", true});
    out->measurements.push_back({"diameter",  2.0 * radius, "mm", true});
    out->measurements.push_back({"residual",  residual,     "mm", true});
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
