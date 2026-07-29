#include "RegionMeasureTool.h"
#include <vector>

namespace vision {

ToolResult RegionMeasureTool::execute(VisionDataPtr input) {
    m_result = {};
    if (!input || !input->hasRegion())
        return { ToolStatus::Fail, "RegionMeasure: Region이 없습니다." };

    const Region& rg = *input->region;
    const HeightMap* map = input->hasHeightMap() ? input->heightmap.get() : nullptr;

    double sumC = 0, sumR = 0, sumZ = 0;
    size_t n = 0, nz = 0;
    for (int r = 0; r < rg.height; ++r)
        for (int c = 0; c < rg.width; ++c) {
            if (!rg.mask[static_cast<size_t>(r) * rg.width + c]) continue;
            sumC += c; sumR += r; ++n;
            if (map && map->inBounds(c, r) && map->valid(c, r)) { sumZ += map->zMm(c, r); ++nz; }
        }
    if (n == 0)
        return { ToolStatus::Fail, "RegionMeasure: 빈 Region입니다." };

    const double cxPx = sumC / n, cyPx = sumR / n;
    m_result.areaPx = static_cast<double>(n);
    if (map) {
        m_result.hasHeight = true;
        m_result.areaMm2 = static_cast<double>(n) * map->xResMm * map->yResMm;
        m_result.cxMm = (cxPx - map->originCol) * map->xResMm;
        m_result.cyMm = (cyPx - map->originRow) * map->yResMm;
        if (nz) m_result.meanZmm = sumZ / nz;
    } else {
        m_result.cxMm = cxPx;   // HeightMap 없으면 px
        m_result.cyMm = cyPx;
    }
    m_result.valid = true;

    // control 출력: heights 배열 (CsvWriter 소비)
    auto heights = std::make_shared<std::vector<double>>();
    heights->push_back(m_result.areaPx);
    if (m_result.hasHeight) heights->push_back(m_result.areaMm2);
    heights->push_back(m_result.cxMm);
    heights->push_back(m_result.cyMm);
    if (nz) heights->push_back(m_result.meanZmm);

    auto out = std::make_shared<VisionData>();
    out->heights  = heights;
    out->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
