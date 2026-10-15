#include <gtest/gtest.h>
#include "Aggregate.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace vision::agg;

// ── 헬퍼 ──────────────────────────────────────────────────────────────────
static Result call(Result(*fn)(const double*, std::size_t), const std::vector<double>& v) {
    return fn(v.data(), v.size());
}

// ── mean ──────────────────────────────────────────────────────────────────
TEST(AggregateTest, MeanBasic) {
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto r = call(mean, v);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 5u);
    EXPECT_NEAR(r.value, 3.0, 1e-12);
}
TEST(AggregateTest, MeanSkipsNaN) {
    double NaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> v = {1.0, NaN, 3.0, NaN, 5.0};
    auto r = call(mean, v);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 3u);
    EXPECT_NEAR(r.value, 3.0, 1e-12);
}
TEST(AggregateTest, MeanAllNaNInvalid) {
    double NaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> v = {NaN, NaN};
    EXPECT_FALSE(call(mean, v).valid);
}
TEST(AggregateTest, MeanEmpty) {
    EXPECT_FALSE(mean(nullptr, 0).valid);
}
TEST(AggregateTest, MeanSingle) {
    std::vector<double> v = {7.0};
    auto r = call(mean, v);
    ASSERT_TRUE(r.valid); EXPECT_NEAR(r.value, 7.0, 1e-12);
}

// ── median ────────────────────────────────────────────────────────────────
TEST(AggregateTest, MedianOdd) {
    std::vector<double> v = {3.0, 1.0, 2.0};
    auto r = call(median, v);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 2.0, 1e-12);
}
TEST(AggregateTest, MedianEven) {
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0};
    auto r = call(median, v);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 2.5, 1e-12);
}
TEST(AggregateTest, MedianSkipsNaN) {
    double NaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> v = {1.0, NaN, 3.0};
    auto r = call(median, v);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 2u);
    EXPECT_NEAR(r.value, 2.0, 1e-12);
}
TEST(AggregateTest, MedianAllSame) {
    std::vector<double> v = {5.0, 5.0, 5.0};
    auto r = call(median, v);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 5.0, 1e-12);
}

// ── maxV / minV ───────────────────────────────────────────────────────────
TEST(AggregateTest, MaxV) {
    std::vector<double> v = {3.0, 1.0, 4.0, 1.0, 5.0, 9.0};
    EXPECT_NEAR(call(maxV, v).value, 9.0, 1e-12);
}
TEST(AggregateTest, MinV) {
    std::vector<double> v = {3.0, 1.0, 4.0, 1.0, 5.0};
    EXPECT_NEAR(call(minV, v).value, 1.0, 1e-12);
}
TEST(AggregateTest, MaxMinAllNaN) {
    double NaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> v = {NaN, NaN};
    EXPECT_FALSE(call(maxV, v).valid);
    EXPECT_FALSE(call(minV, v).valid);
}

// ── stdDev ───────────────────────────────────────────────────────────────
TEST(AggregateTest, StdDevKnown) {
    // {2,4,4,4,5,5,7,9}: mean=5, SSE=32, 표본 σ=sqrt(32/7)≈2.1381 (n-1)
    // 모집단 σ=2.0(n)과 다름 — n-1 구현 확인용.
    std::vector<double> v = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    auto r = call(stdDev, v);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, std::sqrt(32.0 / 7.0), 1e-9);
}
TEST(AggregateTest, StdDevSingle) {
    std::vector<double> v = {3.0};
    auto r = call(stdDev, v);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 0.0, 1e-12);
}
TEST(AggregateTest, StdDevAllSame) {
    std::vector<double> v = {5.0, 5.0, 5.0, 5.0};
    EXPECT_NEAR(call(stdDev, v).value, 0.0, 1e-12);
}

// ── percentile ───────────────────────────────────────────────────────────
TEST(AggregateTest, Percentile50IsMedian) {
    // N=5, p=50 → idx=round(0.5*4)=2 → vals[2] (정렬 후)
    std::vector<double> v = {5.0, 1.0, 3.0, 2.0, 4.0};
    auto r = percentile(v.data(), v.size(), 50.0);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 3.0, 1e-12);
}
TEST(AggregateTest, Percentile0IsMin) {
    std::vector<double> v = {5.0, 1.0, 3.0};
    EXPECT_NEAR(percentile(v.data(), v.size(), 0.0).value, 1.0, 1e-12);
}
TEST(AggregateTest, Percentile100IsMax) {
    std::vector<double> v = {5.0, 1.0, 3.0};
    EXPECT_NEAR(percentile(v.data(), v.size(), 100.0).value, 5.0, 1e-12);
}

// ── highTail ─────────────────────────────────────────────────────────────
// 정답은 구 HeightFromPlaneTool 알고리즘을 수작업으로 계산한 독립 상수.
// v = {1,2,3,4,5} (오름차순), N=5

TEST(AggregateTest, HighTail20pct) {
    // pct=20 → n_top=max(1,ceil(5*0.2))=max(1,1)=1 → top1={5} → avg=5.0
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto r = highTail(v.data(), v.size(), 20.0);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 1u);
    EXPECT_NEAR(r.value, 5.0, 1e-12);
}
TEST(AggregateTest, HighTail40pct) {
    // pct=40 → ceil(5*0.4)=ceil(2)=2 → top2={4,5} → avg=4.5
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto r = highTail(v.data(), v.size(), 40.0);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 2u);
    EXPECT_NEAR(r.value, 4.5, 1e-12);
}
TEST(AggregateTest, HighTail100pct) {
    // pct=100 → n_top=5 → avg=(1+2+3+4+5)/5=3.0
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto r = highTail(v.data(), v.size(), 100.0);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 5u);
    EXPECT_NEAR(r.value, 3.0, 1e-12);
}
TEST(AggregateTest, HighTailCeilBoundary) {
    // N=10, pct=20 → ceil(10*0.2)=ceil(2)=2 → top2={9,10} → avg=9.5
    std::vector<double> v = {1,2,3,4,5,6,7,8,9,10};
    auto r = highTail(v.data(), v.size(), 20.0);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 2u);
    EXPECT_NEAR(r.value, 9.5, 1e-12);
}
TEST(AggregateTest, HighTailSkipsNaN) {
    double NaN = std::numeric_limits<double>::quiet_NaN();
    // 유효: {1,2,3,4,5}, pct=20 → top1={5}=5.0
    std::vector<double> v = {NaN, 1.0, 2.0, NaN, 3.0, 4.0, 5.0};
    auto r = highTail(v.data(), v.size(), 20.0);
    ASSERT_TRUE(r.valid); EXPECT_EQ(r.n, 1u);
    EXPECT_NEAR(r.value, 5.0, 1e-12);
}
TEST(AggregateTest, HighTailAllNaNInvalid) {
    double NaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> v = {NaN, NaN, NaN};
    EXPECT_FALSE(highTail(v.data(), v.size(), 20.0).valid);
}
TEST(AggregateTest, HighTailSingle) {
    // N=1, pct=20 → n_top=max(1,ceil(0.2))=1 → top1={42}=42.0
    std::vector<double> v = {42.0};
    auto r = highTail(v.data(), v.size(), 20.0);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 42.0, 1e-12);
}
TEST(AggregateTest, HighTailAllSame) {
    // N=5, 전부 7.0, pct=20 → top1=7.0
    std::vector<double> v = {7.0, 7.0, 7.0, 7.0, 7.0};
    auto r = highTail(v.data(), v.size(), 20.0);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.value, 7.0, 1e-12);
}
