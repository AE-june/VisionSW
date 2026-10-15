#include <gtest/gtest.h>
#include "SyntheticFixtures.h"
#include "LevelTool.h"
#include "SurfaceCropTool.h"
#include "SurfaceResampleTool.h"
#include "ValidRegionTool.h"
#include "VisionData.h"
#include "Frame.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

// ── 헬퍼 ────────────────────────────────────────────────────────────────

static VisionDataPtr wrapHM(HeightMap hm) {
    return makeInputHM(std::make_shared<HeightMap>(std::move(hm)));
}

static VisionDataPtr wrapHMWithPlane(HeightMap hm, PlaneModel pm) {
    return makeInputHMPlane(
        std::make_shared<HeightMap>(std::move(hm)),
        std::make_shared<PlaneModel>(std::move(pm)));
}

// ── V2-1: 크롭 전후 같은 물리점의 xMm()/yMm() 동일 ──────────────────────

// 허용오차: float 산술 ≤ 1e-6 mm (xResMm*originCol 계산 오차)
TEST(InvariantTest, SurfaceCrop_PhysicalCoordPreserved) {
    // originCol=5, originRow=3, xResMm=0.05, yResMm=0.05
    HeightMap hm = makeBlank(20, 15, 0.05f, 0.05f, 0.001f);
    hm.originCol = 5.f;
    hm.originRow = 3.f;

    // 물리점: input의 (col=7, row=5)
    const float before_x = hm.xMm(7);  // (7-5)*0.05 = 0.1 mm
    const float before_y = hm.yMm(5);  // (5-3)*0.05 = 0.1 mm

    auto in = wrapHM(hm);
    SurfaceCropParams p;
    p.mode   = "rect";
    p.rect_x = 2; p.rect_y = 1;
    p.rect_w = 12; p.rect_h = 10;
    SurfaceCropTool tool(p);
    auto res = tool.execute(in);
    ASSERT_EQ(res.status, ToolStatus::Ok);

    // crop 후 같은 물리점: col=7-2=5, row=5-1=4
    const HeightMap& out = *res.output->heightmap0();
    const float after_x = out.xMm(5);
    const float after_y = out.yMm(4);

    EXPECT_NEAR(after_x, before_x, 1e-6f);
    EXPECT_NEAR(after_y, before_y, 1e-6f);
}

// ── V2-2: 리샘플 전후 같은 물리점의 xMm()/yMm() 동일 ────────────────────

// 허용오차: float 산술 ≤ 1e-6 mm
TEST(InvariantTest, SurfaceResample_PhysicalCoordPreserved) {
    HeightMap hm = makeBlank(20, 16, 0.05f, 0.05f, 0.001f);
    hm.originCol = 10.f;
    hm.originRow = 8.f;

    // 물리점: col=4, row=4 → factor=2 후 col=2, row=2
    const float before_x = hm.xMm(4);  // (4-10)*0.05 = -0.3 mm
    const float before_y = hm.yMm(4);  // (4-8)*0.05  = -0.2 mm

    auto in = wrapHM(hm);
    SurfaceResampleParams p;
    p.mode   = "factor";
    p.factor = 2;
    SurfaceResampleTool tool(p);
    auto res = tool.execute(in);
    ASSERT_EQ(res.status, ToolStatus::Ok);

    const HeightMap& out = *res.output->heightmap0();
    // factor=2: originCol=10/2=5, xResMm=0.1 → xMm(2) = (2-5)*0.1 = -0.3
    const float after_x = out.xMm(2);
    const float after_y = out.yMm(2);

    EXPECT_NEAR(after_x, before_x, 1e-6f);
    EXPECT_NEAR(after_y, before_y, 1e-6f);
}

// ── V2-3: FrameRegistry 왕복 F1→F2→F1 = 원본 ────────────────────────────

// 허용오차: 부동소수 합성 ≤ 1e-9 (rigid 변환의 역행렬)
TEST(InvariantTest, FrameRegistry_RoundTrip) {
    FrameRegistry reg;
    reg.define(Frame{frames::kWorld, "", Transform2D::identity()});

    Transform2D t;
    t.angleDeg = 30.0;
    t.tx = 5.0; t.ty = -3.0; t.tz = 0.1;
    reg.define(Frame{"child", frames::kWorld, t});

    Transform2D fwd, bwd;
    ASSERT_TRUE(reg.transform("child", frames::kWorld, fwd));
    ASSERT_TRUE(reg.transform(frames::kWorld, "child", bwd));

    Transform2D roundtrip = fwd.then(bwd);
    EXPECT_NEAR(roundtrip.angleDeg, 0.0, 1e-9);
    EXPECT_NEAR(roundtrip.tx,       0.0, 1e-9);
    EXPECT_NEAR(roundtrip.ty,       0.0, 1e-9);
    EXPECT_NEAR(roundtrip.tz,       0.0, 1e-9);
}

// ── V2-4: Level(distance) 출력 평균 == 직접 계산한 수직거리 평균 ──────────

// 평면 z=c_ref (a=0, b=0) + 입력이 z=z0이면 distance = (z0-c_ref)/sqrt(1) = z0-c_ref
// 독립 경로: 상수 = 10.0 - 8.0 = 2.0 mm (구현식을 재현하지 않음)
// 허용오차: zResMm/2 = 0.0005 mm (float 격자 왕복 오차)
TEST(InvariantTest, LevelDistance_AvgMatchesDirectCalc) {
    const float zRes = 0.001f;
    // 입력: 전부 z=10 mm (flat), zZeroCount=0 → raw=10/0.001=10000
    HeightMap hm = makeBlank(10, 10, 0.05f, 0.05f, zRes);
    const float rawFor10mm = 10.0f / zRes;  // 10000
    for (auto& v : hm.data) v = rawFor10mm;

    PlaneModel pm;
    pm.a = 0.0; pm.b = 0.0; pm.c = 8.0;  // 기준 평면 z=8mm
    pm.valid = true;

    auto in = wrapHMWithPlane(hm, pm);
    LevelParams lp;
    lp.mode       = "distance";
    lp.keepInvalid = true;
    lp.offsetMm   = 0.0;
    LevelTool tool(lp);
    auto res = tool.execute(in);
    ASSERT_EQ(res.status, ToolStatus::Ok);

    // 전체 픽셀 평균 계산
    const HeightMap& out = *res.output->heightmap0();
    double sum = 0.0;
    for (int r = 0; r < out.height; ++r)
        for (int c = 0; c < out.width; ++c)
            sum += (out.rawAt(c, r) - out.zZeroCount) * out.zResMm;
    double avg = sum / (out.width * out.height);

    // 독립 경로 정답: (10 - 8) / sqrt(1 + 0 + 0) = 2.0 mm
    const double expected = 2.0;
    EXPECT_NEAR(avg, expected, zRes / 2.0);
}

// ── V2-5: NaN 전파 정책 (keepInvalid=true) ───────────────────────────────

// NaN 입력 → NaN 출력 (전파 정책이 propagate=keepInvalid=true일 때)
TEST(InvariantTest, Level_NaN_Propagates_WhenKeepInvalid) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    HeightMap hm    = makeBlank(4, 4, 0.05f, 0.05f, 0.001f);
    const float raw10mm = 10.0f / 0.001f;
    for (auto& v : hm.data) v = raw10mm;
    hm.data[1 * 4 + 2] = nan;  // (col=2, row=1) NaN 주입

    PlaneModel pm; pm.a = 0; pm.b = 0; pm.c = 5.0; pm.valid = true;
    LevelParams lp; lp.mode = "distance"; lp.keepInvalid = true;
    LevelTool tool(lp);
    auto res = tool.execute(wrapHMWithPlane(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    const HeightMap& out = *res.output->heightmap0();
    EXPECT_TRUE(std::isnan(out.rawAt(2, 1, 0)));   // NaN 전파 확인
    EXPECT_FALSE(std::isnan(out.rawAt(0, 0, 0)));  // 다른 픽셀은 유효
}

// ── V2-6: 채널0만 바꾸는 노드가 다른 채널을 bit-identical 보존 ──────────

TEST(InvariantTest, Level_PreservesOtherChannels_BitIdentical) {
    const int W = 6, H = 4;
    HeightMap hm;
    hm.width = W; hm.height = H; hm.channels = 2;
    hm.xResMm = 0.05f; hm.yResMm = 0.05f;
    hm.zResMm = 0.001f; hm.zZeroCount = 0.f;
    hm.data.resize(static_cast<size_t>(W) * H * 2);

    // ch0: z=5mm everywhere
    for (int i = 0; i < W * H; ++i)
        hm.data[i] = 5.0f / 0.001f;  // raw=5000

    // ch1: 고유 패턴 (독립 정답 — 구현과 무관한 상수)
    for (int i = 0; i < W * H; ++i)
        hm.data[W * H + i] = static_cast<float>(i * 13 + 7);

    PlaneModel pm; pm.a = 0; pm.b = 0; pm.c = 3.0; pm.valid = true;
    LevelParams lp; lp.mode = "distance"; lp.keepInvalid = true;
    LevelTool tool(lp);
    auto res = tool.execute(wrapHMWithPlane(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    const HeightMap& out = *res.output->heightmap0();
    ASSERT_EQ(out.channels, 2);
    for (int i = 0; i < W * H; ++i) {
        float expected_ch1 = static_cast<float>(i * 13 + 7);
        EXPECT_EQ(out.data[W * H + i], expected_ch1) << "ch1 at pixel " << i;
    }
}

// ── V2-7: 멱등성 — 같은 입력 + 같은 파라미터 → 같은 출력 ──────────────

// ValidRegion 멱등성: 두 번 실행해도 동일 출력
TEST(InvariantTest, ValidRegion_Idempotent) {
    HeightMap hm = makeTiltedPlane(8, 6, 0.01, 0.005, 3.0);
    injectInvalid(hm, 0.1, /*seed=*/42);

    auto in = wrapHM(hm);
    ValidRegionTool tool;
    auto res1 = tool.execute(in);
    auto res2 = tool.execute(in);
    ASSERT_EQ(res1.status, ToolStatus::Ok);
    ASSERT_EQ(res2.status, ToolStatus::Ok);

    const Region& r1 = *res1.output->region0();
    const Region& r2 = *res2.output->region0();
    ASSERT_EQ(r1.mask.size(), r2.mask.size());
    for (std::size_t i = 0; i < r1.mask.size(); ++i)
        EXPECT_EQ(r1.mask[i], r2.mask[i]) << "mask diff at pixel " << i;
}

// SurfaceCrop 멱등성
TEST(InvariantTest, SurfaceCrop_Idempotent) {
    HeightMap hm = makeStep(12, 10, /*stepCol=*/6, 0.0, 2.5);
    auto in = wrapHM(hm);
    SurfaceCropParams p; p.mode = "rect"; p.rect_x = 1; p.rect_y = 1;
    p.rect_w = 8; p.rect_h = 6;
    SurfaceCropTool tool(p);
    auto res1 = tool.execute(in);
    auto res2 = tool.execute(in);
    ASSERT_EQ(res1.status, ToolStatus::Ok);
    ASSERT_EQ(res2.status, ToolStatus::Ok);
    const HeightMap& o1 = *res1.output->heightmap0();
    const HeightMap& o2 = *res2.output->heightmap0();
    ASSERT_EQ(o1.data.size(), o2.data.size());
    for (std::size_t i = 0; i < o1.data.size(); ++i) {
        if (std::isnan(o1.data[i])) EXPECT_TRUE(std::isnan(o2.data[i]));
        else EXPECT_EQ(o1.data[i], o2.data[i]);
    }
}

// SurfaceResample 멱등성
TEST(InvariantTest, SurfaceResample_Idempotent) {
    HeightMap hm = makeBumpGrid(16, 12, 2, 2, 1.0, 0.5);
    auto in = wrapHM(hm);
    SurfaceResampleParams p; p.mode = "factor"; p.factor = 2; p.method = "decimate";
    SurfaceResampleTool tool(p);
    auto res1 = tool.execute(in);
    auto res2 = tool.execute(in);
    ASSERT_EQ(res1.status, ToolStatus::Ok);
    ASSERT_EQ(res2.status, ToolStatus::Ok);
    const HeightMap& o1 = *res1.output->heightmap0();
    const HeightMap& o2 = *res2.output->heightmap0();
    ASSERT_EQ(o1.data.size(), o2.data.size());
    for (std::size_t i = 0; i < o1.data.size(); ++i) {
        if (std::isnan(o1.data[i])) EXPECT_TRUE(std::isnan(o2.data[i]));
        else EXPECT_EQ(o1.data[i], o2.data[i]);
    }
}

// ── 미구현 항목 (ExtractProfile·Collect 미완료) — 빌드만 통과, 실행 금지 ──

TEST(InvariantTest, DISABLED_ExtractProfile_BitIdentical) {
    // ExtractProfile(axisX) 미구현. Phase 4(T1-Profile) 이후 활성화.
    GTEST_SKIP() << "ExtractProfile not yet implemented";
}

TEST(InvariantTest, DISABLED_CollectionOutputOrder_Deterministic) {
    // Collect 노드 미구현. Phase 2(A3) 이후 활성화.
    GTEST_SKIP() << "Collect not yet implemented";
}
