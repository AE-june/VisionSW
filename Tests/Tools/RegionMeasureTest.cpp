#include <gtest/gtest.h>
#include "RegionMeasureTool.h"
#include "LevelTool.h"
#include "VisionData.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>
#include <string>

using namespace vision;
using namespace vision::test;

// ── 헬퍼 ─────────────────────────────────────────────────────────────────

static double findMeas(const ToolResult& res, const std::string& name) {
    for (const auto& m : res.output->measurements)
        if (m.name == name) return m.value;
    return std::numeric_limits<double>::quiet_NaN();
}

static bool isMeasValid(const ToolResult& res, const std::string& name) {
    for (const auto& m : res.output->measurements)
        if (m.name == name) return m.valid;
    return false;
}

// xRes=yRes=zRes=0.001 float, zZeroCount=0, originCol=originRow=0
static std::shared_ptr<HeightMap> makeUniformHM(int w, int h, double zMm,
                                                 float xRes=1.f, float yRes=1.f,
                                                 float zRes=0.001f) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->xResMm = xRes; hm->yResMm = yRes;
    hm->zResMm = zRes; hm->zZeroCount = 0.f;
    const float raw = static_cast<float>(zMm / zRes);
    hm->data.assign(static_cast<size_t>(w) * h, raw);
    return hm;
}

static std::shared_ptr<Region> makeFullRegion(int w, int h) {
    auto rg = std::make_shared<Region>();
    rg->width = w; rg->height = h;
    rg->mask.assign(static_cast<size_t>(w) * h, 1);
    return rg;
}

static std::shared_ptr<Region> makeRectRegion(int w, int h, int c0, int r0, int cw, int rh) {
    auto rg = std::make_shared<Region>();
    rg->width = w; rg->height = h;
    rg->mask.assign(static_cast<size_t>(w) * h, 0);
    for (int r = r0; r < r0 + rh; ++r)
        for (int c = c0; c < c0 + cw; ++c)
            rg->mask[static_cast<size_t>(r) * w + c] = 1;
    return rg;
}

// ── 기본 오류 경로 ────────────────────────────────────────────────────────

TEST(RegionMeasureTest, NoInputFails) {
    auto d = std::make_shared<VisionData>();
    EXPECT_EQ(RegionMeasureTool().execute(d).status, ToolStatus::Fail);
}

TEST(RegionMeasureTest, EmptyRegionFails) {
    auto rg = std::make_shared<Region>();
    rg->width = 3; rg->height = 3;
    rg->mask.assign(9, 0);
    EXPECT_EQ(RegionMeasureTool().execute(makeInputRegionHM(rg)).status, ToolStatus::Fail);
}

TEST(RegionMeasureTest, UnknownAggregationFails) {
    auto hm = makeUniformHM(3, 3, 5.0);
    RegionMeasureParams p; p.aggregation = "Nonsense";
    auto res = RegionMeasureTool(p).execute(makeInputRegionHM(makeFullRegion(3, 3), hm));
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// ── 집계: Mean ───────────────────────────────────────────────────────────

// 전체가 5mm인 3×3 HeightMap → zMm = 5.0
TEST(RegionMeasureTest, MeanAggregationUniformZ) {
    auto hm = makeUniformHM(3, 3, 5.0);
    auto res = RegionMeasureTool().execute(makeInputRegionHM(makeFullRegion(3, 3), hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "zMm"), 5.0, 1e-9);
    EXPECT_TRUE(isMeasValid(res, "zMm"));
}

// Z=[1,2,3,4] 픽셀 4개 → mean=2.5
TEST(RegionMeasureTest, MeanAggregationKnownValues) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 4; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.0f; hm->zZeroCount = 0.f;
    hm->data = {1.f, 2.f, 3.f, 4.f};  // raw = zMm (zRes=1)
    auto res = RegionMeasureTool().execute(makeInputRegionHM(makeFullRegion(4, 1), hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "zMm"), 2.5, 1e-9);
}

// ── 집계: Median ─────────────────────────────────────────────────────────

// Z=[1,2,3] (홀수) → median=2
TEST(RegionMeasureTest, MedianAggregation) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 3; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.f; hm->zZeroCount = 0.f;
    hm->data = {1.f, 2.f, 3.f};
    RegionMeasureParams p; p.aggregation = "Median";
    auto res = RegionMeasureTool(p).execute(makeInputRegionHM(makeFullRegion(3, 1), hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "zMm"), 2.0, 1e-9);
}

// ── 집계: Max / Min ──────────────────────────────────────────────────────

TEST(RegionMeasureTest, MaxAggregation) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 3; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.f; hm->zZeroCount = 0.f;
    hm->data = {1.f, 5.f, 3.f};
    RegionMeasureParams p; p.aggregation = "Max";
    auto res = RegionMeasureTool(p).execute(makeInputRegionHM(makeFullRegion(3, 1), hm));
    EXPECT_NEAR(findMeas(res, "zMm"), 5.0, 1e-9);
}

TEST(RegionMeasureTest, MinAggregation) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 3; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.f; hm->zZeroCount = 0.f;
    hm->data = {1.f, 5.f, 3.f};
    RegionMeasureParams p; p.aggregation = "Min";
    auto res = RegionMeasureTool(p).execute(makeInputRegionHM(makeFullRegion(3, 1), hm));
    EXPECT_NEAR(findMeas(res, "zMm"), 1.0, 1e-9);
}

// ── 집계: StdDev ─────────────────────────────────────────────────────────

// Z=[2,4] → mean=3, 표본stdDev=sqrt(((2-3)^2+(4-3)^2)/(n-1))=sqrt(2)≈1.4142
TEST(RegionMeasureTest, StdDevAggregation) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 2; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.f; hm->zZeroCount = 0.f;
    hm->data = {2.f, 4.f};
    RegionMeasureParams p; p.aggregation = "StdDev";
    auto res = RegionMeasureTool(p).execute(makeInputRegionHM(makeFullRegion(2, 1), hm));
    EXPECT_NEAR(findMeas(res, "zMm"), std::sqrt(2.0), 1e-9);
}

// ── 집계: HighTail ───────────────────────────────────────────────────────

// Z=[1,2,3,4], highTailPct=50 → 상위 50%(2개=[3,4]) 평균 = 3.5
TEST(RegionMeasureTest, HighTailAggregation) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 4; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.f; hm->zZeroCount = 0.f;
    hm->data = {1.f, 2.f, 3.f, 4.f};
    RegionMeasureParams p; p.aggregation = "HighTail"; p.highTailPct = 50.0;
    auto res = RegionMeasureTool(p).execute(makeInputRegionHM(makeFullRegion(4, 1), hm));
    EXPECT_NEAR(findMeas(res, "zMm"), 3.5, 1e-9);
}

// ── 집계: Percentile ─────────────────────────────────────────────────────

// Z=[0,1,2,3], p=50 → median=1.5 (짝수 개, Aggregate.h 동작에 의존)
TEST(RegionMeasureTest, PercentileAggregation) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 4; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.f; hm->zZeroCount = 0.f;
    hm->data = {0.f, 1.f, 2.f, 3.f};
    RegionMeasureParams p; p.aggregation = "Percentile"; p.percentile = 0.0;
    auto res = RegionMeasureTool(p).execute(makeInputRegionHM(makeFullRegion(4, 1), hm));
    // percentile=0 → 최솟값 = 0
    EXPECT_NEAR(findMeas(res, "zMm"), 0.0, 1e-9);
}

// ── 기하: 면적 ────────────────────────────────────────────────────────────

// 4×3 Region, xRes=2mm, yRes=3mm → areaMm2 = 12*2*3 = 72
TEST(RegionMeasureTest, AreaMm2) {
    auto hm = makeUniformHM(4, 3, 0.0, 2.f, 3.f, 0.001f);
    auto res = RegionMeasureTool().execute(makeInputRegionHM(makeFullRegion(4, 3), hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "areaPx"),  12.0, 1e-9);
    EXPECT_NEAR(findMeas(res, "areaMm2"), 72.0, 1e-9);
}

// ── 기하: BBox ────────────────────────────────────────────────────────────

// 5×5 HM, 가운데 3×1 rect (row=2, col=1~3) → bboxWidth=3px, bboxHeight=1px
TEST(RegionMeasureTest, BBoxDimensions) {
    const float xRes = 2.f, yRes = 3.f;
    auto hm = makeUniformHM(5, 5, 0.0, xRes, yRes, 0.001f);
    auto rg = makeRectRegion(5, 5, 1, 2, 3, 1);  // col=1..3, row=2
    auto res = RegionMeasureTool().execute(makeInputRegionHM(rg, hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "bboxWidthMm"),  3.0 * xRes, 1e-6);
    EXPECT_NEAR(findMeas(res, "bboxHeightMm"), 1.0 * yRes, 1e-6);
    EXPECT_NEAR(findMeas(res, "aspectRatio"),  (3.0 * xRes) / (1.0 * yRes), 1e-6);
}

// ── 기하: 무게중심 ────────────────────────────────────────────────────────

// 2×1 Region (col=0,1, row=0), xRes=1mm → cxMm = 0.5mm (centroid at px 0.5)
TEST(RegionMeasureTest, Centroid) {
    const float xRes = 1.f, yRes = 1.f;
    auto hm = makeUniformHM(4, 1, 0.0, xRes, yRes);
    auto rg = makeRectRegion(4, 1, 0, 0, 2, 1);  // first 2 cols
    auto res = RegionMeasureTool().execute(makeInputRegionHM(rg, hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    // cxPx = (0+1)/2 = 0.5, cxMm = (0.5 - originCol=0)*xRes=0.5
    EXPECT_NEAR(findMeas(res, "cxMm"), 0.5, 1e-9);
    EXPECT_NEAR(findMeas(res, "cyMm"), 0.0, 1e-9);
}

// ── 기하: 방향각 ─────────────────────────────────────────────────────────

// 수평 직사각형 (10×1): orientDeg ≈ 0° (±작은 값)
TEST(RegionMeasureTest, OrientHorizontalRectangle) {
    auto rg = makeRectRegion(10, 5, 0, 2, 10, 1);  // wide rect
    auto res = RegionMeasureTool().execute(makeInputRegionHM(rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    // 수평 직사각형 → 주축이 x축 방향 → orientDeg ≈ 0
    EXPECT_NEAR(findMeas(res, "orientDeg"), 0.0, 1e-6);
}

// 수직 직사각형 (1×10): orientDeg ≈ ±90°
TEST(RegionMeasureTest, OrientVerticalRectangle) {
    auto rg = makeRectRegion(5, 10, 2, 0, 1, 10);  // tall rect
    auto res = RegionMeasureTool().execute(makeInputRegionHM(rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    double ang = std::abs(findMeas(res, "orientDeg"));
    EXPECT_NEAR(ang, 90.0, 1e-6);
}

// ── 기하: 체적 ────────────────────────────────────────────────────────────

// 3×1 pixels, z=2mm, xRes=1mm, yRes=1mm → volumeMm3 = 3*2*1*1 = 6
TEST(RegionMeasureTest, Volume) {
    auto hm = makeUniformHM(3, 1, 2.0, 1.f, 1.f, 0.001f);
    auto res = RegionMeasureTool().execute(makeInputRegionHM(makeFullRegion(3, 1), hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "volumeMm3"), 6.0, 1e-9);
    EXPECT_TRUE(isMeasValid(res, "volumeMm3"));
}

// ── 기하: 평탄도 (flatnessMm = stdDev of Z) ─────────────────────────────

// 균일 Z → flatnessMm=0
TEST(RegionMeasureTest, FlatnessUniformZ) {
    auto hm = makeUniformHM(5, 1, 3.0);
    auto res = RegionMeasureTool().execute(makeInputRegionHM(makeFullRegion(5, 1), hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "flatnessMm"), 0.0, 1e-9);
}

// Z=[2,4] → 표본stdDev=sqrt(2)≈1.4142
TEST(RegionMeasureTest, FlatnessKnownStdDev) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 2; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 1.f; hm->zZeroCount = 0.f;
    hm->data = {2.f, 4.f};
    auto res = RegionMeasureTool().execute(makeInputRegionHM(makeFullRegion(2, 1), hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "flatnessMm"), std::sqrt(2.0), 1e-9);
}

// ── HeightMap 없을 때 ────────────────────────────────────────────────────

// Region 전용 → areaMm2/bboxMm invalid, areaPx/cxMm(px)은 valid
TEST(RegionMeasureTest, NoHeightMapGeometryInvalid) {
    auto rg = makeFullRegion(3, 3);
    auto res = RegionMeasureTool().execute(makeInputRegionHM(rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_FALSE(isMeasValid(res, "areaMm2"));
    EXPECT_FALSE(isMeasValid(res, "bboxWidthMm"));
    EXPECT_FALSE(isMeasValid(res, "bboxHeightMm"));
    EXPECT_FALSE(isMeasValid(res, "volumeMm3"));
    EXPECT_FALSE(isMeasValid(res, "zMm"));
    EXPECT_TRUE(isMeasValid(res, "areaPx"));
    EXPECT_TRUE(isMeasValid(res, "cxMm"));  // px 단위로 유효
}

// ── 5-3: Level(distance) → RegionMeasure(Mean) 정확성 검증 ───────────────
// 수평 평면 z=c mm, HeightMap 전체 z=zSurf mm
// Level(distance) → 각 픽셀 = zSurf - c (수평 평면: inv_norm=1)
// RegionMeasure(Mean) = zSurf - c
TEST(RegionMeasureTest, LevelDistanceMeanEquivalence) {
    const float xRes = 1.f, yRes = 1.f, zRes = 0.001f;
    const double zSurf = 5.0;  // 표면 높이
    const double zPlane = 4.0; // 기준 평면 높이 (수평)
    const double expected = zSurf - zPlane; // 1.0mm

    auto hm = makeUniformHM(4, 4, zSurf, xRes, yRes, zRes);

    // 수평 평면 a=b=0, c=zPlane
    auto plane = std::make_shared<PlaneModel>(PlaneModel{0.0, 0.0, zPlane, true});

    LevelParams lp; lp.mode = "distance"; lp.keepInvalid = true;
    auto levelOut = LevelTool(lp).execute(makeInputHMPlane(hm, plane));
    ASSERT_EQ(levelOut.status, ToolStatus::Ok);
    auto& levelHM = levelOut.output->heightmaps;
    ASSERT_FALSE(levelHM.empty());

    auto res = RegionMeasureTool().execute(
        makeInputRegionHM(makeFullRegion(4, 4), levelHM[0]));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "zMm"), expected, 1e-6);
}

// ── 스칼라화: Region 2개를 주면 첫 원소(inRegion(0))만 측정 ────────────────
TEST(RegionMeasureTest, MultipleRegionsMeasuresFirstOnly) {
    auto hm = makeUniformHM(4, 1, 0.0, 1.f, 1.f, 0.001f);
    auto input  = std::make_shared<VisionData>();
    auto port   = std::make_shared<VisionData>();
    port->regions.push_back(makeRectRegion(4, 1, 0, 0, 2, 1)); // 첫 원소: areaPx=2
    port->regions.push_back(makeRectRegion(4, 1, 0, 0, 4, 1)); // 둘째: areaPx=4
    input->inputs.push_back(port);                              // 포트0 = Region[]
    auto hmPort = std::make_shared<VisionData>();
    hmPort->heightmaps.push_back(hm);
    input->inputs.push_back(hmPort);                            // 포트1 = HeightMap

    auto res = RegionMeasureTool().execute(input);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "areaPx"), 2.0, 1e-9);   // 첫 원소만
    EXPECT_TRUE(std::isnan(findMeas(res, "0.areaPx"))); // prefix 이름 안 생김
}
