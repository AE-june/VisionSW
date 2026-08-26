#include "BroadcastRun.h"

namespace vision {

// 포트 데이터에서 type에 해당하는 iconic 벡터의 원소 수. 매핑 없는 타입은 0.
static std::size_t typedLen(const VisionData& d, const std::string& t) {
    if (t == "Region")       return d.regions.size();
    if (t == "Geometry")     return d.geometries.size();
    if (t == "Profile")      return d.profiles.size();
    if (t == "HeightMap")    return d.heightmaps.size();
    if (t == "PointCloud3D") return d.clouds.size();
    if (t == "Line")         return d.lines.size();
    return 0;   // Any/Measurements/Decisions 등은 첫 슬라이스에서 축 아님
}

// 포트 사본에서 type 벡터를 원소 i 하나만 남긴다.
static void sliceTypedTo(VisionData& d, const std::string& t, std::size_t i) {
    if      (t == "Region"       && i < d.regions.size())    { auto e = d.regions[i];    d.regions    = { e }; }
    else if (t == "Geometry"     && i < d.geometries.size()) { auto e = d.geometries[i]; d.geometries = { e }; }
    else if (t == "Profile"      && i < d.profiles.size())   { auto e = d.profiles[i];   d.profiles   = { e }; }
    else if (t == "HeightMap"    && i < d.heightmaps.size()) { auto e = d.heightmaps[i]; d.heightmaps = { e }; }
    else if (t == "PointCloud3D" && i < d.clouds.size())     { auto e = d.clouds[i];     d.clouds     = { e }; }
    else if (t == "Line"         && i < d.lines.size())      { auto e = d.lines[i];      d.lines      = { e }; }
}

std::vector<std::size_t> broadcastAxisLengths(
    const VisionData& in, const std::vector<PortMeta>& metas) {
    std::vector<std::size_t> lens;
    for (std::size_t p = 0; p < metas.size(); ++p) {
        if (metas[p].isArray) continue;
        auto port = in.in(p);
        if (!port) continue;
        std::size_t n = typedLen(*port, metas[p].type);
        if (n > 1) lens.push_back(n);
    }
    return lens;
}

VisionDataPtr sliceBroadcastInput(
    const VisionData& in, std::size_t i, const std::vector<PortMeta>& metas) {
    // inputs 벡터(shared_ptr들)는 얕은 복사; 축 포트만 깊은 복사 후 슬라이스.
    auto sliced = std::make_shared<VisionData>(in);
    for (std::size_t p = 0; p < metas.size(); ++p) {
        if (metas[p].isArray) continue;
        auto port = in.in(p);
        if (!port) continue;
        if (typedLen(*port, metas[p].type) <= 1) continue;
        auto pc = std::make_shared<VisionData>(*port);   // 포트 깊은 복사
        sliceTypedTo(*pc, metas[p].type, i);
        if (p < sliced->inputs.size()) sliced->inputs[p] = pc;
    }
    return sliced;
}

} // namespace vision
