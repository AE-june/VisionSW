#include <gtest/gtest.h>
#include "ReduceDomainTool.h"
#include "VisionData.h"
#include "Region.h"
#include "TestHelpers.h"
#include <cmath>

using namespace vision;
using namespace vision::test;

static std::shared_ptr<HeightMap> makeFlat(int w, int h) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f; hm->xResMm = 0.5f; hm->yResMm = 0.5f;
    hm->data.assign(static_cast<size_t>(w) * h, 0.f);   // 전부 유효(0)
    return hm;
}

// port0=HeightMap, port1=Region 여러 개
static VisionDataPtr makeInputHMRegions(std::shared_ptr<HeightMap> hm,
                                        std::vector<std::shared_ptr<Region>> regs) {
    auto d = makeInputHM(std::move(hm));
    auto port1 = std::make_shared<VisionData>();
    port1->regions = std::move(regs);
    d->inputs.push_back(std::move(port1));
    return d;
}

static long countValid(const HeightMap& m) {
    long n = 0;
    for (int r = 0; r < m.height; ++r)
        for (int c = 0; c < m.width; ++c)
            if (m.valid(c, r)) ++n;
    return n;
}

TEST(ReduceDomainTest, ExcludesMultipleRegions) {
    // 20x20. 제외영역 2개: 행 0..4, 행 15..19 (각 5*20=100px). invert=true.
    auto hm = makeFlat(20, 20);
    auto top = std::make_shared<Region>(Region::makeEmpty(20, 20));
    auto bot = std::make_shared<Region>(Region::makeEmpty(20, 20));
    for (int r = 0; r < 5; ++r)  for (int c = 0; c < 20; ++c) top->mask[r * 20 + c] = 1;
    for (int r = 15; r < 20; ++r) for (int c = 0; c < 20; ++c) bot->mask[r * 20 + c] = 1;

    ReduceDomainTool tool({ true });   // invert = 제외
    auto res = tool.execute(makeInputHMRegions(hm, { top, bot }));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    const auto& out = *res.output->heightmap0();
    // 400 - 200(제외) = 200 유효
    EXPECT_EQ(countValid(out), 200);
    EXPECT_FALSE(out.valid(0, 0));     // 제외영역
    EXPECT_FALSE(out.valid(0, 19));    // 제외영역
    EXPECT_TRUE (out.valid(0, 10));    // 중간 유지
}

TEST(ReduceDomainTest, KeepInsideUnion) {
    // invert=false: union 안쪽만 유지. 위 두 영역 union = 200px 유지, 나머지 제거.
    auto hm = makeFlat(20, 20);
    auto top = std::make_shared<Region>(Region::makeEmpty(20, 20));
    auto bot = std::make_shared<Region>(Region::makeEmpty(20, 20));
    for (int r = 0; r < 5; ++r)  for (int c = 0; c < 20; ++c) top->mask[r * 20 + c] = 1;
    for (int r = 15; r < 20; ++r) for (int c = 0; c < 20; ++c) bot->mask[r * 20 + c] = 1;

    ReduceDomainTool tool({ false });  // 포함
    auto res = tool.execute(makeInputHMRegions(hm, { top, bot }));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(countValid(*res.output->heightmap0()), 200);
    EXPECT_TRUE (res.output->heightmap0()->valid(0, 0));
    EXPECT_FALSE(res.output->heightmap0()->valid(0, 10));
}
