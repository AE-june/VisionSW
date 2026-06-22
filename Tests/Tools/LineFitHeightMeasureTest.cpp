#include <gtest/gtest.h>
#include "LineFitHeightMeasure.h"
#include "VisionData.h"
#include <cmath>

using namespace vision;

// ─────────────────────────────────────────────────────────────────────
//  테스트용 ZMap 생성 헬퍼
// ─────────────────────────────────────────────────────────────────────

// 완전히 평탄한 ZMap (z = constZ)
static ZMapPtr makeFlatZMap(int w, int h, float constZ,
                             float xRes = 0.1f, float yRes = 0.1f, float zRes = 0.001f) {
    auto m = std::make_shared<ZMap>();
    m->width  = w;  m->height = h;
    m->xResMm = xRes; m->yResMm = yRes; m->zResMm = zRes;
    m->data.assign(static_cast<size_t>(w) * h, constZ);
    return m;
}

// 기울어진 ZMap: z = slope_raw * col + base_raw  (raw 단위)
static ZMapPtr makeSlopedZMap(int w, int h,
                               float slopeRaw, float baseRaw,
                               float xRes = 0.1f, float yRes = 0.1f, float zRes = 0.001f) {
    auto m = std::make_shared<ZMap>();
    m->width  = w;  m->height = h;
    m->xResMm = xRes; m->yResMm = yRes; m->zResMm = zRes;
    m->data.resize(static_cast<size_t>(w) * h);
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col)
            m->data[row * w + col] = slopeRaw * col + baseRaw;
    return m;
}

// ZMap + VisionDataPtr 래퍼
static VisionDataPtr wrap(ZMapPtr z) {
    auto d = std::make_shared<VisionData>();
    d->zmap = z;
    return d;
}

// ─────────────────────────────────────────────────────────────────────
//  기본 동작 테스트
// ─────────────────────────────────────────────────────────────────────
TEST(LineFitHeightMeasureTest, NullInputFails) {
    LineFitHeightMeasure tool;
    EXPECT_EQ(tool.execute(nullptr).status, ToolStatus::Fail);
}

TEST(LineFitHeightMeasureTest, NoZMapFails) {
    auto d = std::make_shared<VisionData>();  // zmap = null
    LineFitHeightMeasure tool;
    EXPECT_EQ(tool.execute(d).status, ToolStatus::Fail);
}

// ─────────────────────────────────────────────────────────────────────
//  평탄한 표면: 직선 기울기 ≈ 0, 높이차 ≈ 0
// ─────────────────────────────────────────────────────────────────────
TEST(LineFitHeightMeasureTest, FlatSurface_ZeroSlopeZeroDiff) {
    // 200×100 ZMap, 모두 z = 1000 raw (= 1.0mm with zRes=0.001)
    auto zmap = makeFlatZMap(200, 100, 1000.f);

    LineFitParams p;
    p.roiFit1    = { 0,   0, 40, 100 };
    p.roiFit2    = { 150, 0, 40, 100 };
    p.roiMeasure = { 80,  0, 40, 100 };
    p.aggregation = ZAggregation::Max;

    LineFitHeightMeasure tool(p);
    auto result = tool.execute(wrap(zmap));

    EXPECT_EQ(result.status, ToolStatus::Ok);
    EXPECT_TRUE(tool.lastResult().valid);
    EXPECT_NEAR(tool.lastResult().slope,      0.0, 1e-6);
    EXPECT_NEAR(tool.lastResult().heightDiff, 0.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────
//  기울어진 기준선 + 측정점이 직선 위에 있는 경우 → 높이차 ≈ 0
// ─────────────────────────────────────────────────────────────────────
TEST(LineFitHeightMeasureTest, SlopedSurface_MeasureOnLine_ZeroDiff) {
    // z = 2.0 * col (raw) → z_mm = 2.0*col*0.001, x_mm = col*0.1
    // slope_mm = 2.0*0.001 / 0.1 = 0.02 mm/mm
    auto zmap = makeSlopedZMap(200, 100, 2.0f, 0.f);

    LineFitParams p;
    p.roiFit1    = { 0,   0, 40, 100 };
    p.roiFit2    = { 150, 0, 40, 100 };
    p.roiMeasure = { 80,  0, 40, 100 };
    p.aggregation = ZAggregation::Max;

    LineFitHeightMeasure tool(p);
    auto result = tool.execute(wrap(zmap));

    EXPECT_EQ(result.status, ToolStatus::Ok);
    EXPECT_NEAR(tool.lastResult().heightDiff, 0.0, 1e-3);
}

// ─────────────────────────────────────────────────────────────────────
//  기울어진 기준선 + 측정점에 Z 오프셋 → 높이차가 정확히 나오는지
// ─────────────────────────────────────────────────────────────────────
TEST(LineFitHeightMeasureTest, SlopedSurface_MeasureWithOffset) {
    // 기준선: z = 2.0*col raw
    // 측정 영역: z = 2.0*col + 500 raw  (= +0.5mm 오프셋)
    auto zmap = makeSlopedZMap(200, 100, 2.0f, 0.f);

    // 측정 ROI에만 +500 raw 추가
    Rect2D roiMeasure = { 80, 0, 40, 100 };
    for (int row = roiMeasure.y; row < roiMeasure.bottom(); ++row)
        for (int col = roiMeasure.x; col < roiMeasure.right(); ++col)
            zmap->data[row * zmap->width + col] += 500.f;

    LineFitParams p;
    p.roiFit1    = { 0,   0, 40, 100 };
    p.roiFit2    = { 150, 0, 40, 100 };
    p.roiMeasure = roiMeasure;
    p.aggregation = ZAggregation::Max;

    LineFitHeightMeasure tool(p);
    auto result = tool.execute(wrap(zmap));

    EXPECT_EQ(result.status, ToolStatus::Ok);
    // 기대 높이차: 500 raw * 0.001 mm/raw = 0.5 mm
    // 단, 직선 기울기가 있으므로 수선의 발 기준 계산 → 아주 가까워야 함
    EXPECT_NEAR(tool.lastResult().heightDiff, 0.5, 0.01);
}

// ─────────────────────────────────────────────────────────────────────
//  수선의 발 공식 검증 (기울기가 있을 때 단순 Q.z - (a*Qx+b) 와 차이)
// ─────────────────────────────────────────────────────────────────────
TEST(LineFitHeightMeasureTest, FootOfPerpendicular_VsNaiveFormula) {
    // 기울기가 큰 경우: slope = 10 (45도 이상)
    // 수선의 발 기준 높이차 ≠ Q.z - (a*Qx + b)
    auto zmap = makeSlopedZMap(200, 100, 10.0f, 0.f);  // steep slope

    // 측정 ROI에 +1000 raw 오프셋
    Rect2D roiMeasure = { 80, 0, 40, 100 };
    for (int row = 0; row < 100; ++row)
        for (int col = 80; col < 120; ++col)
            zmap->data[row * zmap->width + col] += 1000.f;

    LineFitParams p;
    p.roiFit1    = { 0,   0, 40, 100 };
    p.roiFit2    = { 150, 0, 40, 100 };
    p.roiMeasure = roiMeasure;
    p.aggregation = ZAggregation::Max;

    LineFitHeightMeasure tool(p);
    auto result = tool.execute(wrap(zmap));
    ASSERT_EQ(result.status, ToolStatus::Ok);

    const auto& r = tool.lastResult();

    // 수선의 발 기준 heightDiff
    double footDiff = r.heightDiff;

    // 단순 공식: Q.z - (slope*Q.x + intercept) — 기울기 보정 없음
    double naiveDiff = r.Qz - (r.slope * r.Qx + r.intercept);

    // 기울기가 크면 두 값이 다름 (수선의 발이 더 작은 값)
    // heightDiff < naiveDiff (수선이 더 짧음)
    EXPECT_LT(footDiff, naiveDiff + 1e-9);
    EXPECT_GT(naiveDiff - footDiff, -1e-9); // 차이가 양수여야 함
}

// ─────────────────────────────────────────────────────────────────────
//  RANSAC — 이상치가 섞여도 피팅이 안정적인지
// ─────────────────────────────────────────────────────────────────────
TEST(LineFitHeightMeasureTest, RANSAC_RobustToOutliers) {
    // 평탄한 기준선에 일부 이상치 주입
    auto zmap = makeFlatZMap(200, 100, 1000.f);

    // ROI1 (0~40) 과 ROI2 (150~190) 에 스파이크 노이즈 20% 주입
    for (int col = 0; col < 40; col += 5)
        for (int row = 0; row < 100; ++row)
            zmap->data[row * 200 + col] = 5000.f;  // 이상치

    for (int col = 150; col < 190; col += 5)
        for (int row = 0; row < 100; ++row)
            zmap->data[row * 200 + col] = 5000.f;

    // 측정 ROI: 깨끗한 영역 + 오프셋
    Rect2D roiMeasure = { 80, 0, 40, 100 };
    for (int row = 0; row < 100; ++row)
        for (int col = 80; col < 120; ++col)
            zmap->data[row * 200 + col] = 1500.f; // +0.5mm

    LineFitParams p;
    p.roiFit1    = { 0,   0, 40, 100 };
    p.roiFit2    = { 150, 0, 40, 100 };
    p.roiMeasure = roiMeasure;
    p.aggregation    = ZAggregation::Mean;   // 이상치 포함된 상태로 집계
    p.useRansac      = true;
    p.ransacIterations   = 300;
    p.ransacThresholdMm  = 0.2f;

    LineFitHeightMeasure tool(p);
    auto result = tool.execute(wrap(zmap));

    EXPECT_EQ(result.status, ToolStatus::Ok);
    // RANSAC이 이상치 제거 후 올바른 높이차 추정
    EXPECT_NEAR(tool.lastResult().heightDiff, 0.5, 0.1);
}

// ─────────────────────────────────────────────────────────────────────
//  ZAggregation::HighTail 테스트
// ─────────────────────────────────────────────────────────────────────
TEST(LineFitHeightMeasureTest, Aggregation_HighTail) {
    auto zmap = makeFlatZMap(200, 100, 1000.f);

    // 측정 ROI에 Z 변화: 일부는 낮고 일부는 높음
    // → HighTail은 상위 값들만 취하므로 max에 가까운 값을 반환해야 함
    for (int row = 0; row < 100; ++row)
        for (int col = 80; col < 120; ++col)
            // 행마다 다른 Z 값 부여 (0~2000 raw)
            zmap->data[row * 200 + col] = static_cast<float>(row * 20);

    LineFitParams p;
    p.roiFit1    = { 0,   0, 40, 100 };
    p.roiFit2    = { 150, 0, 40, 100 };
    p.roiMeasure = { 80,  0, 40, 100 };
    p.aggregation = ZAggregation::HighTail;
    p.highTailPct = 10.f;   // 상위 10%

    LineFitHeightMeasure tool(p);
    auto result = tool.execute(wrap(zmap));
    EXPECT_EQ(result.status, ToolStatus::Ok);
    // 상위 10% = row 90~99 → z_mm ≈ (1800~1980)*0.001 ≈ 1.9mm
    // 기준선 z_mm = 1.0mm → heightDiff ≈ 0.9mm
    EXPECT_GT(tool.lastResult().heightDiff, 0.5);
}
