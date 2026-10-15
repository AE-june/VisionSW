#include "RowStretchTool.h"
#include "Logger.h"
#include <algorithm>
#include <limits>
#include <vector>
#include <cmath>

namespace vision {

ToolResult RowStretchTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "RowStretch: HeightMap 입력이 필요합니다" };
    const auto& zm = *input->inHeightMap(0);
    const int w = zm.width, h = zm.height;
    if (w <= 0 || h <= 0) return { ToolStatus::Fail, "RowStretch: 빈 HeightMap" };
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const auto region = input->inRegion(1);

    std::vector<int> rowScale((size_t)h, 1);
    if (region && !region->empty()) {
        const int rh = region->height, rw = region->width;
        for (int r = 0; r < h && r < rh; ++r)
            for (int c = 0; c < w && c < rw; ++c)
                if (region->contains(c, r)) { rowScale[r] = m_scale; break; }
    } else {
        std::fill(rowScale.begin(), rowScale.end(), m_scale);
    }

    size_t outH = 0; for (int r = 0; r < h; ++r) outH += (size_t)rowScale[r];
    auto z = std::make_shared<HeightMap>();
    z->width=w; z->height=(int)outH;
    z->xResMm=zm.xResMm; z->yResMm=zm.yResMm;
    z->zResMm=zm.zResMm; z->zZeroCount=zm.zZeroCount;
    z->originCol=zm.originCol; z->originRow=zm.originRow;
    z->data.assign((size_t)outH*w, NaN);

    auto at=[&](int r, int c){ return zm.data[(size_t)r*w+c]; };
    size_t outRow=0;
    for (int r=0; r<h; ++r) {
        const int s=rowScale[r];
        for (int k=0; k<s; ++k) {
            float* dst=&z->data[outRow*w];
            if (k==0 || r+1>=h) {
                std::copy(&zm.data[(size_t)r*w], &zm.data[(size_t)r*w+w], dst);
            } else {
                const float t=(float)k/s;
                for (int c=0; c<w; ++c) {
                    float a=at(r,c), b=at(r+1,c);
                    if (!std::isnan(a)&&!std::isnan(b)) dst[c]=a*(1.f-t)+b*t;
                    else if (!std::isnan(a)) dst[c]=a;
                    else dst[c]=b;
                }
            }
            ++outRow;
        }
    }

    auto data=std::make_shared<VisionData>();
    data->setHeightMap(z);
    data->sourceId=input->sourceId;
    VISION_LOG_INFO("RowStretch: {}x{} → {}x{} (scale={})", w, h, w, (int)outH, m_scale);
    return { ToolStatus::Ok, "", data };
}

} // namespace vision
