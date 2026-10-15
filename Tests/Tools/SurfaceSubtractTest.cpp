#include <gtest/gtest.h>
#include "SurfaceSubtractTool.h"
#include "VisionData.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

static std::shared_ptr<HeightMap> makeHM(int w, int h,
    float fillMm, float zResMm = 0.001f, float zZeroCount = 0.f) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = zResMm; hm->zZeroCount = zZeroCount;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    // raw = mm / zResMm + zZeroCount
    float raw = fillMm / zResMm + zZeroCount;
    hm->data.assign(static_cast<size_t>(w) * h, raw);
    return hm;
}

// port 0=A, port 1=B
static VisionDataPtr makeInputAB(
    std::shared_ptr<HeightMap> a,
    std::shared_ptr<HeightMap> b)
{
    auto portA = std::make_shared<VisionData>();
    portA->setHeightMap(a);
    auto portB = std::make_shared<VisionData>();
    portB->setHeightMap(b);
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(portA);
    d->inputs.push_back(portB);
    return d;
}

// A(3mm) - B(1mm) = 2mm
TEST(SurfaceSubtractTest, BasicDiff) {
    auto a = makeHM(4, 4, 3.0f);
    auto b = makeHM(4, 4, 1.0f);
    SurfaceSubtractTool tool;
    auto res = tool.execute(makeInputAB(a, b));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& out = *res.output->heightmap0();
    // output: zResMm=1, zZeroCount=0 → raw == mm diff
    for (size_t i = 0; i < out.data.size(); ++i)
        EXPECT_NEAR(out.data[i], 2.0f, 1e-5f) << "i=" << i;
}

// absolute=true → |A-B|
TEST(SurfaceSubtractTest, AbsoluteMode) {
    auto a = makeHM(4, 4, 1.0f);
    auto b = makeHM(4, 4, 3.0f);
    SurfaceSubtractParams p; p.absolute = true;
    SurfaceSubtractTool tool(p);
    auto res = tool.execute(makeInputAB(a, b));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& out = *res.output->heightmap0();
    for (size_t i = 0; i < out.data.size(); ++i)
        EXPECT_NEAR(out.data[i], 2.0f, 1e-5f) << "i=" << i;
}

// NaN 전파(propagate 기본)
TEST(SurfaceSubtractTest, NaNPropagate) {
    auto a = makeHM(4, 4, 3.0f);
    auto b = makeHM(4, 4, 1.0f);
    a->data[0] = std::numeric_limits<float>::quiet_NaN();
    SurfaceSubtractTool tool;
    auto res = tool.execute(makeInputAB(a, b));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(std::isnan(res.output->heightmap0()->data[0]));
    EXPECT_NEAR(res.output->heightmap0()->data[1], 2.0f, 1e-5f);
}

// nanPolicy=zero → NaN 위치에 0 출력
TEST(SurfaceSubtractTest, NaNZeroPolicy) {
    auto a = makeHM(4, 4, 3.0f);
    auto b = makeHM(4, 4, 1.0f);
    a->data[0] = std::numeric_limits<float>::quiet_NaN();
    SurfaceSubtractParams p; p.nanPolicy = "zero";
    SurfaceSubtractTool tool(p);
    auto res = tool.execute(makeInputAB(a, b));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(res.output->heightmap0()->data[0], 0.0f, 1e-6f);
    EXPECT_NEAR(res.output->heightmap0()->data[1], 2.0f, 1e-5f);
}

// 크기 불일치 → Fail
TEST(SurfaceSubtractTest, SizeMismatchFails) {
    auto a = makeHM(4, 4, 3.0f);
    auto b = makeHM(5, 4, 1.0f);
    SurfaceSubtractTool tool;
    auto res = tool.execute(makeInputAB(a, b));
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// 포트 B 없음 → Fail
TEST(SurfaceSubtractTest, MissingPortBFails) {
    auto in = makeInputHM(makeHM(4, 4, 1.0f));
    SurfaceSubtractTool tool;
    auto res = tool.execute(in);
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// 출력 zResMm=1, zZeroCount=0 인코딩 확인
TEST(SurfaceSubtractTest, OutputEncoding) {
    auto a = makeHM(4, 4, 5.0f);
    auto b = makeHM(4, 4, 2.0f);
    SurfaceSubtractTool tool;
    auto res = tool.execute(makeInputAB(a, b));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& out = *res.output->heightmap0();
    EXPECT_NEAR(out.zResMm,     1.0f, 1e-7f);
    EXPECT_NEAR(out.zZeroCount, 0.0f, 1e-7f);
    EXPECT_NEAR(out.data[0],    3.0f, 1e-5f);  // raw == mm
}
