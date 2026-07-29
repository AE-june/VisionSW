#include "CreateRoiTool.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace vision {

CreateRoiTool::CreateRoiTool(CreateRoiParams params) : m_params(std::move(params)) {}

ToolResult CreateRoiTool::execute(VisionDataPtr input) {
    if (!input || !input->hasHeightMap())
        return { ToolStatus::Fail, "CreateROI: 크기 기준 HeightMap이 없습니다." };
    if (m_params.rois.empty())
        return { ToolStatus::Fail, "CreateROI: ROI가 없습니다." };

    const HeightMap& map = *input->heightmap;
    const int w = map.width, h = map.height;
    const int offCol = static_cast<int>(std::lround(map.originCol));
    const int offRow = static_cast<int>(std::lround(map.originRow));

    auto rg = std::make_shared<Region>(Region::makeEmpty(w, h));

    for (const auto& roi : m_params.rois) {
        if (!roi.poly.empty()) {
            // 폴리곤: 꼭짓점 %→px, bbox 내부에서 ray-casting 판정
            std::vector<std::array<double, 2>> pv;
            pv.reserve(roi.poly.size());
            double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
            for (const auto& v : roi.poly) {
                double px = static_cast<double>(v[0]) * w + offCol;
                double py = static_cast<double>(v[1]) * h + offRow;
                pv.push_back({ px, py });
                minx = std::min(minx, px); maxx = std::max(maxx, px);
                miny = std::min(miny, py); maxy = std::max(maxy, py);
            }
            int x0 = std::clamp(static_cast<int>(std::floor(minx)), 0, w);
            int x1 = std::clamp(static_cast<int>(std::ceil(maxx)) + 1, 0, w);
            int y0 = std::clamp(static_cast<int>(std::floor(miny)), 0, h);
            int y1 = std::clamp(static_cast<int>(std::ceil(maxy)) + 1, 0, h);
            const int nv = static_cast<int>(pv.size());
            for (int r = y0; r < y1; ++r)
                for (int c = x0; c < x1; ++c) {
                    bool in = false;
                    for (int i = 0, j = nv - 1; i < nv; j = i++) {
                        double xi = pv[i][0], yi = pv[i][1];
                        double xj = pv[j][0], yj = pv[j][1];
                        if (((yi > r) != (yj > r)) &&
                            (c < (xj - xi) * (r - yi) / (yj - yi) + xi)) in = !in;
                    }
                    if (in) rg->mask[static_cast<size_t>(r) * w + c] = 1;
                }
        } else {
            // 사각 / 원(내접 타원) — angleDeg 있으면 중심 기준 회전
            // 정의 박스(회전 전) 중심·반경은 클램프 전 좌표로 계산(클램프하면 중심이 밀림).
            const double bx0 = roi.xPct * w + offCol;
            const double by0 = roi.yPct * h + offRow;
            const double bx1 = (roi.xPct + roi.wPct) * w + offCol;
            const double by1 = (roi.yPct + roi.hPct) * h + offRow;
            const double cx = (bx0 + bx1) * 0.5, cy = (by0 + by1) * 0.5;
            const double rx = std::max(1.0, (bx1 - bx0) * 0.5);
            const double ry = std::max(1.0, (by1 - by0) * 0.5);
            const double th = roi.angleDeg * 3.14159265358979323846 / 180.0;
            const double ct = std::cos(th), st = std::sin(th);
            // 순회 bbox: 회전 시 회전 AABB로 확장.
            const double ex = std::abs(rx * ct) + std::abs(ry * st);
            const double ey = std::abs(rx * st) + std::abs(ry * ct);
            int x0 = std::clamp(static_cast<int>(std::floor(cx - ex)), 0, w);
            int x1 = std::clamp(static_cast<int>(std::ceil (cx + ex)) + 1, 0, w);
            int y0 = std::clamp(static_cast<int>(std::floor(cy - ey)), 0, h);
            int y1 = std::clamp(static_cast<int>(std::ceil (cy + ey)) + 1, 0, h);
            for (int r = y0; r < y1; ++r)
                for (int c = x0; c < x1; ++c) {
                    // 픽셀을 로컬 프레임으로 역회전(-angle).
                    const double dx = c - cx, dy = r - cy;
                    const double lx =  dx * ct + dy * st;
                    const double ly = -dx * st + dy * ct;
                    if (roi.isCircle) {
                        const double nx = lx / rx, ny = ly / ry;
                        if (nx * nx + ny * ny > 1.0) continue;
                    } else {
                        if (std::abs(lx) > rx || std::abs(ly) > ry) continue;
                    }
                    rg->mask[static_cast<size_t>(r) * w + c] = 1;
                }
        }
    }

    auto out = std::make_shared<VisionData>();
    out->region   = rg;
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
