#include <gtest/gtest.h>
#include "ProfileFeatureTool.h"
#include "Aggregate.h"
#include "VisionData.h"
#include "Profile.h"
#include <cmath>
#include <numeric>
#include <vector>
#include <limits>

using namespace vision;
using namespace vision::agg;

// ── 헬퍼 ─────────────────────────────────────────────────────────────────

// Profile을 포트 0으로 싸는 VisionData
static VisionDataPtr wrapProfile(std::shared_ptr<Profile> prof) {
    auto port0 = std::make_shared<VisionData>();
    port0->profiles.push_back(std::move(prof));
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(port0));
    return d;
}

// 기지 z값으로 Profile 생성 (s, x, y는 인덱스 기반 단순값)
// z 벡터를 직접 받아 구성. NaN도 허용.
static std::shared_ptr<Profile> makeProfile(std::vector<double> z,
                                             double sStep = 1.0) {
    auto p = std::make_shared<Profile>();
    const int n = static_cast<int>(z.size());
    p->z = std::move(z);
    p->s.resize(n); p->x.resize(n); p->y.resize(n);
    for (int i = 0; i < n; ++i) {
        p->s[i] = i * sStep;
        p->x[i] = static_cast<double>(i);
        p->y[i] = 0.0;
    }
    return p;
}

// ── 기본 ─────────────────────────────────────────────────────────────────

TEST(ProfileFeatureTest, NoProfileFails) {
    auto d = std::make_shared<VisionData>();
    ProfileFeatureTool tool;
    EXPECT_EQ(tool.execute(d).status, ToolStatus::Fail);
}

TEST(ProfileFeatureTest, AllNaNFails) {
    auto prof = makeProfile({std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::quiet_NaN()});
    ProfileFeatureTool tool;
    EXPECT_EQ(tool.execute(wrapProfile(prof)).status, ToolStatus::Fail);
}

TEST(ProfileFeatureTest, UnknownKindFails) {
    auto prof = makeProfile({1.0, 2.0});
    ProfileFeatureParams p; p.kind = "nonsense";
    ProfileFeatureTool tool(p);
    EXPECT_EQ(tool.execute(wrapProfile(prof)).status, ToolStatus::Fail);
}

// ── maxZ / minZ ───────────────────────────────────────────────────────────

// z = {1, 3, NaN, 5, 2} → maxZ=5 at s=3*sStep
TEST(ProfileFeatureTest, MaxZ) {
    auto prof = makeProfile({1, 3, std::numeric_limits<double>::quiet_NaN(), 5, 2}, 0.5);
    ProfileFeatureParams p; p.kind = "maxZ";
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_FALSE(res.output->measurements.empty());
    const auto& m = res.output->measurements;
    EXPECT_DOUBLE_EQ(m[0].value, 5.0);   // maxZ value
    EXPECT_NEAR(m[1].value, 3 * 0.5, 1e-10);  // maxZ_s = index 3, sStep=0.5
    EXPECT_FALSE(res.output->points.empty());
    EXPECT_TRUE(res.output->points[0].valid);
}

// z = {1, 3, NaN, 5, 2} → minZ=1 at s=0
TEST(ProfileFeatureTest, MinZ) {
    auto prof = makeProfile({1, 3, std::numeric_limits<double>::quiet_NaN(), 5, 2});
    ProfileFeatureParams p; p.kind = "minZ";
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 1.0);
    EXPECT_DOUBLE_EQ(res.output->measurements[1].value, 0.0);  // s=0
}

// nth=1: 두 번째로 큰 z (5→3)
TEST(ProfileFeatureTest, MaxZNth1) {
    auto prof = makeProfile({1, 3, std::numeric_limits<double>::quiet_NaN(), 5, 2});
    ProfileFeatureParams p; p.kind = "maxZ"; p.nth = 1;
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 3.0);
}

// nth=-1: 가장 작은 z (minZ와 동일)
TEST(ProfileFeatureTest, MaxZNthNeg1) {
    auto prof = makeProfile({1, 3, 5, 2});
    ProfileFeatureParams p; p.kind = "maxZ"; p.nth = -1;
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 1.0);  // 최소 = nth-from-end last
}

// ── maxS / minS ───────────────────────────────────────────────────────────

// z = {NaN, 1, 2, NaN}. 유효 인덱스: 1, 2
// minS = s[1]=1, maxS = s[2]=2
TEST(ProfileFeatureTest, MinSMaxS) {
    auto prof = makeProfile({std::numeric_limits<double>::quiet_NaN(), 1, 2,
                              std::numeric_limits<double>::quiet_NaN()});
    {
        ProfileFeatureParams p; p.kind = "minS";
        ProfileFeatureTool tool(p);
        auto res = tool.execute(wrapProfile(prof));
        ASSERT_EQ(res.status, ToolStatus::Ok);
        EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 1.0);  // s[1]=1
    }
    {
        ProfileFeatureParams p; p.kind = "maxS";
        ProfileFeatureTool tool(p);
        auto res = tool.execute(wrapProfile(prof));
        ASSERT_EQ(res.status, ToolStatus::Ok);
        EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 2.0);  // s[2]=2
    }
}

// minS nth=-1 = maxS nth=0 (last valid sample)
TEST(ProfileFeatureTest, MinSNthNeg1IsMaxS) {
    auto prof = makeProfile({1, 2, 3, 4});
    ProfileFeatureParams pMin; pMin.kind = "minS"; pMin.nth = -1;
    ProfileFeatureParams pMax; pMax.kind = "maxS"; pMax.nth =  0;
    ProfileFeatureTool tMin(pMin), tMax(pMax);
    auto rMin = tMin.execute(wrapProfile(prof));
    auto rMax = tMax.execute(wrapProfile(prof));
    ASSERT_EQ(rMin.status, ToolStatus::Ok);
    ASSERT_EQ(rMax.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(rMin.output->measurements[0].value,
                     rMax.output->measurements[0].value);
}

// ── mean / median / stdDev ────────────────────────────────────────────────

// z = {1, 3, NaN, 5, 2} 유효: {1,3,5,2} — Aggregate와 일치
TEST(ProfileFeatureTest, MeanMatchesAggregate) {
    std::vector<double> zFull = {1, 3, std::numeric_limits<double>::quiet_NaN(), 5, 2};
    std::vector<double> zValid = {1, 3, 5, 2};
    auto expected = mean(zValid.data(), zValid.size());

    auto prof = makeProfile(zFull);
    ProfileFeatureParams p; p.kind = "mean";
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, expected.value);
    EXPECT_EQ(res.output->measurements[0].name, "mean");
    EXPECT_EQ(res.output->measurements[0].unit, "mm");
}

TEST(ProfileFeatureTest, MedianMatchesAggregate) {
    std::vector<double> zValid = {1, 3, 5, 2};
    auto expected = median(zValid.data(), zValid.size());

    auto prof = makeProfile({1, 3, std::numeric_limits<double>::quiet_NaN(), 5, 2});
    ProfileFeatureParams p; p.kind = "median";
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, expected.value);
}

TEST(ProfileFeatureTest, StdDevMatchesAggregate) {
    std::vector<double> zValid = {1, 3, 5, 2};
    auto expected = stdDev(zValid.data(), zValid.size());

    auto prof = makeProfile({1, 3, std::numeric_limits<double>::quiet_NaN(), 5, 2});
    ProfileFeatureParams p; p.kind = "stdDev";
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, expected.value);
}

// ── percentile / highTail ─────────────────────────────────────────────────

TEST(ProfileFeatureTest, PercentileMatchesAggregate) {
    std::vector<double> zValid = {2, 4, 6, 8, 10};
    const double pct = 75;
    auto expected = percentile(zValid.data(), zValid.size(), pct);

    auto prof = makeProfile(zValid);
    ProfileFeatureParams p; p.kind = "percentile"; p.percentile = pct;
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, expected.value);
}

TEST(ProfileFeatureTest, HighTailMatchesAggregate) {
    std::vector<double> zValid = {2, 4, 6, 8, 10};
    const double pct = 20;
    auto expected = highTail(zValid.data(), zValid.size(), pct);

    auto prof = makeProfile(zValid);
    ProfileFeatureParams p; p.kind = "highTail"; p.percentile = pct;
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, expected.value);
}

// ── 검색 구간 ─────────────────────────────────────────────────────────────

// s=[0,1,2,3,4], z=[1,2,3,4,5]. 구간 s∈[1,3] → z={2,3,4} mean=3
TEST(ProfileFeatureTest, SearchRangeFilters) {
    auto prof = makeProfile({1, 2, 3, 4, 5});
    ProfileFeatureParams p; p.kind = "mean";
    p.searchFromMm = 1.0; p.searchToMm = 3.0;
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 3.0);
}

// 검색 구간 안에 유효 샘플 없으면 Fail
TEST(ProfileFeatureTest, SearchRangeNoValidSamplesFails) {
    auto prof = makeProfile({1, 2, 3, 4, 5});
    ProfileFeatureParams p; p.kind = "mean";
    p.searchFromMm = 10.0; p.searchToMm = 20.0;
    ProfileFeatureTool tool(p);
    EXPECT_EQ(tool.execute(wrapProfile(prof)).status, ToolStatus::Fail);
}

// ── 단일 표본 ──────────────────────────────────────────────────────────────

// 단일 유효 샘플: mean == 그 값, stdDev == 0 (n=1이면 std::sqrt(0/0) → agg returns invalid)
// 설계: n<2이면 stdDev.valid=false → Fail
TEST(ProfileFeatureTest, SingleSampleMeanOk) {
    auto prof = makeProfile({7.0});
    ProfileFeatureParams p; p.kind = "mean";
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 7.0);
}

TEST(ProfileFeatureTest, SingleSampleStdDevIsZero) {
    auto prof = makeProfile({7.0});
    ProfileFeatureParams p; p.kind = "stdDev";
    ProfileFeatureTool tool(p);
    // Aggregate::stdDev n=1 → 0.0 (분산 없음) — valid result
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_DOUBLE_EQ(res.output->measurements[0].value, 0.0);
}

// ── nth 범위 초과 ─────────────────────────────────────────────────────────

TEST(ProfileFeatureTest, NthOutOfRangeFails) {
    auto prof = makeProfile({1, 2, 3});
    ProfileFeatureParams p; p.kind = "maxZ"; p.nth = 5;  // 유효 3개, nth=5 → 초과
    ProfileFeatureTool tool(p);
    EXPECT_EQ(tool.execute(wrapProfile(prof)).status, ToolStatus::Fail);
}

// ── Phase 6: edge 검출 ────────────────────────────────────────────────────

// 헬퍼: measurements에서 이름으로 값 찾기. 없으면 -9999 반환.
static double findMeas(const ToolResult& res, const std::string& name) {
    if (!res.output) return -9999.0;
    for (const auto& m : res.output->measurements)
        if (m.name == name) return m.value;
    return -9999.0;
}

// 완전한 계단: z[0..4]=0, z[5..9]=1, sStep=0.1
// dz[5]=1.0, 다른 dz=0. 서브픽셀 보정 없음 → edge_s = s[5] = 0.5 mm
TEST(ProfileFeatureTest, EdgePositionSubpixel) {
    auto prof = makeProfile({0, 0, 0, 0, 0, 1, 1, 1, 1, 1}, 0.1);
    ProfileFeatureParams p;
    p.kind = "edge"; p.edgeDir = "any";
    p.edgeThresholdMm = 0.5; p.smoothWindow = 1;
    ProfileFeatureTool tool(p);
    auto res = tool.execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const double edge_s = findMeas(res, "edge_s");
    ASSERT_NE(edge_s, -9999.0);
    // sub-pixel 오차 < 0.1 px = 0.01 mm
    EXPECT_NEAR(edge_s, 0.5, 0.01) << "edge_s 서브픽셀 오차 > 0.1 px";
}

// 상승 엣지만 있는 프로파일: "rising" → Ok, "falling" → Fail
TEST(ProfileFeatureTest, EdgeDirRisingOnlyDetectsRising) {
    auto prof = makeProfile({0, 0, 0, 0, 0, 1, 1, 1, 1, 1}, 0.1);
    {
        ProfileFeatureParams p; p.kind = "edge"; p.edgeDir = "rising";
        p.edgeThresholdMm = 0.5; p.smoothWindow = 1;
        EXPECT_EQ(ProfileFeatureTool(p).execute(wrapProfile(prof)).status, ToolStatus::Ok);
    }
    {
        ProfileFeatureParams p; p.kind = "edge"; p.edgeDir = "falling";
        p.edgeThresholdMm = 0.5; p.smoothWindow = 1;
        EXPECT_EQ(ProfileFeatureTool(p).execute(wrapProfile(prof)).status, ToolStatus::Fail);
    }
}

// 하강 엣지만 있는 프로파일: "falling" → Ok, "rising" → Fail
TEST(ProfileFeatureTest, EdgeDirFallingOnlyDetectsFalling) {
    auto prof = makeProfile({1, 1, 1, 1, 1, 0, 0, 0, 0, 0}, 0.1);
    {
        ProfileFeatureParams p; p.kind = "edge"; p.edgeDir = "falling";
        p.edgeThresholdMm = 0.5; p.smoothWindow = 1;
        auto res = ProfileFeatureTool(p).execute(wrapProfile(prof));
        ASSERT_EQ(res.status, ToolStatus::Ok);
        EXPECT_NEAR(findMeas(res, "edge_s"), 0.5, 0.01);
    }
    {
        ProfileFeatureParams p; p.kind = "edge"; p.edgeDir = "rising";
        p.edgeThresholdMm = 0.5; p.smoothWindow = 1;
        EXPECT_EQ(ProfileFeatureTool(p).execute(wrapProfile(prof)).status, ToolStatus::Fail);
    }
}

// 계단 높이 < 임계 → 검출 없음 (Fail)
TEST(ProfileFeatureTest, EdgeBelowThreshold) {
    auto prof = makeProfile({0, 0, 0, 0, 0, 0.1, 0.1, 0.1, 0.1, 0.1}, 0.1);
    ProfileFeatureParams p; p.kind = "edge"; p.edgeDir = "any";
    p.edgeThresholdMm = 0.5; p.smoothWindow = 1;  // 0.1 < 0.5
    EXPECT_EQ(ProfileFeatureTool(p).execute(wrapProfile(prof)).status, ToolStatus::Fail);
}

// NaN 구간을 사이에 두고 값이 바뀌어도 허위 엣지 없음
TEST(ProfileFeatureTest, EdgeNaNGapNoFakeEdge) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    // 0→NaN→1: NaN 경계에서 dz=NaN → 검출 불가
    auto prof = makeProfile({0, 0, 0, nan, 1, 1, 1}, 0.1);
    ProfileFeatureParams p; p.kind = "edge"; p.edgeDir = "any";
    p.edgeThresholdMm = 0.5; p.smoothWindow = 1;
    EXPECT_EQ(ProfileFeatureTool(p).execute(wrapProfile(prof)).status, ToolStatus::Fail)
        << "NaN 구간 허위 엣지 없음";
}

// 두 번째 엣지 선택 (nth=1)
TEST(ProfileFeatureTest, EdgeNthSecond) {
    // 두 상승 계단: z={0,0,1,1,2,2}, 엣지 at s=0.2, s=0.4
    auto prof = makeProfile({0, 0, 1, 1, 2, 2}, 0.1);
    ProfileFeatureParams p; p.kind = "edge"; p.edgeDir = "rising";
    p.edgeThresholdMm = 0.5; p.nth = 1; p.smoothWindow = 1;
    auto res = ProfileFeatureTool(p).execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "edge_s"), 0.4, 0.01);
}

// ── Phase 6: ridge / valley ───────────────────────────────────────────────

// 종 모양: z[3]=3 이 최고점 → ridge_s=3.0, ridge_z=3.0
TEST(ProfileFeatureTest, RidgeDetected) {
    auto prof = makeProfile({0, 1, 2, 3, 2, 1, 0}, 1.0);
    ProfileFeatureParams p; p.kind = "ridge";
    p.edgeThresholdMm = 0.0; p.smoothWindow = 1;
    auto res = ProfileFeatureTool(p).execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "ridge_s"), 3.0, 0.01);
    EXPECT_NEAR(findMeas(res, "ridge_z"), 3.0, 0.01);
    ASSERT_FALSE(res.output->points.empty());
    EXPECT_TRUE(res.output->points[0].valid);
}

// 역 종 모양: z[3]=0 이 최저점 → valley_s=3.0, valley_z=0.0
TEST(ProfileFeatureTest, ValleyDetected) {
    auto prof = makeProfile({3, 2, 1, 0, 1, 2, 3}, 1.0);
    ProfileFeatureParams p; p.kind = "valley";
    p.edgeThresholdMm = 0.0; p.smoothWindow = 1;
    auto res = ProfileFeatureTool(p).execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "valley_s"), 3.0, 0.01);
    EXPECT_NEAR(findMeas(res, "valley_z"), 0.0, 0.01);
}

// ── Phase 6: corner ───────────────────────────────────────────────────────

// z={0,0,0,2,4,4,4}: d2z[2]=2, d2z[4]=-2 → 두 corner 중 nth=0 → s=2.0
TEST(ProfileFeatureTest, CornerDetected) {
    auto prof = makeProfile({0, 0, 0, 2, 4, 4, 4}, 1.0);
    ProfileFeatureParams p; p.kind = "corner";
    p.edgeThresholdMm = 1.5; p.smoothWindow = 1; p.nth = 0;
    auto res = ProfileFeatureTool(p).execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "corner_s"), 2.0, 0.01);
}

// 같은 프로파일, nth=1 → 두 번째 corner at s=4.0
TEST(ProfileFeatureTest, CornerNthSecond) {
    auto prof = makeProfile({0, 0, 0, 2, 4, 4, 4}, 1.0);
    ProfileFeatureParams p; p.kind = "corner";
    p.edgeThresholdMm = 1.5; p.smoothWindow = 1; p.nth = 1;
    auto res = ProfileFeatureTool(p).execute(wrapProfile(prof));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "corner_s"), 4.0, 0.01);
}
