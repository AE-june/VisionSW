#include <gtest/gtest.h>
#include "CompareTool.h"
#include "CombineDecisionTool.h"
#include "VisionData.h"
#include <vector>

using namespace vision;

// ── 헬퍼 ─────────────────────────────────────────────────────────────────

static VisionDataPtr wrapMeasurements(std::vector<Measurement> meas) {
    auto inner = std::make_shared<VisionData>();
    inner->measurements = std::move(meas);
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(inner));
    return d;
}

static VisionDataPtr wrapDecisions(std::vector<Decision> decs) {
    auto inner = std::make_shared<VisionData>();
    inner->decisions = std::move(decs);
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(inner));
    return d;
}

static Measurement makeMeas(const std::string& name, double value) {
    return {name, value, "mm", true};
}

static Decision makeDec(const std::string& name, bool pass) {
    Decision d; d.name = name; d.pass = pass; return d;
}

// ── CompareTool: 기본 ─────────────────────────────────────────────────────

TEST(CompareToolTest, NoInputFails) {
    auto d = std::make_shared<VisionData>();
    EXPECT_EQ(CompareTool().execute(d).status, ToolStatus::Fail);
}

TEST(CompareToolTest, EmptyMeasurementsFails) {
    auto d = wrapMeasurements({});
    EXPECT_EQ(CompareTool().execute(d).status, ToolStatus::Fail);
}

TEST(CompareToolTest, TargetNotFoundFails) {
    auto d = wrapMeasurements({makeMeas("height", 3.0)});
    CompareParams p; p.target = "width"; p.mode = "tolerance";
    p.nominal = 3.0; p.tolerance = 0.1;
    EXPECT_EQ(CompareTool(p).execute(d).status, ToolStatus::Fail);
}

// ── CompareTool: tolerance 모드 ───────────────────────────────────────────

TEST(CompareToolTest, TolerancePass) {
    auto d = wrapMeasurements({makeMeas("h", 3.02)});
    CompareParams p; p.mode = "tolerance"; p.nominal = 3.0; p.tolerance = 0.05;
    auto res = CompareTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_FALSE(res.output->decisions.empty());
    EXPECT_TRUE(res.output->decisions[0].pass);
}

TEST(CompareToolTest, ToleranceFail) {
    auto d = wrapMeasurements({makeMeas("h", 3.10)});
    CompareParams p; p.mode = "tolerance"; p.nominal = 3.0; p.tolerance = 0.05;
    auto res = CompareTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_FALSE(res.output->decisions[0].pass);
}

// 경계값: |measured - nominal| == tolerance → pass
TEST(CompareToolTest, ToleranceBoundaryPass) {
    auto d = wrapMeasurements({makeMeas("h", 3.05)});
    CompareParams p; p.mode = "tolerance"; p.nominal = 3.0; p.tolerance = 0.05;
    auto res = CompareTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(res.output->decisions[0].pass);
}

// ── CompareTool: range 모드 ───────────────────────────────────────────────

TEST(CompareToolTest, RangePass) {
    auto d = wrapMeasurements({makeMeas("x", 2.5)});
    CompareParams p; p.mode = "range"; p.min = 2.0; p.max = 3.0;
    auto res = CompareTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(res.output->decisions[0].pass);
}

TEST(CompareToolTest, RangeFail_Below) {
    auto d = wrapMeasurements({makeMeas("x", 1.9)});
    CompareParams p; p.mode = "range"; p.min = 2.0; p.max = 3.0;
    EXPECT_FALSE(CompareTool(p).execute(d).output->decisions[0].pass);
}

TEST(CompareToolTest, RangeFail_Above) {
    auto d = wrapMeasurements({makeMeas("x", 3.1)});
    CompareParams p; p.mode = "range"; p.min = 2.0; p.max = 3.0;
    EXPECT_FALSE(CompareTool(p).execute(d).output->decisions[0].pass);
}

// ── CompareTool: max 모드 ─────────────────────────────────────────────────

TEST(CompareToolTest, MaxPass) {
    auto d = wrapMeasurements({makeMeas("gap", 0.03)});
    CompareParams p; p.mode = "max"; p.max = 0.05;
    EXPECT_TRUE(CompareTool(p).execute(d).output->decisions[0].pass);
}

TEST(CompareToolTest, MaxFail) {
    auto d = wrapMeasurements({makeMeas("gap", 0.07)});
    CompareParams p; p.mode = "max"; p.max = 0.05;
    EXPECT_FALSE(CompareTool(p).execute(d).output->decisions[0].pass);
}

// ── CompareTool: min 모드 ─────────────────────────────────────────────────

TEST(CompareToolTest, MinPass) {
    auto d = wrapMeasurements({makeMeas("score", 0.9)});
    CompareParams p; p.mode = "min"; p.min = 0.8;
    EXPECT_TRUE(CompareTool(p).execute(d).output->decisions[0].pass);
}

TEST(CompareToolTest, MinFail) {
    auto d = wrapMeasurements({makeMeas("score", 0.7)});
    CompareParams p; p.mode = "min"; p.min = 0.8;
    EXPECT_FALSE(CompareTool(p).execute(d).output->decisions[0].pass);
}

// ── CompareTool: target 필터 ──────────────────────────────────────────────

// target 미지정 → 모든 측정값 비교 (3개 → 3 decisions)
TEST(CompareToolTest, AllTargetsCompared) {
    auto d = wrapMeasurements({makeMeas("a", 1.0), makeMeas("b", 2.0), makeMeas("c", 3.0)});
    CompareParams p; p.mode = "max"; p.max = 2.5;
    auto res = CompareTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->decisions.size(), 3u);
    EXPECT_TRUE(res.output->decisions[0].pass);   // a=1.0 ≤ 2.5
    EXPECT_TRUE(res.output->decisions[1].pass);   // b=2.0 ≤ 2.5
    EXPECT_FALSE(res.output->decisions[2].pass);  // c=3.0 > 2.5
}

// target 지정 → 해당 측정값만 비교
TEST(CompareToolTest, TargetFilterSingleMatch) {
    auto d = wrapMeasurements({makeMeas("height", 3.0), makeMeas("width", 10.0)});
    CompareParams p; p.target = "height"; p.mode = "tolerance";
    p.nominal = 3.0; p.tolerance = 0.05;
    auto res = CompareTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->decisions.size(), 1u);
    EXPECT_EQ(res.output->decisions[0].name, "height");
    EXPECT_TRUE(res.output->decisions[0].pass);
}

// 측정값 pass-through 확인
TEST(CompareToolTest, MeasurementsPassThrough) {
    auto d = wrapMeasurements({makeMeas("h", 3.0), makeMeas("w", 5.0)});
    CompareParams p; p.mode = "max"; p.max = 10.0;
    auto res = CompareTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->measurements.size(), 2u);
    EXPECT_EQ(res.output->measurements[0].name, "h");
    EXPECT_EQ(res.output->measurements[1].name, "w");
}

// 알 수 없는 mode → Fail
TEST(CompareToolTest, UnknownModeFails) {
    auto d = wrapMeasurements({makeMeas("h", 3.0)});
    CompareParams p; p.mode = "nonsense";
    EXPECT_EQ(CompareTool(p).execute(d).status, ToolStatus::Fail);
}

// ── CombineDecisionTool: all 모드 ─────────────────────────────────────────

TEST(CombineDecisionTest, NoInputFails) {
    auto d = std::make_shared<VisionData>();
    EXPECT_EQ(CombineDecisionTool().execute(d).status, ToolStatus::Fail);
}

TEST(CombineDecisionTest, AllPass_WhenAllPass) {
    auto d = wrapDecisions({makeDec("a", true), makeDec("b", true)});
    auto res = CombineDecisionTool().execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->decisions.size(), 1u);
    EXPECT_TRUE(res.output->decisions[0].pass);
}

TEST(CombineDecisionTest, AllFail_WhenAnyFails) {
    auto d = wrapDecisions({makeDec("a", true), makeDec("b", false)});
    CombineDecisionParams p; p.mode = "all";
    auto res = CombineDecisionTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_FALSE(res.output->decisions[0].pass);
}

// ── CombineDecisionTool: any 모드 ─────────────────────────────────────────

TEST(CombineDecisionTest, AnyPass_WhenAtLeastOnePass) {
    auto d = wrapDecisions({makeDec("a", false), makeDec("b", true)});
    CombineDecisionParams p; p.mode = "any";
    auto res = CombineDecisionTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(res.output->decisions[0].pass);
}

TEST(CombineDecisionTest, AnyFail_WhenNonePass) {
    auto d = wrapDecisions({makeDec("a", false), makeDec("b", false)});
    CombineDecisionParams p; p.mode = "any";
    auto res = CombineDecisionTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_FALSE(res.output->decisions[0].pass);
}

// ── CombineDecisionTool: count 모드 ──────────────────────────────────────

TEST(CombineDecisionTest, CountPass) {
    auto d = wrapDecisions({makeDec("a", true), makeDec("b", true), makeDec("c", false)});
    CombineDecisionParams p; p.mode = "count"; p.count = 2;
    EXPECT_TRUE(CombineDecisionTool(p).execute(d).output->decisions[0].pass);
}

TEST(CombineDecisionTest, CountFail) {
    auto d = wrapDecisions({makeDec("a", true), makeDec("b", false), makeDec("c", false)});
    CombineDecisionParams p; p.mode = "count"; p.count = 2;
    EXPECT_FALSE(CombineDecisionTool(p).execute(d).output->decisions[0].pass);
}

// ── CombineDecisionTool: 출력 이름 ───────────────────────────────────────

TEST(CombineDecisionTest, OutputName) {
    auto d = wrapDecisions({makeDec("x", true)});
    CombineDecisionParams p; p.name = "final";
    auto res = CombineDecisionTool(p).execute(d);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->decisions[0].name, "final");
}

// measured = 통과 개수
TEST(CombineDecisionTest, MeasuredIsPassCount) {
    auto d = wrapDecisions({makeDec("a", true), makeDec("b", true), makeDec("c", false)});
    CombineDecisionParams p; p.mode = "any";
    auto res = CombineDecisionTool(p).execute(d);
    EXPECT_DOUBLE_EQ(res.output->decisions[0].measured, 2.0);  // 2개 통과
}

// 알 수 없는 mode → Fail
TEST(CombineDecisionTest, UnknownModeFails) {
    auto d = wrapDecisions({makeDec("x", true)});
    CombineDecisionParams p; p.mode = "nope";
    EXPECT_EQ(CombineDecisionTool(p).execute(d).status, ToolStatus::Fail);
}
