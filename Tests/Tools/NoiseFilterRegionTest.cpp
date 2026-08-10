#include <gtest/gtest.h>
#include "NoiseFilter.h"
#include "VisionData.h"
#include "Region.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

static std::shared_ptr<HeightMap> makeConstHM(int w, int h, float raw) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->data.assign(static_cast<size_t>(w) * h, raw);
    return hm;
}

static std::shared_ptr<Region> makeRectRegion(int w, int h, int rx0, int ry0, int rx1, int ry1) {
    auto rg = std::make_shared<Region>();
    rg->width = w; rg->height = h;
    rg->mask.assign(static_cast<size_t>(w) * h, 0);
    for (int r = ry0; r < ry1; ++r)
        for (int c = rx0; c < rx1; ++c)
            rg->mask[static_cast<size_t>(r) * w + c] = 1;
    return rg;
}

// Region 없음 → 전체 이미지에 Mean 필터 (상수 → 변화 없음)
TEST(NoiseFilterRegionTest, NoRegionFiltersWhole) {
    auto hm = makeConstHM(16, 16, 1000.f);
    NoiseFilter::Params p; p.type = NoiseFilter::Type::Mean;
    NoiseFilter tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& out = *res.output->heightmap0();
    for (size_t i = 0; i < out.data.size(); ++i)
        EXPECT_NEAR(out.data[i], 1000.f, 1.f) << "i=" << i;
}

// Region 지정 → 해당 영역 내 픽셀 변경됨, Region 밖은 원본 그대로
TEST(NoiseFilterRegionTest, RegionOnlyPixelsChanged) {
    const int W = 20, H = 20;
    // 홀수 픽셀에 노이즈 삽입 (Region 밖까지)
    auto hm = makeConstHM(W, H, 5000.f);
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    // Region: 행 5~14, 열 5~14
    auto rg = makeRectRegion(W, H, 5, 5, 15, 15);

    // Region 안 임의 픽셀에 큰 스파이크 (SOR로 제거)
    hm->data[6 * W + 6] = 99999.f;

    NoiseFilter::Params p;
    p.type = NoiseFilter::Type::Mean;
    p.kernelSizeX = 3; p.kernelSizeY = 3;
    NoiseFilter tool(p);
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    const auto& out = *res.output->heightmap0();

    // Region 밖 첫 번째 픽셀(row=0, col=0)은 원본과 동일
    EXPECT_NEAR(out.data[0], 5000.f, 1.f);
    // Region 안은 Mean 필터 적용 → 스파이크 희석됨
    float spike_area = out.data[6 * W + 6];
    EXPECT_LT(spike_area, 99999.f);   // 완화됨
}

// Region 전체 0 → 아무 픽셀도 안 바뀜 (원본 그대로)
TEST(NoiseFilterRegionTest, EmptyRegionLeavesImageUnchanged) {
    auto hm = makeConstHM(10, 10, 2000.f);
    hm->data[5] = 88888.f;   // 스파이크
    auto rg = std::make_shared<Region>();
    rg->width = 10; rg->height = 10;
    rg->mask.assign(100, 0);   // 전부 0

    NoiseFilter::Params p; p.type = NoiseFilter::Type::Mean;
    NoiseFilter tool(p);
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    // 스파이크가 그대로여야 함
    EXPECT_NEAR(res.output->heightmap0()->data[5], 88888.f, 1.f);
}

// HeightMap 없음 → Skip
TEST(NoiseFilterRegionTest, NoHeightMapSkips) {
    auto d = std::make_shared<VisionData>();
    NoiseFilter tool;
    auto res = tool.execute(d);
    EXPECT_EQ(res.status, ToolStatus::Skip);
}
