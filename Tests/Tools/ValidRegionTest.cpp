#include <gtest/gtest.h>
#include "ValidRegionTool.h"
#include "VisionData.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

// 단순 HeightMap 생성 헬퍼
static std::shared_ptr<HeightMap> makeHM(int w, int h, float fillRaw = 100.f, std::string frameId = "") {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->frameId = frameId;
    hm->data.assign(static_cast<size_t>(w) * h, fillRaw);
    return hm;
}

// 유효 픽셀(non-NaN) → mask=1
TEST(ValidRegionTest, AllValidPixelsFillMask) {
    auto hm = makeHM(4, 4, 100.f);
    ValidRegionTool tool;
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto rg = res.output->region0();
    ASSERT_NE(rg, nullptr);
    EXPECT_EQ(rg->width, 4);
    EXPECT_EQ(rg->height, 4);
    for (uint8_t v : rg->mask) EXPECT_EQ(v, 1);
}

// NaN 픽셀 → mask=0
TEST(ValidRegionTest, NaNPixelsExcluded) {
    auto hm = makeHM(3, 3, 100.f);
    // 중앙 픽셀을 NaN으로
    hm->data[1 * 3 + 1] = std::numeric_limits<float>::quiet_NaN();
    ValidRegionTool tool;
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto rg = res.output->region0();
    EXPECT_EQ(rg->mask[1 * 3 + 1], 0);
    EXPECT_EQ(rg->mask[0], 1);
    EXPECT_EQ(rg->mask[8], 1);
}

// invert=true → 유효 픽셀이 0, NaN이 1
TEST(ValidRegionTest, InvertFlag) {
    auto hm = makeHM(2, 2, 100.f);
    hm->data[0] = std::numeric_limits<float>::quiet_NaN();
    ValidRegionParams p; p.invert = true;
    ValidRegionTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto rg = res.output->region0();
    EXPECT_EQ(rg->mask[0], 1);   // NaN → invert → 1
    EXPECT_EQ(rg->mask[1], 0);   // valid → invert → 0
}

// frameId 전파: 출력 Region::frameId = 입력 HeightMap::frameId
TEST(ValidRegionTest, FrameIdPropagated) {
    auto hm = makeHM(2, 2, 100.f, "cam1");
    ValidRegionTool tool;
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->region0()->frameId, "cam1");
}

// channel 선택: channel=1 의 NaN을 확인
TEST(ValidRegionTest, ChannelSelection) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 2; hm->height = 1; hm->channels = 2;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->data.resize(4, 100.f);
    // ch0 valid, ch1 NaN at pixel 0
    hm->data[0 * 2 + 0] = 100.f;  // ch0, px0
    hm->data[1 * 2 + 0] = std::numeric_limits<float>::quiet_NaN();  // ch1, px0
    hm->data[0 * 2 + 1] = 100.f;  // ch0, px1
    hm->data[1 * 2 + 1] = 100.f;  // ch1, px1

    ValidRegionParams p; p.channel = 1;
    ValidRegionTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto rg = res.output->region0();
    EXPECT_EQ(rg->mask[0], 0);   // ch1 NaN → excluded
    EXPECT_EQ(rg->mask[1], 1);   // ch1 valid
}

// 빈 입력 → Fail
TEST(ValidRegionTest, NullInputFails) {
    ValidRegionTool tool;
    auto res = tool.execute(nullptr);
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

TEST(ValidRegionTest, NoHeightMapFails) {
    auto d = std::make_shared<VisionData>();
    ValidRegionTool tool;
    auto res = tool.execute(d);
    EXPECT_EQ(res.status, ToolStatus::Fail);
}
