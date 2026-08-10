#include <gtest/gtest.h>
#include "ExtractProfileTool.h"
#include "VisionData.h"
#include "Region.h"
#include "SyntheticFixtures.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

static std::shared_ptr<HeightMap> makeHMShared(HeightMap hm) {
    return std::make_shared<HeightMap>(std::move(hm));
}

// ── 기본 ─────────────────────────────────────────────────────────────────

// axisX는 HeightMap 없으면 Fail
TEST(ExtractProfileTest, NoHeightMapFails) {
    auto d = std::make_shared<VisionData>();
    ExtractProfileTool tool;
    auto res = tool.execute(d);
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// axisX: 행 그대로 추출 — 원본 행과 bit-identical (보간 없음)
TEST(ExtractProfileTest, AxisXBitIdentical) {
    auto raw = makeTiltedPlane(40, 30, 0.01, 0.02, 1.0);
    auto hm  = makeHMShared(raw);
    const int row = 10;

    ExtractProfileParams p; p.mode = "axisX"; p.index = row;
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->profiles.size(), 1u);
    const auto& prof = *res.output->profiles[0];

    ASSERT_EQ(static_cast<int>(prof.size()), raw.width);
    for (int col = 0; col < raw.width; ++col) {
        const float rawVal = raw.rawAt(col, row);
        const double expected_z = (rawVal - raw.zZeroCount) * raw.zResMm;
        // bit-identical: 같은 float → double 변환이므로 exact
        EXPECT_DOUBLE_EQ(prof.z[col], expected_z) << "col=" << col;
    }
}

// axisY: 열 그대로 추출 — 원본 열과 bit-identical
TEST(ExtractProfileTest, AxisYBitIdentical) {
    auto raw = makeTiltedPlane(30, 40, 0.005, 0.015, 2.0);
    auto hm  = makeHMShared(raw);
    const int col = 15;

    ExtractProfileParams p; p.mode = "axisY"; p.index = col;
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& prof = *res.output->profiles[0];
    ASSERT_EQ(static_cast<int>(prof.size()), raw.height);
    for (int row = 0; row < raw.height; ++row) {
        double expected_z = (raw.rawAt(col, row) - raw.zZeroCount) * raw.zResMm;
        EXPECT_DOUBLE_EQ(prof.z[row], expected_z) << "row=" << row;
    }
}

// s[0]=0, 단조 증가
TEST(ExtractProfileTest, SArcLengthMonotone) {
    auto hm = makeHMShared(makeTiltedPlane(20, 10, 0, 0, 1.0));
    ExtractProfileTool tool;
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& s = res.output->profiles[0]->s;
    EXPECT_NEAR(s[0], 0.0, 1e-12);
    for (std::size_t i = 1; i < s.size(); ++i)
        EXPECT_GT(s[i], s[i-1]) << "i=" << i;
}

// frameId 복사
TEST(ExtractProfileTest, FrameIdPropagated) {
    auto raw = makeTiltedPlane(10, 10, 0, 0, 0);
    raw.frameId = "test_frame";
    auto hm = makeHMShared(raw);
    ExtractProfileTool tool;
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->profiles[0]->frameId, "test_frame");
}

// x, y는 mm 좌표 (xMm/yMm)
TEST(ExtractProfileTest, PhysicalCoordMm) {
    auto raw = makeTiltedPlane(10, 10, 0, 0, 0, 0.5f, 0.3f);
    auto hm  = makeHMShared(raw);
    ExtractProfileParams p; p.mode = "axisX"; p.index = 3;
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& prof = *res.output->profiles[0];
    for (int col = 0; col < raw.width; ++col) {
        // HeightMap::xMm/yMm returns float; float→double promotion is exact but
        // float(col * 0.5f) != col * 0.5 by ~1 ULP. Use 1e-6 margin.
        EXPECT_NEAR(prof.x[col], static_cast<double>(static_cast<float>(col) * 0.5f), 1e-9) << "col=" << col;
        EXPECT_NEAR(prof.y[col], static_cast<double>(3.f * 0.3f), 1e-9) << "col=" << col;
    }
}

// Region 밖 샘플은 NaN, 길이 불변
TEST(ExtractProfileTest, RegionMaskKeepsLength) {
    auto raw = makeTiltedPlane(20, 10, 0, 0, 5.0);
    auto hm  = makeHMShared(raw);
    // Region: col 5~14만 내부
    auto rg = std::make_shared<Region>();
    rg->width = raw.width; rg->height = raw.height;
    rg->mask.assign(static_cast<size_t>(raw.width) * raw.height, 0);
    for (int r = 0; r < raw.height; ++r)
        for (int c = 5; c < 15; ++c)
            rg->mask[r * raw.width + c] = 1;

    ExtractProfileParams p; p.mode = "axisX"; p.index = 3;
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& prof = *res.output->profiles[0];
    ASSERT_EQ(static_cast<int>(prof.size()), raw.width);   // 길이 불변
    for (int col = 0; col < 5; ++col)
        EXPECT_TRUE(std::isnan(prof.z[col])) << "col=" << col;
    for (int col = 5; col < 15; ++col)
        EXPECT_FALSE(std::isnan(prof.z[col])) << "col=" << col;
    for (int col = 15; col < raw.width; ++col)
        EXPECT_TRUE(std::isnan(prof.z[col])) << "col=" << col;
}

// span>1: 이웃 평균 (상수 HM에서 변화 없음)
TEST(ExtractProfileTest, SpanAverageConstantHM) {
    auto raw = makeTiltedPlane(20, 10, 0, 0, 3.0);
    auto hm  = makeHMShared(raw);
    ExtractProfileParams p; p.mode = "axisX"; p.index = 4; p.span = 3;
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& prof = *res.output->profiles[0];
    for (auto z : prof.z)
        EXPECT_NEAR(z, 3.0, 1e-6) << "span 평균이 상수를 보존해야 함";
}

// 멱등성: 같은 입력 반복 실행 시 동일 출력
TEST(ExtractProfileTest, Idempotent) {
    auto hm = makeHMShared(makeTiltedPlane(15, 8, 0.01, 0.02, 1.5));
    ExtractProfileTool tool;
    auto in = makeInputHM(hm);
    auto r1 = tool.execute(in);
    auto r2 = tool.execute(in);
    ASSERT_EQ(r1.status, ToolStatus::Ok);
    ASSERT_EQ(r2.status, ToolStatus::Ok);
    const auto& z1 = r1.output->profiles[0]->z;
    const auto& z2 = r2.output->profiles[0]->z;
    ASSERT_EQ(z1.size(), z2.size());
    for (std::size_t i = 0; i < z1.size(); ++i)
        EXPECT_DOUBLE_EQ(z1[i], z2[i]) << "i=" << i;
}

// ── Phase 4: line 모드 ────────────────────────────────────────────────────

// 축평행 line(nearest) == axisX 결과 (bit-identical)
TEST(ExtractProfileTest, LineAxisParallelMatchesAxisX) {
    auto raw = makeTiltedPlane(20, 10, 0.01, 0.02, 1.0);
    auto hm  = makeHMShared(raw);
    const int row = 3;
    const int W   = raw.width;

    ExtractProfileParams pAx; pAx.mode = "axisX"; pAx.index = row;
    ExtractProfileTool axTool(pAx);
    auto axRes = axTool.execute(makeInputHM(hm));
    ASSERT_EQ(axRes.status, ToolStatus::Ok);
    const auto& axProf = *axRes.output->profiles[0];

    ExtractProfileParams pLn;
    pLn.mode = "line"; pLn.interp = "nearest"; pLn.unit = "mm"; pLn.count = W;
    pLn.p0x = 0.0;
    pLn.p0y = static_cast<double>(row) * raw.yResMm;
    pLn.p1x = static_cast<double>(W - 1) * raw.xResMm;
    pLn.p1y = static_cast<double>(row) * raw.yResMm;
    ExtractProfileTool lnTool(pLn);
    auto lnRes = lnTool.execute(makeInputHM(hm));
    ASSERT_EQ(lnRes.status, ToolStatus::Ok);
    const auto& lnProf = *lnRes.output->profiles[0];

    ASSERT_EQ(lnProf.size(), axProf.size());
    for (std::size_t i = 0; i < lnProf.size(); ++i)
        EXPECT_DOUBLE_EQ(lnProf.z[i], axProf.z[i]) << "i=" << i;
}

// 45° 경로, bilinear, 기울어진 평면 — 해석적 정답
TEST(ExtractProfileTest, Line45DegBilinearAnalytical) {
    // z(x,y) = a*x + b*y + c. bilinear은 선형 함수에서 exact.
    const double a = 1.0, b = 0.5, c = 2.0;
    auto raw = makeTiltedPlane(30, 30, a, b, c, 0.1f, 0.1f, 0.001f);
    auto hm  = makeHMShared(raw);

    // p0=(0,0)mm, p1=(1,1)mm, 11 samples
    ExtractProfileParams p;
    p.mode = "line"; p.p0x = 0; p.p0y = 0; p.p1x = 1.0; p.p1y = 1.0;
    p.unit = "mm"; p.count = 11; p.interp = "bilinear";
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& prof = *res.output->profiles[0];
    ASSERT_EQ(prof.size(), 11u);

    const double expectedLen = std::sqrt(1.0*1.0 + 1.0*1.0);
    for (std::size_t i = 0; i < prof.size(); ++i) {
        const double t    = static_cast<double>(i) / 10.0;
        const double xMm  = t * 1.0, yMm = t * 1.0;
        EXPECT_NEAR(prof.z[i], a * xMm + b * yMm + c, 1e-4) << "i=" << i;
        EXPECT_NEAR(prof.s[i], t * expectedLen, 1e-10) << "s i=" << i;
    }
}

// unit=px 좌표 — 동일 픽셀 위치 접근
TEST(ExtractProfileTest, LineUnitPxFlat) {
    auto raw = makeTiltedPlane(10, 10, 0, 0, 5.0);
    auto hm  = makeHMShared(raw);
    ExtractProfileParams p;
    p.mode = "line"; p.p0x = 0; p.p0y = 2; p.p1x = 9; p.p1y = 2;
    p.unit = "px"; p.count = 10; p.interp = "nearest";
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& prof = *res.output->profiles[0];
    ASSERT_EQ(prof.size(), 10u);
    for (auto z : prof.z)
        EXPECT_NEAR(z, 5.0, 1e-5);
}

// 경계 밖 샘플 NaN, 경계 안 샘플 유효 (nearest)
TEST(ExtractProfileTest, LineOobIsNaN) {
    auto hm = makeHMShared(makeTiltedPlane(10, 10, 0, 0, 3.0));
    ExtractProfileParams p;
    p.mode = "line"; p.p0x = -2; p.p0y = 5; p.p1x = 11; p.p1y = 5;
    p.unit = "px"; p.count = 14; p.interp = "nearest";
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& prof = *res.output->profiles[0];
    ASSERT_EQ(prof.size(), 14u);
    // px i=0→-2, i=1→-1 (OOB), i=2→0..i=11→9 (valid), i=12→10, i=13→11 (OOB)
    EXPECT_TRUE(std::isnan(prof.z[0]));
    EXPECT_TRUE(std::isnan(prof.z[1]));
    EXPECT_FALSE(std::isnan(prof.z[2]));
    EXPECT_FALSE(std::isnan(prof.z[11]));
    EXPECT_TRUE(std::isnan(prof.z[12]));
    EXPECT_TRUE(std::isnan(prof.z[13]));
}

// s[0]=0, 단조 증가 (line 모드)
TEST(ExtractProfileTest, LineSArcLengthMonotone) {
    auto hm = makeHMShared(makeTiltedPlane(20, 10, 0, 0, 1.0));
    ExtractProfileParams p;
    p.mode = "line"; p.p0x = 0; p.p0y = 0; p.p1x = 0.5; p.p1y = 0;
    p.unit = "mm"; p.count = 6; p.interp = "nearest";
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const auto& s = res.output->profiles[0]->s;
    EXPECT_NEAR(s[0], 0.0, 1e-12);
    for (std::size_t i = 1; i < s.size(); ++i)
        EXPECT_GT(s[i], s[i-1]) << "i=" << i;
}

// count=0 자동: lenPx 기준 최소 1 샘플
TEST(ExtractProfileTest, LineCountAutoNonZero) {
    auto hm = makeHMShared(makeTiltedPlane(20, 10, 0, 0, 1.0));
    ExtractProfileParams p;
    p.mode = "line"; p.p0x = 0; p.p0y = 0; p.p1x = 5; p.p1y = 0;
    p.unit = "px"; p.count = 0; p.interp = "nearest";
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_GT(res.output->profiles[0]->size(), 0u);
}

// bilinear: NaN 이웃이 있으면 해당 샘플 NaN
TEST(ExtractProfileTest, BilinearNaNNeighborYieldsNaN) {
    auto raw = makeTiltedPlane(5, 5, 0, 0, 1.0);
    // (1,1)에 NaN 주입
    raw.data[1 * raw.width + 1] = std::numeric_limits<float>::quiet_NaN();
    auto hm = makeHMShared(raw);

    // 샘플 위치 px=0.5, py=0.5 → bilinear 이웃 (0,0),(1,0),(0,1),(1,1). (1,1)=NaN → 결과 NaN
    ExtractProfileParams p;
    p.mode = "line"; p.p0x = 0.5; p.p0y = 0.5; p.p1x = 0.5; p.p1y = 0.5;
    p.unit = "px"; p.count = 1; p.interp = "bilinear";
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(std::isnan(res.output->profiles[0]->z[0]));
}

// frameId line 모드에서도 복사
TEST(ExtractProfileTest, LineFrameIdPropagated) {
    auto raw = makeTiltedPlane(10, 10, 0, 0, 0);
    raw.frameId = "frame_line";
    auto hm = makeHMShared(raw);
    ExtractProfileParams p;
    p.mode = "line"; p.p0x = 0; p.p0y = 0; p.p1x = 5; p.p1y = 0;
    p.unit = "px"; p.count = 5; p.interp = "nearest";
    ExtractProfileTool tool(p);
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->profiles[0]->frameId, "frame_line");
}

// Region frameId 불일치 → Fail
TEST(ExtractProfileTest, RegionFrameMismatchFails) {
    auto raw = makeTiltedPlane(10, 10, 0, 0, 0);
    raw.frameId = "hmFrame";
    auto hm = makeHMShared(raw);
    auto rg = std::make_shared<Region>();
    rg->width = 10; rg->height = 10;
    rg->mask.assign(100, 1);
    rg->frameId = "otherFrame";
    ExtractProfileTool tool;
    auto res = tool.execute(makeInputHMRegion(hm, rg));
    EXPECT_EQ(res.status, ToolStatus::Fail);
}
