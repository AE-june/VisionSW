#include <gtest/gtest.h>
#include "SurfaceCropTool.h"
#include "VisionData.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

// 4×4 HeightMap: 픽셀 (c,r)의 raw = r*4 + c (0-based, ch0)
static HeightMapPtr makeGrid(int w, int h, float xRes = 1.f, float yRes = 1.f,
                              float zRes = 0.001f, float zZero = 0.f,
                              float originCol = 0.f, float originRow = 0.f,
                              std::string frameId = "") {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->xResMm = xRes; hm->yResMm = yRes;
    hm->zResMm = zRes; hm->zZeroCount = zZero;
    hm->originCol = originCol; hm->originRow = originRow;
    hm->frameId = frameId;
    hm->data.resize(static_cast<size_t>(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            hm->data[r * w + c] = static_cast<float>(r * w + c);
    return hm;
}

// ── mode=rect ─────────────────────────────────────────────────────────

// 정상 크롭: 4×4에서 (1,1) 2×2 잘라내기
TEST(SurfaceCropTest, RectCropBasic) {
    auto hm = makeGrid(4, 4);
    SurfaceCropParams p; p.mode = "rect"; p.rect_x=1; p.rect_y=1; p.rect_w=2; p.rect_h=2;
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    EXPECT_EQ(out.width, 2);
    EXPECT_EQ(out.height, 2);
    // 원본 픽셀 (1,1)=5, (2,1)=6, (1,2)=9, (2,2)=10
    EXPECT_FLOAT_EQ(out.data[0], 5.f);   // (0,0) 출력 = (1,1) 원본
    EXPECT_FLOAT_EQ(out.data[1], 6.f);
    EXPECT_FLOAT_EQ(out.data[2], 9.f);
    EXPECT_FLOAT_EQ(out.data[3], 10.f);
}

// originCol 보존: 크롭 후 같은 물리점이 같은 mm를 가져야 함
TEST(SurfaceCropTest, OriginColPreservesMm) {
    // xRes=0.5mm, originCol=2.f (x=0mm at col=2)
    auto hm = makeGrid(6, 4, 0.5f, 1.f, 0.001f, 0.f, 2.f, 0.f);
    SurfaceCropParams p; p.mode = "rect"; p.rect_x=1; p.rect_y=0; p.rect_w=4; p.rect_h=4;
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    // 원본 col=3: xMm = (3 - 2) * 0.5 = 0.5mm
    // 출력 new_col=2 (= col3 - dx1): xMm = (2 - out.originCol) * 0.5
    // out.originCol = 2 - 1 = 1. xMm = (2 - 1) * 0.5 = 0.5mm ✓
    EXPECT_FLOAT_EQ(out.originCol, 2.f - 1.f);
    float xmm_in  = (3.f - 2.f) * 0.5f;
    float xmm_out = (2.f - out.originCol) * out.xResMm;
    EXPECT_NEAR(xmm_in, xmm_out, 1e-6f);
}

// w=0, h=0 → 전체 너비/높이
TEST(SurfaceCropTest, RectZeroWHMeansFullDimension) {
    auto hm = makeGrid(5, 3);
    SurfaceCropParams p; p.mode = "rect"; p.rect_x=1; p.rect_y=1; p.rect_w=0; p.rect_h=0;
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->heightmap0()->width,  4);   // 5 - 1
    EXPECT_EQ(res.output->heightmap0()->height, 2);   // 3 - 1
}

// rect 범위 초과 → Fail
TEST(SurfaceCropTest, RectOutOfBoundsFails) {
    auto hm = makeGrid(4, 4);
    SurfaceCropParams p; p.mode = "rect"; p.rect_x=3; p.rect_y=0; p.rect_w=2; p.rect_h=4;
    SurfaceCropTool tool(p);
    EXPECT_EQ(tool.execute(makeInputHM(hm)).status, ToolStatus::Fail);
}

// ── mode=region ────────────────────────────────────────────────────────

// Region 바운딩박스로 크롭 + outsideNaN
TEST(SurfaceCropTest, RegionCropWithOutsideNaN) {
    auto hm = makeGrid(4, 4);
    // Region: 4×4, 내부 픽셀 (1,1) (2,1) (1,2) (2,2) 만
    auto rg = std::make_shared<Region>(Region::makeEmpty(4, 4));
    rg->mask[1 * 4 + 1] = 1;
    rg->mask[1 * 4 + 2] = 1;
    rg->mask[2 * 4 + 1] = 1;
    rg->mask[2 * 4 + 2] = 1;

    SurfaceCropParams p; p.mode = "region"; p.outsideNaN = true;
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    // BBox = (1,1) 2×2
    EXPECT_EQ(out.width,  2);
    EXPECT_EQ(out.height, 2);
    // 모든 픽셀이 Region 내부 → NaN 없음
    for (float v : out.data) EXPECT_FALSE(std::isnan(v));
    // 값 확인
    EXPECT_FLOAT_EQ(out.data[0], 5.f);   // (1,1)
    EXPECT_FLOAT_EQ(out.data[3], 10.f);  // (2,2)
}

// outsideNaN=true이고 Region 밖 픽셀이 있으면 ch0 NaN
TEST(SurfaceCropTest, RegionOutsidePixelsNaN) {
    auto hm = makeGrid(4, 4);
    // BBox (0,0) 2×2, 하지만 내부 픽셀은 (0,0) 하나뿐
    auto rg = std::make_shared<Region>(Region::makeEmpty(4, 4));
    rg->mask[0 * 4 + 0] = 1;   // (0,0)만

    SurfaceCropParams p; p.mode = "region"; p.outsideNaN = true;
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    // BBox = (0,0) 1×1, 내부 픽셀만 → NaN 없음
    EXPECT_EQ(res.output->heightmap0()->width, 1);
    EXPECT_FALSE(std::isnan(res.output->heightmap0()->data[0]));
}

// Region 비어있음 → Fail
TEST(SurfaceCropTest, EmptyRegionFails) {
    auto hm = makeGrid(4, 4);
    auto rg = std::make_shared<Region>(Region::makeEmpty(4, 4));  // all zeros
    SurfaceCropParams p; p.mode = "region";
    SurfaceCropTool tool(p);
    EXPECT_EQ(tool.execute(makeInputHMRegion(hm, rg)).status, ToolStatus::Fail);
}

// mode=region이지만 Region 미제공 → Fail
TEST(SurfaceCropTest, RegionModeNoRegionFails) {
    auto hm = makeGrid(4, 4);
    SurfaceCropParams p; p.mode = "region";
    SurfaceCropTool tool(p);
    EXPECT_EQ(tool.execute(makeInputHM(hm)).status, ToolStatus::Fail);
}

// ── 프레임·일반 ─────────────────────────────────────────────────────────

// nodeId 있으면 frameId = "hm:<nodeId>"
TEST(SurfaceCropTest, FrameIdSetFromNodeId) {
    auto hm = makeGrid(4, 4, 1.f, 1.f, 0.001f, 0.f, 0.f, 0.f, "cam1");
    SurfaceCropParams p; p.mode = "rect"; p.rect_w = 2; p.rect_h = 2;
    p.nodeId = "node-42";
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->heightmap0()->frameId, "hm:node-42");
    // definedFrames에 추가됨
    ASSERT_EQ(res.output->definedFrames.size(), 1u);
    EXPECT_EQ(res.output->definedFrames[0].id,       "hm:node-42");
    EXPECT_EQ(res.output->definedFrames[0].parentId, "cam1");
    EXPECT_TRUE(res.output->definedFrames[0].toParent.isIdentity());
}

// nodeId 없으면 기존 frameId 유지
TEST(SurfaceCropTest, NoNodeIdKeepsInputFrameId) {
    auto hm = makeGrid(4, 4, 1.f, 1.f, 0.001f, 0.f, 0.f, 0.f, "cam1");
    SurfaceCropParams p; p.mode = "rect"; p.rect_w = 2; p.rect_h = 2;
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->heightmap0()->frameId, "cam1");
    EXPECT_TRUE(res.output->definedFrames.empty());
}

// HeightMap 없음 → Fail
TEST(SurfaceCropTest, NoHeightMapFails) {
    SurfaceCropTool tool;
    EXPECT_EQ(tool.execute(std::make_shared<VisionData>()).status, ToolStatus::Fail);
}

// 다채널: 모든 채널 슬라이스됨
TEST(SurfaceCropTest, MultiChannelSliced) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 3; hm->height = 3; hm->channels = 2;
    hm->xResMm = 1.f; hm->yResMm = 1.f; hm->zResMm = 0.001f;
    hm->data.resize(2 * 9);
    for (int i = 0; i < 9; ++i) {
        hm->data[i]     = static_cast<float>(i);        // ch0
        hm->data[9 + i] = static_cast<float>(i + 100); // ch1
    }
    SurfaceCropParams p; p.mode = "rect"; p.rect_x=1; p.rect_y=1; p.rect_w=2; p.rect_h=2;
    SurfaceCropTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    EXPECT_EQ(out.channels, 2);
    // ch0: (1,1)=4, (2,1)=5, (1,2)=7, (2,2)=8
    EXPECT_FLOAT_EQ(out.data[0], 4.f);
    EXPECT_FLOAT_EQ(out.data[3], 8.f);
    // ch1: +100
    EXPECT_FLOAT_EQ(out.data[4],  104.f);
    EXPECT_FLOAT_EQ(out.data[7],  108.f);
}
