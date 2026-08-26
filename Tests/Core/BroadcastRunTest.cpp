#include <gtest/gtest.h>
#include "BroadcastRun.h"
#include "VisionData.h"

using namespace vision;

// 포트0에 Region N개를 담은 상류 입력을 만든다.
static VisionDataPtr makeRegionPortInput(int nRegions) {
    auto up = std::make_shared<VisionData>();
    for (int k = 0; k < nRegions; ++k) {
        auto rg = std::make_shared<Region>();
        rg->width = 1; rg->height = 1; rg->mask.assign(1, 1);
        rg->label = std::to_string(k);   // 원소 구분용
        up->regions.push_back(rg);
    }
    auto merged = std::make_shared<VisionData>();
    merged->inputs.push_back(up);        // 포트0 = up
    return merged;
}

TEST(BroadcastRunTest, ScalarPortWithArrayIsAxis) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", false } };  // 스칼라 선언
    auto lens = broadcastAxisLengths(*in, metas);
    ASSERT_EQ(lens.size(), 1u);
    EXPECT_EQ(lens[0], 3u);
}

TEST(BroadcastRunTest, ArrayDeclaredPortIsNotAxis) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", true } };   // 배열 선언 → 통째 소비
    auto lens = broadcastAxisLengths(*in, metas);
    EXPECT_TRUE(lens.empty());
}

TEST(BroadcastRunTest, ScalarSingleElementIsNotAxis) {
    auto in = makeRegionPortInput(1);
    std::vector<PortMeta> metas{ { "Region", false } };
    auto lens = broadcastAxisLengths(*in, metas);
    EXPECT_TRUE(lens.empty());
}

TEST(BroadcastRunTest, SliceKeepsOnlyElementI) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", false } };
    auto s1 = sliceBroadcastInput(*in, 1, metas);
    auto port0 = s1->in(0);
    ASSERT_TRUE(port0);
    ASSERT_EQ(port0->regions.size(), 1u);
    EXPECT_EQ(port0->regions[0]->label, "1");   // 원소 1만 남음
}

TEST(BroadcastRunTest, SliceLeavesArrayPortWhole) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", true } };  // 배열 선언
    auto s0 = sliceBroadcastInput(*in, 0, metas);
    ASSERT_TRUE(s0->in(0));
    EXPECT_EQ(s0->in(0)->regions.size(), 3u);           // 통째 유지
}
