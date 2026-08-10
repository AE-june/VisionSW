#include "CreateRoiTool.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace vision {

CreateRoiTool::CreateRoiTool(CreateRoiParams params) : m_params(std::move(params)) {}

// 단일 ROI를 빈 Region에 래스터화. Region 크기는 호출 전 이미 w×h로 초기화돼 있어야 함.
static void rasterizeRoi(const CreateRoiParams::ROI& roi,
                          int w, int h, int offCol, int offRow,
                          Region& out) {
    if (!roi.poly.empty()) {
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
                if (in) out.mask[static_cast<size_t>(r) * w + c] = 1;
            }
    } else {
        const double bx0 = roi.xPct * w + offCol;
        const double by0 = roi.yPct * h + offRow;
        const double bx1 = (roi.xPct + roi.wPct) * w + offCol;
        const double by1 = (roi.yPct + roi.hPct) * h + offRow;
        const double cx = (bx0 + bx1) * 0.5, cy = (by0 + by1) * 0.5;
        const double rx = std::max(1.0, (bx1 - bx0) * 0.5);
        const double ry = std::max(1.0, (by1 - by0) * 0.5);
        const double th = roi.angleDeg * 3.14159265358979323846 / 180.0;
        const double ct = std::cos(th), st = std::sin(th);
        const double ex = std::abs(rx * ct) + std::abs(ry * st);
        const double ey = std::abs(rx * st) + std::abs(ry * ct);
        int x0 = std::clamp(static_cast<int>(std::floor(cx - ex)), 0, w);
        int x1 = std::clamp(static_cast<int>(std::ceil (cx + ex)) + 1, 0, w);
        int y0 = std::clamp(static_cast<int>(std::floor(cy - ey)), 0, h);
        int y1 = std::clamp(static_cast<int>(std::ceil (cy + ey)) + 1, 0, h);
        for (int r = y0; r < y1; ++r)
            for (int c = x0; c < x1; ++c) {
                const double dx = c - cx, dy = r - cy;
                const double lx =  dx * ct + dy * st;
                const double ly = -dx * st + dy * ct;
                if (roi.isCircle) {
                    const double nx = lx / rx, ny = ly / ry;
                    if (nx * nx + ny * ny > 1.0) continue;
                } else {
                    if (std::abs(lx) > rx || std::abs(ly) > ry) continue;
                }
                out.mask[static_cast<size_t>(r) * w + c] = 1;
            }
    }
}

// 라인 정렬 밴드를 mm-공간에서 래스터화 (xResMm≠yResMm 비등방 정확).
//   center(mm)·dir(단위)·법선 n=(-dirY,dirX). |along|≤len/2 && |perp|≤width/2.
static void rasterizeBand(const HeightMap& map, Region& out,
                          double cxMm, double cyMm,
                          double dirX, double dirY,
                          double lengthMm, double widthMm) {
    const double nX = -dirY, nY = dirX;
    const double halfLen = lengthMm * 0.5, halfWid = widthMm * 0.5;
    for (int r = 0; r < map.height; ++r) {
        const double yMm = map.yMm(r);
        for (int c = 0; c < map.width; ++c) {
            const double xMm = map.xMm(c);
            const double ex = xMm - cxMm, ey = yMm - cyMm;
            const double along = ex * dirX + ey * dirY;
            const double perp  = ex * nX   + ey * nY;
            if (std::abs(along) <= halfLen && std::abs(perp) <= halfWid)
                out.mask[static_cast<size_t>(r) * map.width + c] = 1;
        }
    }
}

ToolResult CreateRoiTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "CreateROI: 크기 기준 HeightMap이 없습니다." };

    const HeightMap& map = *input->inHeightMap(0);
    const int w = map.width, h = map.height;

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;

    // ── 라인 밴드 모드 (포트1 Line 연결 시) ─────────────────────────────
    if (auto lm = input->inLine(1)) {
        // 방향·중심을 실제 끝점(mm)에서 — 그려지는 라인(p0→p1)과 정확히 일치.
        // 끝점 없으면 angle+center로 폴백.
        const double segLen = std::hypot(lm->p1xMm - lm->p0xMm, lm->p1yMm - lm->p0yMm);
        double dirX, dirY, midX, midY, lineLen;
        if (segLen > 1e-9) {
            dirX = (lm->p1xMm - lm->p0xMm) / segLen;
            dirY = (lm->p1yMm - lm->p0yMm) / segLen;
            midX = (lm->p0xMm + lm->p1xMm) * 0.5;
            midY = (lm->p0yMm + lm->p1yMm) * 0.5;
            lineLen = segLen;
        } else {
            const double th = lm->angleDeg * 3.14159265358979323846 / 180.0;
            dirX = std::cos(th); dirY = std::sin(th);
            midX = lm->cxMm; midY = lm->cyMm; lineLen = lm->lengthMm;
        }
        const double nX = -dirY, nY = dirX;
        const double lenMm = (m_params.bandLenMode == CreateRoiParams::BandLen::Line)
                             ? lineLen : m_params.bandLengthMm;
        if (lenMm <= 0.0)
            return { ToolStatus::Fail, "CreateROI: 밴드 길이가 0입니다 (라인 길이/파라미터 확인)." };

        Overlay ov;
        ov.kind = Overlay::Kind::Lines;
        const double halfLen = lenMm * 0.5, halfWid = m_params.bandWidthMm * 0.5;
        // mm 좌표 → px (프랙셔널)
        auto toPx = [&](double xMm, double yMm, double& c, double& r) {
            c = xMm / map.xResMm + map.originCol;
            r = yMm / map.yResMm + map.originRow;
        };

        auto emitBand = [&](double sign, const std::string& label, int bandIdx) {
            auto rg = std::make_shared<Region>(Region::makeEmpty(w, h));
            rg->frameId = map.frameId;
            rg->label   = label;
            const double bcx = midX + sign * m_params.bandOffsetMm * nX;
            const double bcy = midY + sign * m_params.bandOffsetMm * nY;
            rasterizeBand(map, *rg, bcx, bcy, dirX, dirY, lenMm, m_params.bandWidthMm);
            out->regions.push_back(std::move(rg));

            // 외곽선 4변 → overlay lines (px)
            double cxx[4], cyy[4];
            const double sgn[4][2] = { {+1,+1}, {+1,-1}, {-1,-1}, {-1,+1} };
            for (int k = 0; k < 4; ++k) {
                const double mmX = bcx + sgn[k][0] * halfLen * dirX + sgn[k][1] * halfWid * nX;
                const double mmY = bcy + sgn[k][0] * halfLen * dirY + sgn[k][1] * halfWid * nY;
                toPx(mmX, mmY, cxx[k], cyy[k]);
            }
            for (int k = 0; k < 4; ++k) {
                Overlay::LineData ld;
                ld.p0x = cxx[k];       ld.p0y = cyy[k];
                ld.p1x = cxx[(k+1)%4]; ld.p1y = cyy[(k+1)%4];
                ld.roiIndex = bandIdx;
                ov.lines.push_back(ld);
            }
        };
        using BS = CreateRoiParams::BandSide;
        int idx = 0;
        if (m_params.bandSide == BS::Left  || m_params.bandSide == BS::Both) emitBand(+1.0, "left",  idx++);
        if (m_params.bandSide == BS::Right || m_params.bandSide == BS::Both) emitBand(-1.0, "right", idx++);
        out->overlays.push_back(std::move(ov));

        return { ToolStatus::Ok, "", out };
    }

    // ── 정적 ROI 모드 ──────────────────────────────────────────────────
    if (m_params.rois.empty())
        return { ToolStatus::Fail, "CreateROI: ROI가 없습니다." };

    const int offCol = static_cast<int>(std::lround(map.originCol));
    const int offRow = static_cast<int>(std::lround(map.originRow));
    for (const auto& roi : m_params.rois) {
        auto rg = std::make_shared<Region>(Region::makeEmpty(w, h));
        rg->frameId = map.frameId;
        rg->label   = roi.id;
        rasterizeRoi(roi, w, h, offCol, offRow, *rg);
        out->regions.push_back(std::move(rg));
    }

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
