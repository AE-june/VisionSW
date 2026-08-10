#include "RegionMeasureTool.h"
#include "Aggregate.h"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vision {

RegionMeasureTool::RegionMeasureTool(RegionMeasureParams params)
    : m_params(std::move(params)) {}

// 단일 Region 측정 → Measurement 목록 반환. 이름에 prefix 붙임(빈 문자면 그냥 이름).
static std::vector<Measurement> measureOne(
    const Region& rg,
    const HeightMap* map,
    const RegionMeasureParams& p,
    const std::string& prefix)
{
    double sumC = 0, sumR = 0;
    double sumCC = 0, sumRR = 0, sumCR = 0;
    int minC = rg.width, maxC = 0, minR = rg.height, maxR = 0;
    std::vector<double> zVals;
    double sumZ = 0;
    size_t n = 0;

    for (int r = 0; r < rg.height; ++r) {
        for (int c = 0; c < rg.width; ++c) {
            if (!rg.mask[static_cast<size_t>(r) * rg.width + c]) continue;
            sumC  += c; sumR  += r;
            sumCC += static_cast<double>(c) * c;
            sumRR += static_cast<double>(r) * r;
            sumCR += static_cast<double>(c) * r;
            if (c < minC) minC = c;
            if (c > maxC) maxC = c;
            if (r < minR) minR = r;
            if (r > maxR) maxR = r;
            ++n;
            if (map && map->inBounds(c, r) && map->valid(c, r)) {
                double z = map->zMm(c, r);
                zVals.push_back(z);
                sumZ += z;
            }
        }
    }

    if (n == 0) return {};

    const double cxPx  = sumC / n;
    const double cyPx  = sumR / n;
    const bool   hasHM = (map != nullptr);
    const size_t nz    = zVals.size();

    const double areaPx  = static_cast<double>(n);
    const double areaMm2 = hasHM ? areaPx * map->xResMm * map->yResMm : 0;
    const double cxMm    = hasHM ? (cxPx - map->originCol) * map->xResMm : cxPx;
    const double cyMm    = hasHM ? (cyPx - map->originRow) * map->yResMm : cyPx;

    const double mcc = sumCC / n - cxPx * cxPx;
    const double mrr = sumRR / n - cyPx * cyPx;
    const double mcr = sumCR / n - cxPx * cyPx;
    const double orientDeg = 0.5 * std::atan2(2.0 * mcr, mcc - mrr) * (180.0 / M_PI);

    const double bboxWidthMm  = hasHM
        ? (maxC - minC + 1) * static_cast<double>(map->xResMm) : static_cast<double>(maxC - minC + 1);
    const double bboxHeightMm = hasHM
        ? (maxR - minR + 1) * static_cast<double>(map->yResMm) : static_cast<double>(maxR - minR + 1);
    const double aspectRatio  = (bboxHeightMm > 0) ? bboxWidthMm / bboxHeightMm : 0;

    agg::Result zRes;
    if (nz > 0) {
        const std::string& ag = p.aggregation;
        if      (ag == "Mean")       zRes = agg::mean      (zVals.data(), nz);
        else if (ag == "Median")     zRes = agg::median    (zVals.data(), nz);
        else if (ag == "Max")        zRes = agg::maxV      (zVals.data(), nz);
        else if (ag == "Min")        zRes = agg::minV      (zVals.data(), nz);
        else if (ag == "StdDev")     zRes = agg::stdDev    (zVals.data(), nz);
        else if (ag == "HighTail")   zRes = agg::highTail  (zVals.data(), nz, p.highTailPct);
        else if (ag == "Percentile") zRes = agg::percentile(zVals.data(), nz, p.percentile);
    }

    agg::Result flatRes;
    if (nz > 1) flatRes = agg::stdDev(zVals.data(), nz);

    const double volumeMm3 = (hasHM && nz > 0) ? sumZ * map->xResMm * map->yResMm : 0;

    auto name = [&](const char* key) -> std::string {
        return prefix.empty() ? key : (prefix + "." + key);
    };

    return {
        {name("areaPx"),       areaPx,                              "px",  true},
        {name("areaMm2"),      areaMm2,                             "mm2", hasHM},
        {name("cxMm"),         cxMm,                                "mm",  true},
        {name("cyMm"),         cyMm,                                "mm",  true},
        {name("bboxWidthMm"),  bboxWidthMm,                         "mm",  hasHM},
        {name("bboxHeightMm"), bboxHeightMm,                        "mm",  hasHM},
        {name("aspectRatio"),  aspectRatio,                         "",    hasHM},
        {name("orientDeg"),    orientDeg,                           "deg", n >= 2},
        {name("zMm"),          zRes.valid ? zRes.value : 0.0,       "mm",  nz > 0},
        {name("volumeMm3"),    volumeMm3,                           "mm3", hasHM && nz > 0},
        {name("flatnessMm"),   flatRes.valid ? flatRes.value : 0.0, "mm",  nz > 1},
    };
}

ToolResult RegionMeasureTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "RegionMeasure: 입력이 없습니다." };

    const auto& regions = input->inRegions(0);
    if (regions.empty())
        return { ToolStatus::Fail, "RegionMeasure: Region이 없습니다." };

    const HeightMap* map = input->inHeightMap(1) ? input->inHeightMap(1).get() : nullptr;
    const bool multiRegion = (regions.size() > 1);

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;

    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& rg = regions[i];
        if (!rg || rg->empty()) continue;

        // prefix: label 있으면 label, 없으면 단수면 "" 다수면 인덱스 문자열
        std::string prefix;
        if (multiRegion) {
            prefix = rg->label.empty() ? std::to_string(i) : rg->label;
        }

        auto ms = measureOne(*rg, map, m_params, prefix);
        if (ms.empty())
            return { ToolStatus::Fail, "RegionMeasure: 빈 Region입니다." };

        out->measurements.insert(out->measurements.end(), ms.begin(), ms.end());
    }

    if (out->measurements.empty())
        return { ToolStatus::Fail, "RegionMeasure: 유효한 Region이 없습니다." };

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
