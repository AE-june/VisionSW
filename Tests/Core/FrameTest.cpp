#include <gtest/gtest.h>
#include "Frame.h"

using namespace vision;

static constexpr double kEps = 1e-10;

// ── Transform2D helpers ──────────────────────────────────────────────────────

TEST(Transform2D, IdentityApply) {
    auto t = Transform2D::identity();
    double x = 3.0, y = 4.0;
    t.apply(x, y);
    EXPECT_NEAR(x, 3.0, kEps);
    EXPECT_NEAR(y, 4.0, kEps);
    EXPECT_NEAR(t.applyZ(5.0), 5.0, kEps);
}

TEST(Transform2D, PureTranslation) {
    Transform2D t;
    t.tx = 1.0; t.ty = 2.0; t.tz = 3.0;
    double x = 0.0, y = 0.0;
    t.apply(x, y);
    EXPECT_NEAR(x, 1.0, kEps);
    EXPECT_NEAR(y, 2.0, kEps);
    EXPECT_NEAR(t.applyZ(0.0), 3.0, kEps);
}

TEST(Transform2D, Rotation90) {
    Transform2D t;
    t.angleDeg = 90.0;
    double x = 1.0, y = 0.0;
    t.apply(x, y);
    EXPECT_NEAR(x, 0.0, 1e-9);
    EXPECT_NEAR(y, 1.0, 1e-9);
}

TEST(Transform2D, InverseRoundtrip) {
    Transform2D t;
    t.angleDeg = 37.0; t.tx = 5.0; t.ty = -3.0; t.tz = 1.5;
    auto inv = t.inverse();
    auto composed = t.then(inv);  // should be identity
    EXPECT_NEAR(composed.angleDeg, 0.0, 1e-9);
    EXPECT_NEAR(composed.tx, 0.0, 1e-9);
    EXPECT_NEAR(composed.ty, 0.0, 1e-9);
    EXPECT_NEAR(composed.tz, 0.0, 1e-9);
}

TEST(Transform2D, ThenCompose) {
    // Translate by (1,0) then rotate 90° around origin
    Transform2D t1, t2;
    t1.tx = 1.0;
    t2.angleDeg = 90.0;
    auto composed = t1.then(t2);
    double x = 0.0, y = 0.0;
    composed.apply(x, y);
    // (0,0) → t1 → (1,0) → t2(rot90) → (0,1)
    EXPECT_NEAR(x, 0.0, 1e-9);
    EXPECT_NEAR(y, 1.0, 1e-9);
}

// ── FrameRegistry — identity ─────────────────────────────────────────────────

TEST(FrameRegistry, SameFrameIsIdentity) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});
    Transform2D t;
    ASSERT_TRUE(reg.transform("world", "world", t));
    EXPECT_NEAR(t.angleDeg, 0.0, kEps);
    EXPECT_NEAR(t.tx, 0.0, kEps);
    EXPECT_NEAR(t.ty, 0.0, kEps);
}

// ── 1단 부모-자식 ─────────────────────────────────────────────────────────────

TEST(FrameRegistry, OneStep) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});
    Transform2D toParent;
    toParent.tx = 10.0;
    reg.define(Frame{"child", "world", toParent});

    Transform2D t;
    ASSERT_TRUE(reg.transform("child", "world", t));
    EXPECT_NEAR(t.tx, 10.0, kEps);
    EXPECT_NEAR(t.ty, 0.0, kEps);
    EXPECT_NEAR(t.angleDeg, 0.0, kEps);
}

TEST(FrameRegistry, OneStepInverse) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});
    Transform2D toParent;
    toParent.tx = 10.0;
    reg.define(Frame{"child", "world", toParent});

    Transform2D t;
    ASSERT_TRUE(reg.transform("world", "child", t));
    EXPECT_NEAR(t.tx, -10.0, kEps);
}

// ── 2단 합성 ──────────────────────────────────────────────────────────────────

TEST(FrameRegistry, TwoStepCompose) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});

    Transform2D t1; t1.tx = 5.0;
    reg.define(Frame{"mid", "world", t1});

    Transform2D t2; t2.ty = 3.0;
    reg.define(Frame{"leaf", "mid", t2});

    Transform2D t;
    ASSERT_TRUE(reg.transform("leaf", "world", t));
    EXPECT_NEAR(t.tx, 5.0, kEps);
    EXPECT_NEAR(t.ty, 3.0, kEps);
}

// ── 형제 간 변환 ──────────────────────────────────────────────────────────────

TEST(FrameRegistry, SiblingTransform) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});

    Transform2D tA; tA.tx = 10.0;
    reg.define(Frame{"A", "world", tA});

    Transform2D tB; tB.ty = 5.0;
    reg.define(Frame{"B", "world", tB});

    // A → B: go A→world (tx=10), then world→B (inverse of tB = ty=-5)
    Transform2D t;
    ASSERT_TRUE(reg.transform("A", "B", t));
    // point (0,0) in A → world is (10, 0) → B: world→B = B.toParent.inverse() = ty=-5
    // so (10, 0) in world → (10, 0-5) = (10, -5) in B
    double x = 0.0, y = 0.0;
    t.apply(x, y);
    EXPECT_NEAR(x, 10.0, 1e-9);
    EXPECT_NEAR(y, -5.0, 1e-9);
}

// ── 사이클 거부 ───────────────────────────────────────────────────────────────

TEST(FrameRegistry, CycleRejected) {
    FrameRegistry reg;
    reg.define(Frame{"A", "B", Transform2D::identity()});  // B not defined yet
    reg.define(Frame{"B", "A", Transform2D::identity()});  // would create A→B→A cycle
    // B should be rejected since its parent A has B in its chain
    // After rejection, B is not defined
    EXPECT_FALSE(reg.exists("B"));
}

// ── 미정의 id 실패 ────────────────────────────────────────────────────────────

TEST(FrameRegistry, UndefinedFrameFails) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});
    Transform2D t;
    EXPECT_FALSE(reg.transform("ghost", "world", t));
    EXPECT_FALSE(reg.transform("world", "ghost", t));
}

// ── inverse·then 왕복 ─────────────────────────────────────────────────────────

TEST(FrameRegistry, InverseThenRoundtrip) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});
    Transform2D tp; tp.angleDeg = 45.0; tp.tx = 3.0; tp.ty = 1.0;
    reg.define(Frame{"sensor", "world", tp});

    Transform2D fwd, bwd;
    ASSERT_TRUE(reg.transform("sensor", "world", fwd));
    ASSERT_TRUE(reg.transform("world", "sensor", bwd));

    // fwd.then(bwd) should be identity
    auto combined = fwd.then(bwd);
    EXPECT_NEAR(combined.angleDeg, 0.0, 1e-9);
    EXPECT_NEAR(combined.tx, 0.0, 1e-9);
    EXPECT_NEAR(combined.ty, 0.0, 1e-9);
}

// ── compatible ────────────────────────────────────────────────────────────────

TEST(FrameRegistry, Compatible) {
    FrameRegistry reg;
    reg.define(Frame{"world", "", Transform2D::identity()});
    Transform2D t; t.tx = 1.0;
    reg.define(Frame{"cam", "world", t});
    EXPECT_TRUE(reg.compatible("cam", "world"));
    EXPECT_FALSE(reg.compatible("cam", "other"));
}
