// InspectionWorkflowTest.cpp
// 10대 검사 워크플로우 + 신규 툴 통합 테스트
// 합성 픽스처 기반, 알려진 정답 대비 수치 검증

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include "SyntheticFixtures.h"
#include "TestHelpers.h"
#include "VisionData.h"

// ── 신규 툴 ──
#include "CircleFitTool.h"
#include "ConnectedComponentsTool.h"
#include "CountRegionsTool.h"
#include "RegionBooleanTool.h"
#include "RegionMorphologyTool.h"
#include "RegionFilterTool.h"
#include "RegionSelectTool.h"
#include "HeightMapMathTool.h"
#include "GradientMapTool.h"
#include "ProfileSmoothTool.h"
#include "ScalarMathTool.h"
#include "GeometryMeasureTool.h"

// ── 기존 툴 ──
#include "PlaneFitTool.h"
#include "LevelTool.h"
#include "ThresholdTool.h"
#include "RegionMeasureTool.h"
#include "SurfaceSubtractTool.h"
#include "ExtractProfileTool.h"
#include "ProfileFeatureTool.h"
#include "ValidRegionTool.h"

using namespace vision;
using namespace vision::test;

namespace {

// 측정값 이름으로 값 찾기
double getMeas(const VisionDataPtr& vd, const std::string& name) {
    if (!vd) return std::numeric_limits<double>::quiet_NaN();
    for (const auto& m : vd->measurements)
        if (m.name == name && m.valid) return m.value;
    return std::numeric_limits<double>::quiet_NaN();
}

// Region 생성 헬퍼 (포트0=Region 배열)
VisionDataPtr makeInputRegions(std::vector<std::shared_ptr<Region>> regions) {
    auto port0 = std::make_shared<VisionData>();
    port0->regions = std::move(regions);
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(port0));
    return d;
}

// port0=HM, port1=Region[]
VisionDataPtr makeInputHMRegions(std::shared_ptr<HeightMap> hm,
                                  std::vector<std::shared_ptr<Region>> regions) {
    auto d = makeInputHM(std::move(hm));
    auto port1 = std::make_shared<VisionData>();
    port1->regions = std::move(regions);
    d->inputs.push_back(std::move(port1));
    return d;
}

// port0=Region[], port1=HM
VisionDataPtr makeInputRegionsHM(std::vector<std::shared_ptr<Region>> regions,
                                   std::shared_ptr<HeightMap> hm) {
    auto port0 = std::make_shared<VisionData>();
    port0->regions = std::move(regions);
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(port0));
    auto port1 = std::make_shared<VisionData>();
    port1->setHeightMap(std::move(hm));
    d->inputs.push_back(std::move(port1));
    return d;
}

// port0=HM A, port1=HM B
VisionDataPtr makeInputHM2(std::shared_ptr<HeightMap> a, std::shared_ptr<HeightMap> b) {
    auto d = makeInputHM(std::move(a));
    auto port1 = std::make_shared<VisionData>();
    port1->setHeightMap(std::move(b));
    d->inputs.push_back(std::move(port1));
    return d;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// 신규 툴 단위 테스트
// ═══════════════════════════════════════════════════════════════════════

// ── CircleFit ──────────────────────────────────────────────────────────
TEST(CircleFitTest, CircularRegionFitsCorrectly) {
    // 100x100 HeightMap, 중심(2.5mm,2.5mm), 반경 1.5mm
    const int W = 100, H = 100;
    const float xRes = 0.05f, yRes = 0.05f, zRes = 0.001f;
    const double cx = 2.5, cy = 2.5, r = 1.5;

    HeightMap hm = makeBlank(W, H, xRes, yRes, zRes);
    auto region = std::make_shared<Region>(Region::makeEmpty(W, H));
    for (int row = 0; row < H; ++row)
        for (int col = 0; col < W; ++col) {
            double dx = col * xRes - cx, dy = row * yRes - cy;
            if (dx*dx + dy*dy <= r*r)
                region->mask[row * W + col] = 1;
        }

    auto port0 = std::make_shared<VisionData>();
    port0->regions = {region};
    auto port1 = std::make_shared<VisionData>();
    port1->setHeightMap(std::make_shared<HeightMap>(hm));
    auto input = std::make_shared<VisionData>();
    input->inputs = {port0, port1};

    CircleFitTool tool;
    auto res = tool.execute(input);

    ASSERT_EQ(res.status, ToolStatus::Ok);
    double rMeas = getMeas(res.output, "radius");
    double dMeas = getMeas(res.output, "diameter");
    EXPECT_NEAR(rMeas, r, 0.05) << "반경 오차 5% 이내";
    EXPECT_NEAR(dMeas, 2*r, 0.1) << "직경 오차";
}

TEST(CircleFitTest, NoRegionFails) {
    auto input = std::make_shared<VisionData>();
    CircleFitTool tool;
    auto res = tool.execute(input);
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// ── CountRegions ────────────────────────────────────────────────────────
TEST(CountRegionsTest, CountsCorrectly) {
    auto r1 = std::make_shared<Region>(Region::makeEmpty(50, 50));
    auto r2 = std::make_shared<Region>(Region::makeEmpty(50, 50));
    auto r3 = std::make_shared<Region>(Region::makeEmpty(50, 50));
    r1->mask[0] = 1; r2->mask[1] = 1; r3->mask[2] = 1;

    CountRegionsParams p; p.outputName = "count";
    CountRegionsTool tool(p);
    auto res = tool.execute(makeInputRegions({r1, r2, r3}));

    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(getMeas(res.output, "count"), 3.0);
}

TEST(CountRegionsTest, EmptyArrayGivesZero) {
    CountRegionsTool tool({});
    auto res = tool.execute(makeInputRegions({}));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(getMeas(res.output, "count"), 0.0);
}

// ── RegionBoolean ────────────────────────────────────────────────────────
TEST(RegionBooleanTest, UnionCoversAll) {
    const int W = 10, H = 10;
    auto rA = std::make_shared<Region>(Region::makeEmpty(W, H));
    auto rB = std::make_shared<Region>(Region::makeEmpty(W, H));
    for (int i = 0; i < 5; ++i) rA->mask[i] = 1;
    for (int i = 5; i < 10; ++i) rB->mask[i] = 1;

    auto port0 = std::make_shared<VisionData>(); port0->regions = {rA};
    auto port1 = std::make_shared<VisionData>(); port1->regions = {rB};
    auto input = std::make_shared<VisionData>();
    input->inputs = {port0, port1};

    RegionBooleanParams p; p.op = "or";
    RegionBooleanTool tool(p);
    auto res = tool.execute(input);

    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_FALSE(res.output->regions.empty());
    int count = 0;
    for (auto v : res.output->regions[0]->mask) count += v;
    EXPECT_EQ(count, 10);
}

TEST(RegionBooleanTest, IntersectionEmpty) {
    const int W = 10, H = 10;
    auto rA = std::make_shared<Region>(Region::makeEmpty(W, H));
    auto rB = std::make_shared<Region>(Region::makeEmpty(W, H));
    for (int i = 0; i < 5; ++i) rA->mask[i] = 1;
    for (int i = 5; i < 10; ++i) rB->mask[i] = 1;

    auto port0 = std::make_shared<VisionData>(); port0->regions = {rA};
    auto port1 = std::make_shared<VisionData>(); port1->regions = {rB};
    auto input = std::make_shared<VisionData>();
    input->inputs = {port0, port1};

    RegionBooleanParams p; p.op = "and";
    RegionBooleanTool tool(p);
    auto res = tool.execute(input);

    ASSERT_EQ(res.status, ToolStatus::Ok);
    int count = 0;
    for (auto v : res.output->regions[0]->mask) count += v;
    EXPECT_EQ(count, 0);
}

// ── RegionMorphology ─────────────────────────────────────────────────────
TEST(RegionMorphologyTest, DilateIncreasesArea) {
    const int W = 50, H = 50;
    auto rg = std::make_shared<Region>(Region::makeEmpty(W, H));
    rg->mask[25 * W + 25] = 1; // 단일 픽셀

    RegionMorphologyParams p; p.op = "dilate"; p.radius = 3; p.shape = "rect";
    RegionMorphologyTool tool(p);
    auto res = tool.execute(makeInputRegions({rg}));

    ASSERT_EQ(res.status, ToolStatus::Ok);
    int count = 0;
    for (auto v : res.output->regions[0]->mask) count += v;
    EXPECT_GT(count, 1) << "팽창 후 픽셀 수 증가";
}

TEST(RegionMorphologyTest, ErodeRemovesSmallRegion) {
    const int W = 50, H = 50;
    auto rg = std::make_shared<Region>(Region::makeEmpty(W, H));
    // 3x3 블록
    for (int r = 24; r <= 26; ++r)
        for (int c = 24; c <= 26; ++c)
            rg->mask[r * W + c] = 1;

    RegionMorphologyParams p; p.op = "erode"; p.radius = 3; p.shape = "rect";
    RegionMorphologyTool tool(p);
    auto res = tool.execute(makeInputRegions({rg}));

    ASSERT_EQ(res.status, ToolStatus::Ok);
    int count = 0;
    for (auto v : res.output->regions[0]->mask) count += v;
    EXPECT_EQ(count, 0) << "작은 블록 침식 시 제거";
}

// ── HeightMapMath ─────────────────────────────────────────────────────────
TEST(HeightMapMathTest, AbsGivesPositive) {
    HeightMap hm = makeBlank(10, 10, 0.1f, 0.1f, 0.001f);
    // 절반은 음수(-1mm), 절반은 양수(+1mm)
    for (int i = 0; i < 50; ++i) hm.data[i] = -1000.f; // -1mm in raw
    for (int i = 50; i < 100; ++i) hm.data[i] = 1000.f;

    HeightMapMathParams p; p.op = "abs";
    HeightMapMathTool tool(p);
    auto res = tool.execute(makeInputHM(std::make_shared<HeightMap>(hm)));

    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto& out = res.output->heightmaps[0];
    for (int i = 0; i < 100; ++i)
        EXPECT_GE(out->data[i], 0.f) << "abs 후 모든 값 ≥ 0";
}

TEST(HeightMapMathTest, SubtractIdenticalGivesZero) {
    HeightMap hm = makeBlank(10, 10, 0.1f, 0.1f, 0.001f);
    for (int i = 0; i < 100; ++i) hm.data[i] = static_cast<float>(i);

    HeightMapMathParams p; p.op = "subtract";
    HeightMapMathTool tool(p);
    auto hmA = std::make_shared<HeightMap>(hm);
    auto hmB = std::make_shared<HeightMap>(hm);
    auto res = tool.execute(makeInputHM2(hmA, hmB));

    ASSERT_EQ(res.status, ToolStatus::Ok);
    for (int i = 0; i < 100; ++i)
        EXPECT_NEAR(res.output->heightmaps[0]->data[i], 0.f, 1e-4f);
}

// ── GradientMap ──────────────────────────────────────────────────────────
TEST(GradientMapTest, FlatSurfaceGradientNearZero) {
    HeightMap flat = makeBlank(50, 50, 0.1f, 0.1f, 0.001f);
    // 모든 픽셀 동일값
    for (auto& v : flat.data) v = 1000.f;

    GradientMapParams p; p.output_mode = "magnitude"; p.ksize = 3;
    GradientMapTool tool(p);
    auto res = tool.execute(makeInputHM(std::make_shared<HeightMap>(flat)));

    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto& out = *res.output->heightmaps[0];
    // 내부 픽셀 기울기 ≈ 0
    for (int r = 2; r < 48; ++r)
        for (int c = 2; c < 48; ++c)
            EXPECT_NEAR(out.zMm(c, r), 0.0, 1e-3) << "평탄 면 기울기≈0";
}

TEST(GradientMapTest, StepEdgeHasNonzeroGradient) {
    HeightMap hm = makeStep(100, 50, 50, 0.0, 1.0, 0.1f, 0.1f, 0.001f);

    GradientMapParams p; p.output_mode = "gx"; p.ksize = 3;
    GradientMapTool tool(p);
    auto res = tool.execute(makeInputHM(std::make_shared<HeightMap>(hm)));

    ASSERT_EQ(res.status, ToolStatus::Ok);
    auto& out = *res.output->heightmaps[0];
    // col=50 근처에서 X방향 기울기 큼
    double maxGx = 0;
    for (int r = 10; r < 40; ++r) {
        double v = std::abs(out.zMm(50, r));
        if (v > maxGx) maxGx = v;
    }
    EXPECT_GT(maxGx, 0.1) << "단차 엣지에서 기울기 크게 발생";
}

// ── ProfileSmooth ─────────────────────────────────────────────────────────
TEST(ProfileSmoothTest, SmoothedStdDevSmaller) {
    // 노이즈 있는 profile 생성
    HeightMap hm = makeBlank(200, 1, 0.1f, 0.1f, 0.001f);
    uint64_t rng = 42;
    for (int i = 0; i < 200; ++i)
        hm.data[i] = static_cast<float>(1000.0 + 50.0 * lcgGaussian(rng));

    ExtractProfileParams ep; ep.mode = "axisX"; ep.index = 0; ep.channel = 0;
    ExtractProfileTool ept(ep);
    auto eRes = ept.execute(makeInputHM(std::make_shared<HeightMap>(hm)));
    ASSERT_EQ(eRes.status, ToolStatus::Ok);

    auto profIn = eRes.output->profiles[0];
    // 실제 표준편차 계산 (mean 차감)
    double meanBefore = 0;
    for (double z : profIn->z) meanBefore += z;
    meanBefore /= profIn->z.size();
    double stdBefore = 0;
    for (double z : profIn->z) stdBefore += (z - meanBefore) * (z - meanBefore);
    stdBefore = std::sqrt(stdBefore / profIn->z.size());

    auto port0 = std::make_shared<VisionData>();
    port0->profiles = {profIn};
    auto smooth_in = std::make_shared<VisionData>();
    smooth_in->inputs = {port0};

    ProfileSmoothParams sp; sp.method = "gaussian"; sp.windowSize = 11; sp.sigma = 3.0;
    ProfileSmoothTool smt(sp);
    auto sRes = smt.execute(smooth_in);

    ASSERT_EQ(sRes.status, ToolStatus::Ok);
    auto profOut = sRes.output->profiles[0];
    double meanAfter = 0;
    for (double z : profOut->z) meanAfter += z;
    meanAfter /= profOut->z.size();
    double stdAfter = 0;
    for (double z : profOut->z) stdAfter += (z - meanAfter) * (z - meanAfter);
    stdAfter = std::sqrt(stdAfter / profOut->z.size());

    EXPECT_LT(stdAfter, stdBefore * 0.5) << "스무딩 후 분산 절반 이하";
}

// ═══════════════════════════════════════════════════════════════════════
// 워크플로우 통합 테스트
// ═══════════════════════════════════════════════════════════════════════

// ── 1. 단차 측정 ─────────────────────────────────────────────────────────
// HeightMap(step) → PlaneFit(ref) → Level → RegionMeasure.zMean = 단차
TEST(Workflow, StepHeight) {
    const double STEP = 0.5; // mm
    HeightMap hm = makeStep(200, 100, 100, 0.0, STEP, 0.05f, 0.05f, 0.001f);

    // 1. PlaneFit on left half (reference)
    auto refRegion = std::make_shared<Region>(Region::makeEmpty(200, 100));
    for (int r = 0; r < 100; ++r)
        for (int c = 0; c < 100; ++c)
            refRegion->mask[r * 200 + c] = 1;

    PlaneFitTool pfTool({});
    auto pfIn = makeInputHMRegions(std::make_shared<HeightMap>(hm), {refRegion});
    auto pfRes = pfTool.execute(pfIn);
    ASSERT_EQ(pfRes.status, ToolStatus::Ok);
    auto plane = pfRes.output->planes[0];

    // 2. Level
    LevelParams lp; lp.mode = "distance";
    LevelTool levelTool(lp);
    auto levelIn = makeInputHMPlane(std::make_shared<HeightMap>(hm), plane);
    auto levelRes = levelTool.execute(levelIn);
    ASSERT_EQ(levelRes.status, ToolStatus::Ok);

    // 3. RegionMeasure on right half (step surface)
    auto stepRegion = std::make_shared<Region>(Region::makeEmpty(200, 100));
    for (int r = 10; r < 90; ++r)
        for (int c = 110; c < 190; ++c)
            stepRegion->mask[r * 200 + c] = 1;

    auto leveledHM = levelRes.output->heightmaps[0];
    RegionMeasureParams rmp;
    RegionMeasureTool rmTool(rmp);
    auto rmIn = makeInputRegionsHM({stepRegion}, leveledHM);
    auto rmRes = rmTool.execute(rmIn);
    ASSERT_EQ(rmRes.status, ToolStatus::Ok);

    double zMean = getMeas(rmRes.output, "zMm");
    EXPECT_NEAR(zMean, STEP, 0.01) << "단차 0.5mm 정확도 ±10μm";
}

// ── 2. 평탄도 ────────────────────────────────────────────────────────────
// 노이즈 있는 평면 → PlaneFit.rmse = 평탄도
TEST(Workflow, Flatness) {
    HeightMap hm = makeTiltedPlane(100, 100, 0.001, 0.0005, 1.0);
    // 랜덤 노이즈 추가
    uint64_t rng = 99;
    const float noiseMm = 0.005f;
    for (auto& v : hm.data)
        v += static_cast<float>(noiseMm / hm.zResMm * lcgGaussian(rng));

    PlaneFitTool tool({});
    auto res = tool.execute(makeInputHM(std::make_shared<HeightMap>(hm)));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    double rmse = getMeas(res.output, "rmse");
    EXPECT_NEAR(rmse, noiseMm, noiseMm * 0.5) << "rmse가 노이즈 수준 반영";
}

// ── 3. 폭 측정 (RegionMeasure bbox) ─────────────────────────────────────
TEST(Workflow, WidthFromBbox) {
    // 40px 폭 직사각형 Region, xRes=0.05 → 폭 = 2.0mm
    const int W = 100, H = 50;
    const float xRes = 0.05f;
    auto region = std::make_shared<Region>(Region::makeEmpty(W, H));
    for (int r = 10; r < 40; ++r)
        for (int c = 20; c < 60; ++c) // 40px wide
            region->mask[r * W + c] = 1;

    HeightMap hm = makeBlank(W, H, xRes, 0.05f, 0.001f);
    RegionMeasureTool tool({});
    auto res = tool.execute(makeInputRegionsHM({region}, std::make_shared<HeightMap>(hm)));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    double width = getMeas(res.output, "bboxWidthMm");
    EXPECT_NEAR(width, 40 * xRes, xRes) << "폭 2.0mm ±1픽셀";
}

// ── 4. 홀 직경 (Threshold→CC→CircleFit) ─────────────────────────────────
TEST(Workflow, HoleDiameter) {
    const double CX = 2.5, CY = 2.5, R = 1.2;
    HeightMap hm = makeHole(100, 100, CX, CY, R, 0.0, 0.05f, 0.05f, 0.001f);

    // 1. Threshold: NaN 픽셀 = 홀 내부 → ValidRegion(invert)
    ValidRegionParams vp; vp.invert = true;
    ValidRegionTool vTool(vp);
    auto vRes = vTool.execute(makeInputHM(std::make_shared<HeightMap>(hm)));
    ASSERT_EQ(vRes.status, ToolStatus::Ok);

    // 2. ConnectedComponents
    ConnectedComponentsParams cp; cp.connectivity = 8; cp.minAreaPx = 10;
    ConnectedComponentsTool ccTool(cp);
    auto ccRes = ccTool.execute(makeInputRegions(vRes.output->regions));
    ASSERT_EQ(ccRes.status, ToolStatus::Ok);
    ASSERT_GE(ccRes.output->regions.size(), 1u);

    // 3. RegionSelect largest
    RegionSelectParams sp; sp.mode = "largest";
    RegionSelectTool selTool(sp);
    auto selRes = selTool.execute(makeInputRegions(ccRes.output->regions));
    ASSERT_EQ(selRes.status, ToolStatus::Ok);

    // 4. CircleFit
    auto port0 = std::make_shared<VisionData>();
    port0->regions = selRes.output->regions;
    auto port1 = std::make_shared<VisionData>();
    port1->setHeightMap(std::make_shared<HeightMap>(hm));
    auto cfIn = std::make_shared<VisionData>();
    cfIn->inputs = {port0, port1};

    CircleFitTool cfTool;
    auto cfRes = cfTool.execute(cfIn);
    ASSERT_EQ(cfRes.status, ToolStatus::Ok);

    double measR = getMeas(cfRes.output, "radius");
    EXPECT_NEAR(measR, R, 0.05) << "홀 반경 1.2mm ±0.05mm";
}

// ── 5. 결함 검출 (SurfaceSubtract→HeightMapMath abs→Threshold→CountRegions) ──
TEST(Workflow, DefectDetection) {
    // 기준 면 (평탄)
    HeightMap refHM = makeBlank(300, 50, 0.05f, 0.05f, 0.001f);

    // 검사 면 (돌기 3개, 간격 충분히 벌려서 CC로 분리 가능하도록)
    HeightMap inspHM = makeBumpGrid(300, 50, 3, 1, 0.3, 0.2, 0.05f, 0.05f, 0.001f);

    // 1. SurfaceSubtract
    SurfaceSubtractParams ssp; ssp.absolute = true;
    SurfaceSubtractTool ssTool(ssp);
    auto ssRes = ssTool.execute(makeInputHM2(
        std::make_shared<HeightMap>(inspHM),
        std::make_shared<HeightMap>(refHM)));
    ASSERT_EQ(ssRes.status, ToolStatus::Ok);

    // 2. Threshold on difference map (>0.05mm)
    ThresholdParams tp; tp.thresholdMm = 0.08; tp.keepAbove = true;
    ThresholdTool tTool(tp);
    auto tRes = tTool.execute(makeInputHM(ssRes.output->heightmaps[0]));
    ASSERT_EQ(tRes.status, ToolStatus::Ok);

    // 3. ConnectedComponents
    ConnectedComponentsParams cp; cp.minAreaPx = 5;
    ConnectedComponentsTool ccTool(cp);
    auto ccRes = ccTool.execute(makeInputRegions(tRes.output->regions));
    ASSERT_EQ(ccRes.status, ToolStatus::Ok);

    // 4. CountRegions
    CountRegionsParams crp; crp.outputName = "defectCount";
    CountRegionsTool crTool(crp);
    auto crRes = crTool.execute(makeInputRegions(ccRes.output->regions));
    ASSERT_EQ(crRes.status, ToolStatus::Ok);

    double cnt = getMeas(crRes.output, "defectCount");
    EXPECT_NEAR(cnt, 3.0, 1.0) << "돌기 3개 검출 (±1 허용)";
}

// ── 6. 단면 형상 (ExtractProfile→ProfileFeature) ──────────────────────────
TEST(Workflow, ProfileInspection) {
    // 계단형 HeightMap → 단면 추출 → 엣지 위치 검출
    HeightMap hm = makeStep(200, 50, 100, 0.0, 0.5, 0.05f, 0.05f, 0.001f);

    // ExtractProfile axisX (행 25)
    ExtractProfileParams ep; ep.mode = "axisX"; ep.index = 25; ep.channel = 0;
    ExtractProfileTool epTool(ep);
    auto epRes = epTool.execute(makeInputHM(std::make_shared<HeightMap>(hm)));
    ASSERT_EQ(epRes.status, ToolStatus::Ok);
    ASSERT_FALSE(epRes.output->profiles.empty());

    // ProfileSmooth 먼저
    auto port0 = std::make_shared<VisionData>();
    port0->profiles = epRes.output->profiles;
    auto smIn = std::make_shared<VisionData>();
    smIn->inputs = {port0};
    ProfileSmoothParams sp; sp.method = "gaussian"; sp.windowSize = 5; sp.sigma = 1.0;
    ProfileSmoothTool smTool(sp);
    auto smRes = smTool.execute(smIn);

    // ProfileFeature edge
    auto port1 = std::make_shared<VisionData>();
    port1->profiles = smRes.output->profiles;
    auto pfIn = std::make_shared<VisionData>();
    pfIn->inputs = {port1};
    ProfileFeatureParams pfp;
    pfp.kind = "edge";
    pfp.edgeThresholdMm = 0.1;
    pfp.edgeDir = "rising";
    pfp.smoothWindow = 3;
    ProfileFeatureTool pfTool(pfp);
    auto pfRes = pfTool.execute(pfIn);
    ASSERT_EQ(pfRes.status, ToolStatus::Ok);

    // 엣지 위치: s = 100 * 0.05mm = 5.0mm 근처
    // getMeas key는 첫 번째 measurement
    bool foundEdge = false;
    for (const auto& m : pfRes.output->measurements) {
        if (m.name == "edge_s" && m.valid) {
            EXPECT_NEAR(m.value, 5.0, 0.3) << "엣지 위치 5mm ±0.3mm";
            foundEdge = true;
        }
    }
    EXPECT_TRUE(foundEdge) << "엣지 검출됨";
}

// ── 7. 면 각도 (PlaneFit×2→GeometryMeasure planeAngle) ───────────────────
TEST(Workflow, SurfaceAngle) {
    // 평면A: 수평 (a=0, b=0, c=0)
    // 평면B: 10도 기울어짐 → tan(10°) ≈ 0.1763
    const double tiltRad = 10.0 * 3.14159265358979323846 / 180.0;
    const double a2 = std::tan(tiltRad);

    HeightMap hmA = makeTiltedPlane(100, 100, 0.0, 0.0, 0.0);
    HeightMap hmB = makeTiltedPlane(100, 100, a2, 0.0, 0.0);

    PlaneFitTool pfTool({});
    auto resA = pfTool.execute(makeInputHM(std::make_shared<HeightMap>(hmA)));
    auto resB = pfTool.execute(makeInputHM(std::make_shared<HeightMap>(hmB)));
    ASSERT_EQ(resA.status, ToolStatus::Ok);
    ASSERT_EQ(resB.status, ToolStatus::Ok);

    GeometryMeasureParams gp; gp.kind = "planeAngle";
    GeometryMeasureTool gTool(gp);
    auto port0 = std::make_shared<VisionData>();
    port0->planes = resA.output->planes;
    auto port1 = std::make_shared<VisionData>();
    port1->planes = resB.output->planes;
    auto gmIn = std::make_shared<VisionData>();
    gmIn->inputs = {port0, port1};
    auto gmRes = gTool.execute(gmIn);

    ASSERT_EQ(gmRes.status, ToolStatus::Ok);
    double angle = getMeas(gmRes.output, "planeAngle");
    EXPECT_NEAR(angle, 10.0, 0.5) << "면 각도 10도 ±0.5도";
}

// ── 8. 체적 (RegionMeasure.volumeMm3) ─────────────────────────────────────
TEST(Workflow, VolumeOfBump) {
    // 가우시안 범프 1개, 높이 0.5mm, σ=1mm
    const double BH = 0.5, BR = 1.0;
    HeightMap hm = makeBumpGrid(200, 200, 1, 1, BH, BR, 0.05f, 0.05f, 0.001f);

    // Region: 전체
    auto region = std::make_shared<Region>(Region::makeEmpty(200, 200));
    std::fill(region->mask.begin(), region->mask.end(), 1);

    RegionMeasureTool rmTool({});
    auto res = rmTool.execute(makeInputRegionsHM({region}, std::make_shared<HeightMap>(hm)));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    double vol = getMeas(res.output, "volumeMm3");
    // 가우시안 체적 = 2π * σ² * h = 2π * 1 * 0.5 ≈ 3.14mm³
    const double expectedVol = 2.0 * 3.14159265358979323846 * BR * BR * BH;
    EXPECT_NEAR(vol, expectedVol, expectedVol * 0.3) << "체적 ±30% (경계 픽셀 효과)";
}

// ── 9. 엣지 위치 (GradientMap→Threshold→RegionMeasure centroid) ───────────
TEST(Workflow, EdgePosition) {
    HeightMap hm = makeStep(200, 100, 100, 0.0, 1.0, 0.05f, 0.05f, 0.001f);
    // 엣지 X위치 = 100 * 0.05mm = 5.0mm

    GradientMapParams gp; gp.output_mode = "magnitude"; gp.ksize = 3;
    GradientMapTool gTool(gp);
    auto gRes = gTool.execute(makeInputHM(std::make_shared<HeightMap>(hm)));
    ASSERT_EQ(gRes.status, ToolStatus::Ok);

    ThresholdParams tp; tp.thresholdMm = 0.5; tp.keepAbove = true;
    ThresholdTool tTool(tp);
    auto tRes = tTool.execute(makeInputHM(gRes.output->heightmaps[0]));
    ASSERT_EQ(tRes.status, ToolStatus::Ok);

    HeightMap dummy = makeBlank(200, 100, 0.05f, 0.05f, 0.001f);
    RegionMeasureTool rmTool({});
    auto rmRes = rmTool.execute(makeInputRegionsHM(tRes.output->regions, std::make_shared<HeightMap>(dummy)));
    ASSERT_EQ(rmRes.status, ToolStatus::Ok);

    double cx = getMeas(rmRes.output, "cxMm");
    EXPECT_NEAR(cx, 5.0, 0.3) << "엣지 X위치 5.0mm ±0.3mm";
}

// ── 10. 공면도 (RegionBoolean→PlaneFit.rmse) ─────────────────────────────
TEST(Workflow, Coplanarity) {
    // 4개 영역, 모두 z=1mm 평면 → rmse 작아야 함
    const float xRes = 0.05f, yRes = 0.05f, zRes = 0.001f;
    HeightMap hm = makeTiltedPlane(200, 200, 0.0, 0.0, 1.0, xRes, yRes, zRes);

    // 4개 독립 Region
    std::vector<std::shared_ptr<Region>> rgs;
    const int regions[4][4] = {{10,10,40,40},{50,10,90,40},{10,50,40,90},{50,50,90,90}};
    for (auto& r : regions) {
        auto rg = std::make_shared<Region>(Region::makeEmpty(200, 200));
        for (int row = r[1]; row < r[3]; ++row)
            for (int col = r[0]; col < r[2]; ++col)
                rg->mask[row * 200 + col] = 1;
        rgs.push_back(rg);
    }

    // RegionBoolean 체인으로 합집합
    auto makeOr = [](std::shared_ptr<Region> a, std::shared_ptr<Region> b) {
        auto p0 = std::make_shared<VisionData>(); p0->regions = {a};
        auto p1 = std::make_shared<VisionData>(); p1->regions = {b};
        auto in = std::make_shared<VisionData>(); in->inputs = {p0, p1};
        RegionBooleanParams bp; bp.op = "or";
        return RegionBooleanTool(bp).execute(in);
    };
    auto r01 = makeOr(rgs[0], rgs[1]);
    auto r23 = makeOr(rgs[2], rgs[3]);
    auto rAll = makeOr(r01.output->regions[0], r23.output->regions[0]);
    ASSERT_EQ(rAll.status, ToolStatus::Ok);

    PlaneFitTool pfTool({});
    auto pfRes = pfTool.execute(makeInputHMRegions(
        std::make_shared<HeightMap>(hm), rAll.output->regions));
    ASSERT_EQ(pfRes.status, ToolStatus::Ok);

    double rmse = getMeas(pfRes.output, "rmse");
    EXPECT_LT(rmse, 0.005) << "공면도 rmse < 5μm (완전 평면 입력)";
}
