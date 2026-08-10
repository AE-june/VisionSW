#include <gtest/gtest.h>
#include "CreateRoiTool.h"
#include "VisionData.h"
#include "Region.h"
#include "TestHelpers.h"

using namespace vision;
using namespace vision::test;

static std::shared_ptr<HeightMap> makeFlat(int w, int h, float res = 0.5f) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = res; hm->yResMm = res;
    hm->data.assign(static_cast<size_t>(w) * h, 0.f);
    return hm;
}

// 세로 라인(angle=90) 중심 (col=50,row=50) mm=(25,25), 길이 20mm.
static std::shared_ptr<LineModel> makeVerticalLine(float res = 0.5f) {
    auto lm = std::make_shared<LineModel>();
    lm->cx = 50; lm->cy = 50;
    lm->cxMm = 50 * res; lm->cyMm = 50 * res;   // (25,25)
    lm->angleDeg = 90.0;                          // 세로
    lm->lengthMm = 20.0;
    // 실제 끝점(mm): col50(=25mm) 세로, row30..70(=15..35mm) → 길이 20mm
    lm->p0xMm = 50 * res; lm->p0yMm = 30 * res;
    lm->p1xMm = 50 * res; lm->p1yMm = 70 * res;
    lm->valid = true;
    return lm;
}

TEST(CreateRoiBandTest, BothSidesProduceTwoRegions) {
    auto hm = makeFlat(100, 100);
    auto ln = makeVerticalLine();
    CreateRoiParams params;
    params.bandWidthMm  = 4.0f;
    params.bandOffsetMm = 5.0f;
    params.bandSide     = CreateRoiParams::BandSide::Both;
    params.bandLenMode  = CreateRoiParams::BandLen::Line;   // 길이 20mm
    CreateRoiTool tool(params);

    auto res = tool.execute(makeInputHMLine(hm, ln));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->regions.size(), 2u);
    EXPECT_EQ(res.output->regions[0]->label, "left");
    EXPECT_EQ(res.output->regions[1]->label, "right");

    // 세로 라인 → 법선 = X방향. left = -Y법선? n=(-dirY,dirX), dir(90)=(0,1) → n=(-1,0).
    //   left(sign +1): center_x = 25 + 5*(-1) = 20mm = col40.  right: col60.
    // 밴드 폭 4mm=8px(±4px). 길이 20mm=40px(±20px, row30..70).
    auto& L = *res.output->regions[0];
    auto& R = *res.output->regions[1];
    // 대표 픽셀 검사: left는 col40 근처 켜짐, col60 근처 꺼짐. right 반대.
    auto on = [](const Region& rg, int c, int r) {
        return rg.mask[static_cast<size_t>(r) * rg.width + c] != 0;
    };
    EXPECT_TRUE (on(L, 40, 50));
    EXPECT_FALSE(on(L, 60, 50));
    EXPECT_TRUE (on(R, 60, 50));
    EXPECT_FALSE(on(R, 40, 50));
    // 길이 밖(row 10)은 꺼짐
    EXPECT_FALSE(on(L, 40, 10));
}

TEST(CreateRoiBandTest, LeftOnly) {
    auto hm = makeFlat(100, 100);
    auto ln = makeVerticalLine();
    CreateRoiParams params;
    params.bandSide = CreateRoiParams::BandSide::Left;
    CreateRoiTool tool(params);
    auto res = tool.execute(makeInputHMLine(hm, ln));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->regions.size(), 1u);
    EXPECT_EQ(res.output->regions[0]->label, "left");
}

TEST(CreateRoiBandTest, FixedLength) {
    auto hm = makeFlat(100, 100);
    auto ln = makeVerticalLine();  // lengthMm=20 무시됨
    CreateRoiParams params;
    params.bandSide     = CreateRoiParams::BandSide::Left;
    params.bandLenMode  = CreateRoiParams::BandLen::Fixed;
    params.bandLengthMm = 5.0f;    // 훨씬 짧게
    params.bandWidthMm  = 4.0f;
    params.bandOffsetMm = 5.0f;
    CreateRoiTool tool(params);
    auto res = tool.execute(makeInputHMLine(hm, ln));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto& L = *res.output->regions[0];
    auto on = [](const Region& rg, int c, int r) {
        return rg.mask[static_cast<size_t>(r) * rg.width + c] != 0;
    };
    // 5mm=10px → row45..55만. row50 켜짐, row40 꺼짐.
    EXPECT_TRUE (on(L, 40, 50));
    EXPECT_FALSE(on(L, 40, 40));
}
