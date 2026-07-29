#include <gtest/gtest.h>
#include "Broadcast.h"

using namespace vision;

// 배열 입력 없음 → 1회 실행
TEST(Broadcast, NoArrayInputs) {
    auto p = computeBroadcast({});
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 1u);
}

// 전부 길이 1 → 1회 실행
TEST(Broadcast, AllScalarLength1) {
    auto p = computeBroadcast({1, 1, 1});
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 1u);
}

// 길이 N 배열 → N회
TEST(Broadcast, SingleArrayLengthN) {
    auto p = computeBroadcast({3});
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 3u);
}

// 길이 1은 N으로 확장 (3 + 1 → 3)
TEST(Broadcast, Length1Expands) {
    auto p = computeBroadcast({3, 1});
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 3u);

    auto p2 = computeBroadcast({1, 5, 1});
    EXPECT_TRUE(p2.ok);
    EXPECT_EQ(p2.count, 5u);
}

// 같은 다중 길이 여러 개 → 허용
TEST(Broadcast, MatchingLengths) {
    auto p = computeBroadcast({4, 4, 4});
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 4u);
}

// 서로 다른 다중 길이 → Fail
TEST(Broadcast, MismatchFails) {
    auto p = computeBroadcast({3, 5});
    EXPECT_FALSE(p.ok);
    EXPECT_EQ(p.count, 0u);
}

// 길이 0 배열 → 0회 실행(빈 출력)
TEST(Broadcast, ZeroLength) {
    auto p = computeBroadcast({0});
    EXPECT_TRUE(p.ok);
    EXPECT_EQ(p.count, 0u);
}
