#include "LineCenterTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>

namespace vision {

bool LineCenterTool::findLine(const HeightMap& map, int x0, int y0, int x1, int y1,
                              double& outCx, double& outCy, double& outAngleDeg, int& outCount) const {
    const float thr = m_params.threshold;
    const bool scanX = (m_params.scanDir == ScanDir::Lr || m_params.scanDir == ScanDir::Rl);
    const bool fwd   = (m_params.scanDir == ScanDir::Lr || m_params.scanDir == ScanDir::Tb);
    const bool d2l   = (m_params.polarity == Polarity::DarkToLight);

    double sx=0, sy=0, sxx=0, syy=0, sxy=0;
    long n=0;

    if (scanX) {
        // 스캔 = 열(X방향), 수직 = 행(Y)
        for (int row = y0; row < y1; ++row) {
            bool havePrev=false, prevAbove=false;
            for (int dc=0; dc<(x1-x0); ++dc) {
                int col = fwd ? (x0+dc) : (x1-1-dc);
                bool valid = map.inBounds(col, row) && map.valid(col, row);
                bool above = valid && (map.rawAt(col, row) >= thr);
                if (havePrev) {
                    bool trans = d2l ? (!prevAbove && above) : (prevAbove && !above);
                    if (trans) {
                        // 에지 위치: 전/후 픽셀 중간
                        double ex = fwd ? (col - 0.5) : (col + 0.5);
                        sx+=ex; sy+=row; sxx+=ex*ex; syy+=(double)row*row; sxy+=ex*row; ++n;
                        break;
                    }
                }
                prevAbove=above; havePrev=true;
            }
        }
    } else {
        // 스캔 = 행(Y방향), 수직 = 열(X)
        for (int col = x0; col < x1; ++col) {
            bool havePrev=false, prevAbove=false;
            for (int dr=0; dr<(y1-y0); ++dr) {
                int row = fwd ? (y0+dr) : (y1-1-dr);
                bool valid = map.inBounds(col, row) && map.valid(col, row);
                bool above = valid && (map.rawAt(col, row) >= thr);
                if (havePrev) {
                    bool trans = d2l ? (!prevAbove && above) : (prevAbove && !above);
                    if (trans) {
                        double ey = fwd ? (row - 0.5) : (row + 0.5);
                        sx+=col; sy+=ey; sxx+=(double)col*col; syy+=ey*ey; sxy+=(double)col*ey; ++n;
                        break;
                    }
                }
                prevAbove=above; havePrev=true;
            }
        }
    }

    if (n < 2) return false;

    double mx=sx/n, my=sy/n;
    double cxx=sxx/n-mx*mx, cyy=syy/n-my*my, cxy=sxy/n-mx*my;
    double angleRad = 0.5 * std::atan2(2.0*cxy, cxx-cyy);
    const double dx=std::cos(angleRad), dy=std::sin(angleRad);
    // 라인 중심 = 검색 영역 중앙을 피팅 라인에 투영
    const double cx_roi=(x0+x1)*0.5, cy_roi=(y0+y1)*0.5;
    const double t=(cx_roi-mx)*dx+(cy_roi-my)*dy;
    outCx=mx+t*dx; outCy=my+t*dy;
    outAngleDeg=angleRad*180.0/3.14159265358979323846;
    outCount=static_cast<int>(n);
    return true;
}

ToolResult LineCenterTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "LineCenter: HeightMap 입력이 없습니다" };

    const auto& map = *input->inHeightMap(0);
    const auto region = input->inRegion(1);

    // 검색 영역: Region 바운딩박스 or 전체 이미지
    int x0, y0, x1, y1;
    if (region && !region->empty()) {
        auto bb = region->boundingBox();
        if (bb.w <= 0 || bb.h <= 0)
            return { ToolStatus::Fail, "LineCenter: Region이 비어있습니다" };
        x0=bb.x; y0=bb.y; x1=bb.x+bb.w; y1=bb.y+bb.h;
    } else {
        x0=0; y0=0; x1=map.width; y1=map.height;
    }

    double cx, cy, angleDeg;
    int count;
    if (!findLine(map, x0, y0, x1, y1, cx, cy, angleDeg, count))
        return { ToolStatus::Fail, "LineCenter: 에지를 찾지 못했습니다 (임계값/극성/방향을 확인하세요)" };

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;

    RefPoint rp;
    rp.cx=cx; rp.cy=cy;
    rp.cxMm=cx*map.xResMm; rp.cyMm=cy*map.yResMm;
    rp.angleDeg=angleDeg; rp.roiIndex=0; rp.valid=true;
    out->points.push_back(rp);

    Overlay ov;
    ov.kind=Overlay::Kind::Lines;
    Overlay::LineData ld;
    ld.cx=cx; ld.cy=cy; ld.cxMm=rp.cxMm; ld.cyMm=rp.cyMm;
    ld.angleDeg=angleDeg; ld.roiIndex=0; ld.pointCount=count;
    ov.lines.push_back(ld);
    out->overlays.push_back(std::move(ov));

    VISION_LOG_INFO("LineCenter: 라인 검색됨 cx={:.1f} cy={:.1f} angle={:.1f}deg edges={}", cx, cy, angleDeg, count);
    return {ToolStatus::Ok, "", out};
}

} // namespace vision
