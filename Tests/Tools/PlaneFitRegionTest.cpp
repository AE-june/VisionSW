#include <gtest/gtest.h>
#include "PlaneFitTool.h"
#include "VisionData.h"
#include "Region.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

// 평탄한 z=constMm 높이맵 생성 (zResMm=0.001, zZeroCount=0)
static std::shared_ptr<HeightMap> makeFlat(int w, int h, float constMm,
    float xResMm = 1.f, float yResMm = 1.f) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = xResMm; hm->yResMm = yResMm;
    float raw = constMm / 0.001f;
    hm->data.assign(static_cast<size_t>(w) * h, raw);
    return hm;
}

// 기울어진 평면: z = a*x + b*y + c (mm), x=col*xResMm, y=row*yResMm
static std::shared_ptr<HeightMap> makeTilted(int w, int h, double a, double b, double c,
    float xResMm = 0.5f, float yResMm = 0.5f) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = xResMm; hm->yResMm = yResMm;
    hm->data.resize(static_cast<size_t>(w) * h);
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) {
            double xmm = col * xResMm;
            double ymm = row * yResMm;
            double zmm = a * xmm + b * ymm + c;
            hm->data[static_cast<size_t>(row) * w + col] = static_cast<float>(zmm / 0.001f);
        }
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

// Region 없이 평탄 HM → planeC ≈ constMm
TEST(PlaneFitRegionTest, FlatHMNoRegion) {
    auto hm = makeFlat(20, 20, 5.0f);
    PlaneFitTool tool;
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    double c = 0;
    for (auto& m : res.output->measurements)
        if (m.name == "planeC") { c = m.value; break; }
    EXPECT_NEAR(c, 5.0, 1e-3);
}

// Region 있을 때 피팅이 Region 내 픽셀만 사용 → 올바른 평면 추출
TEST(PlaneFitRegionTest, TiltedHMWithRegion) {
    const double A = 0.01, B = 0.02, C = 3.0;
    auto hm = makeTilted(40, 40, A, B, C);
    // 전체 40×40이지만 좌상단 절반만 Region으로 지정
    auto rg = makeRectRegion(40, 40, 0, 0, 20, 20);
    PlaneFitTool tool;
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    double planeA = 0, planeB = 0, planeC = 0;
    for (auto& m : res.output->measurements) {
        if (m.name == "planeA") planeA = m.value;
        if (m.name == "planeB") planeB = m.value;
        if (m.name == "planeC") planeC = m.value;
    }
    EXPECT_NEAR(planeA, A, 1e-4);
    EXPECT_NEAR(planeB, B, 1e-4);
    EXPECT_NEAR(planeC, C, 1e-3);
}

// Region 없이 포인트 부족 시 Fail → 비어있지 않은 HM은 OK
TEST(PlaneFitRegionTest, NoRegionUsesAllPixels) {
    auto hm = makeFlat(10, 10, 2.0f);
    PlaneFitTool tool;
    auto res = tool.execute(makeInputHM(hm));
    EXPECT_EQ(res.status, ToolStatus::Ok);
    double cnt = 0;
    for (auto& m : res.output->measurements)
        if (m.name == "refPointCount") { cnt = m.value; break; }
    EXPECT_EQ(static_cast<int>(cnt), 100);   // 10*10 전체
}

// Region이 전부 0(비어있음) → 포인트 0개 → Fail
TEST(PlaneFitRegionTest, EmptyRegionFails) {
    auto hm = makeFlat(10, 10, 1.0f);
    auto rg = std::make_shared<Region>();
    rg->width = 10; rg->height = 10;
    rg->mask.assign(100, 0);   // 전부 0
    PlaneFitTool tool;
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// HeightMap 없음 → Fail
TEST(PlaneFitRegionTest, NoHeightMapFails) {
    auto d = std::make_shared<VisionData>();
    PlaneFitTool tool;
    auto res = tool.execute(d);
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// 출력에 plane0 존재
TEST(PlaneFitRegionTest, OutputHasPlane) {
    auto hm = makeFlat(10, 10, 3.0f);
    PlaneFitTool tool;
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_TRUE(res.output->plane0() != nullptr);
    EXPECT_TRUE(res.output->plane0()->valid);
}
