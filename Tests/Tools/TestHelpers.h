#pragma once
// Phase 2: 포트 기반 VisionData 입력 생성 헬퍼 (테스트 전용)
#include "VisionData.h"
#include <memory>

namespace vision::test {

// port 0 = HeightMap
inline VisionDataPtr makeInputHM(std::shared_ptr<HeightMap> hm) {
    auto port0 = std::make_shared<VisionData>();
    port0->setHeightMap(std::move(hm));
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(port0));
    return d;
}

// port 0 = HeightMap, port 1 = Plane  (Level, HeightMeasure)
inline VisionDataPtr makeInputHMPlane(
    std::shared_ptr<HeightMap> hm,
    std::shared_ptr<PlaneModel> pl)
{
    auto d = makeInputHM(std::move(hm));
    auto port1 = std::make_shared<VisionData>();
    port1->setPlane(std::move(pl));
    d->inputs.push_back(std::move(port1));
    return d;
}

// port 0 = HeightMap, port 1 = Region  (SurfaceCrop, ReduceDomain)
inline VisionDataPtr makeInputHMRegion(
    std::shared_ptr<HeightMap> hm,
    std::shared_ptr<Region> rg)
{
    auto d = makeInputHM(std::move(hm));
    auto port1 = std::make_shared<VisionData>();
    port1->setRegion(std::move(rg));
    d->inputs.push_back(std::move(port1));
    return d;
}

// port 0 = HeightMap, port 1 = Line  (CreateROI 밴드 모드)
inline VisionDataPtr makeInputHMLine(
    std::shared_ptr<HeightMap> hm,
    std::shared_ptr<LineModel> ln)
{
    auto d = makeInputHM(std::move(hm));
    auto port1 = std::make_shared<VisionData>();
    port1->setLine(std::move(ln));
    d->inputs.push_back(std::move(port1));
    return d;
}

// port 0 = Region, port 1 = HeightMap (선택)  (RegionMeasure)
inline VisionDataPtr makeInputRegionHM(
    std::shared_ptr<Region> rg,
    std::shared_ptr<HeightMap> hm = nullptr)
{
    auto port0 = std::make_shared<VisionData>();
    port0->setRegion(std::move(rg));
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(port0));
    if (hm) {
        auto port1 = std::make_shared<VisionData>();
        port1->setHeightMap(std::move(hm));
        d->inputs.push_back(std::move(port1));
    }
    return d;
}

} // namespace vision::test
