#include <gtest/gtest.h>
#include "SurfaceResampleTool.h"
#include "VisionData.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

static HeightMapPtr makeGrid(int w, int h,
                              float xRes = 1.f, float yRes = 1.f,
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

// ── decimate (기본) ──────────────────────────────────────────────────────

// factor=2: 4×4 → 2×2, 원본 값 그대로
TEST(SurfaceResampleTest, DecimateFactor2Basic) {
    auto hm = makeGrid(4, 4);
    SurfaceResampleParams p; p.factor = 2;
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    EXPECT_EQ(out.width,  2);
    EXPECT_EQ(out.height, 2);
    // 원본 (0,0)=0, (2,0)=2, (0,2)=8, (2,2)=10
    EXPECT_FLOAT_EQ(out.data[0], 0.f);
    EXPECT_FLOAT_EQ(out.data[1], 2.f);
    EXPECT_FLOAT_EQ(out.data[2], 8.f);
    EXPECT_FLOAT_EQ(out.data[3], 10.f);
}

// decimate: NaN 원본 값 그대로 유지
TEST(SurfaceResampleTest, DecimateNaNPassthrough) {
    auto hm = makeGrid(4, 4);
    hm->data[0] = std::numeric_limits<float>::quiet_NaN();
    SurfaceResampleParams p; p.factor = 2;
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(std::isnan(res.output->heightmap0()->data[0]));
    EXPECT_FALSE(std::isnan(res.output->heightmap0()->data[3]));
}

// ── meanValid ───────────────────────────────────────────────────────────

// meanValid: 2×1 블록 평균
TEST(SurfaceResampleTest, MeanValidBasic) {
    // 1행 4열 → factor=2 → 1행 2열
    auto hm = makeGrid(4, 1);
    // data: 0 1 2 3
    SurfaceResampleParams p; p.factor = 2; p.method = "meanValid";
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    EXPECT_EQ(out.width, 2); EXPECT_EQ(out.height, 1);
    EXPECT_NEAR(out.data[0], 0.5f, 0.001f);   // mean(0,1)
    EXPECT_NEAR(out.data[1], 2.5f, 0.001f);   // mean(2,3)
}

// meanValid: NaN 픽셀 제외
TEST(SurfaceResampleTest, MeanValidExcludesNaN) {
    auto hm = makeGrid(2, 1);
    hm->data[0] = std::numeric_limits<float>::quiet_NaN();
    hm->data[1] = 10.f;
    SurfaceResampleParams p; p.factor = 2; p.method = "meanValid";
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(res.output->heightmap0()->data[0], 10.f, 0.001f);  // NaN 제외, 10만
}

// meanValid: 블록 전부 NaN → 출력 NaN
TEST(SurfaceResampleTest, MeanValidAllNaNBlockIsNaN) {
    auto hm = makeGrid(2, 1);
    hm->data[0] = hm->data[1] = std::numeric_limits<float>::quiet_NaN();
    SurfaceResampleParams p; p.factor = 2; p.method = "meanValid";
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(std::isnan(res.output->heightmap0()->data[0]));
}

// ── mm 좌표 보존 ─────────────────────────────────────────────────────────

// 리샘플 전후 같은 물리점의 mm 좌표 일치
TEST(SurfaceResampleTest, MmCoordinatePreserved) {
    // xRes=0.5mm, originCol=4.f
    auto hm = makeGrid(8, 4, 0.5f, 1.f, 0.001f, 0.f, 4.f, 0.f);
    SurfaceResampleParams p; p.factor = 2;
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();

    EXPECT_FLOAT_EQ(out.xResMm, 1.f);           // 0.5 * 2
    EXPECT_FLOAT_EQ(out.originCol, 2.f);         // 4 / 2

    // 원본 col=6: xMm = (6 - 4) * 0.5 = 1.0mm
    // 출력 col=3 (= 6/2): xMm = (3 - 2) * 1.0 = 1.0mm
    float xmm_in  = (6.f - 4.f) * 0.5f;
    float xmm_out = (3.f - out.originCol) * out.xResMm;
    EXPECT_NEAR(xmm_in, xmm_out, 1e-5f);
}

// ── mode=resolution ──────────────────────────────────────────────────────

TEST(SurfaceResampleTest, ModeResolution) {
    // xRes=0.5mm, target=1mm → factorX=2
    auto hm = makeGrid(6, 4, 0.5f, 0.5f);
    SurfaceResampleParams p;
    p.mode = "resolution"; p.targetXResMm = 1.f; p.targetYResMm = 1.f;
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->heightmap0()->width,  3);   // ceil(6/2)
    EXPECT_EQ(res.output->heightmap0()->height, 2);   // ceil(4/2)
    EXPECT_NEAR(res.output->heightmap0()->xResMm, 1.f, 0.001f);
}

// ── 프레임 ──────────────────────────────────────────────────────────────

TEST(SurfaceResampleTest, FrameIdSetFromNodeId) {
    auto hm = makeGrid(4, 4, 1.f, 1.f, 0.001f, 0.f, 0.f, 0.f, "cam1");
    SurfaceResampleParams p; p.factor = 2; p.nodeId = "node-99";
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->heightmap0()->frameId, "hm:node-99");
    ASSERT_EQ(res.output->definedFrames.size(), 1u);
    EXPECT_EQ(res.output->definedFrames[0].parentId, "cam1");
}

// ── 에러 ────────────────────────────────────────────────────────────────

TEST(SurfaceResampleTest, NoHeightMapFails) {
    SurfaceResampleTool tool;
    EXPECT_EQ(tool.execute(std::make_shared<VisionData>()).status, ToolStatus::Fail);
}

TEST(SurfaceResampleTest, InvalidFactorFails) {
    auto hm = makeGrid(4, 4);
    SurfaceResampleParams p; p.factor = 0;
    SurfaceResampleTool tool(p);
    EXPECT_EQ(tool.execute(makeInputHM(hm)).status, ToolStatus::Fail);
}

TEST(SurfaceResampleTest, InvalidResolutionFails) {
    auto hm = makeGrid(4, 4);
    SurfaceResampleParams p; p.mode = "resolution"; p.targetXResMm = 0.f; p.targetYResMm = 1.f;
    SurfaceResampleTool tool(p);
    EXPECT_EQ(tool.execute(makeInputHM(hm)).status, ToolStatus::Fail);
}

// factor=1: 동일 출력
TEST(SurfaceResampleTest, Factor1IsIdentity) {
    auto hm = makeGrid(3, 3);
    SurfaceResampleParams p; p.factor = 1;
    SurfaceResampleTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->heightmap0()->width,  3);
    EXPECT_EQ(res.output->heightmap0()->height, 3);
    for (int i = 0; i < 9; ++i)
        EXPECT_FLOAT_EQ(res.output->heightmap0()->data[i], hm->data[i]);
}
