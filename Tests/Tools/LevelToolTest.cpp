#include <gtest/gtest.h>
#include "LevelTool.h"
#include "VisionData.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

// 단순 HeightMap 생성 헬퍼 (단일 채널, originCol/Row=0)
static HeightMapPtr makeHM(int w, int h, float fillRaw,
                            float xRes = 1.f, float yRes = 1.f,
                            float zRes = 0.001f, float zZero = 0.f,
                            std::string frameId = "") {
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->xResMm = xRes; hm->yResMm = yRes;
    hm->zResMm = zRes; hm->zZeroCount = zZero;
    hm->frameId = frameId;
    hm->data.assign(static_cast<size_t>(w) * h, fillRaw);
    return hm;
}

static VisionDataPtr makeInput(HeightMapPtr hm, std::shared_ptr<PlaneModel> pm) {
    return makeInputHMPlane(std::move(hm), std::move(pm));
}

static std::shared_ptr<PlaneModel> makePlane(double a, double b, double c,
                                              std::string frameId = "") {
    auto pm = std::make_shared<PlaneModel>();
    pm->a = a; pm->b = b; pm->c = c;
    pm->valid = true;
    pm->frameId = frameId;
    return pm;
}

// 평면 위에 있는 데이터 → distance 출력 전부 0
TEST(LevelToolTest, FlatInputDistanceIsZero) {
    // z = 0.5mm (constant). Plane: a=0, b=0, c=0.5. All pixels on plane.
    // raw = 0.5 / 0.001 = 500 (zZero=0)
    auto hm = makeHM(4, 4, 500.f);       // z=0.5mm
    auto pm = makePlane(0, 0, 0.5);      // c = 0.5
    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    EXPECT_FLOAT_EQ(out.zZeroCount, 0.f);      // B-plan
    for (int i = 0; i < 4*4; ++i)
        EXPECT_NEAR(out.data[i], 0.f, 0.01f);  // raw=0 → zMm=0
}

// 기울어진 평면 + 알려진 단차 복원
TEST(LevelToolTest, TiltedPlaneStepRecovered) {
    // Plane: a=0, b=0, c=0 (z=0mm 평면). 입력은 0.1mm 위에 있는 픽셀들.
    // raw = 0.1mm / 0.001 = 100
    auto hm = makeHM(3, 3, 100.f);       // z=0.1mm
    auto pm = makePlane(0, 0, 0);
    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    // out_mm = 0.1mm, raw = 0.1/0.001 = 100
    for (float v : out.data)
        EXPECT_NEAR(v, 100.f, 0.1f);
}

// distance vs flatten: 기울기 0일 때 일치
TEST(LevelToolTest, DistanceFlattenAgreeAtZeroTilt) {
    auto hm = makeHM(3, 3, 200.f);       // z=0.2mm
    auto pm = makePlane(0, 0, 0.1);      // plane c=0.1mm

    LevelParams pd; pd.mode = "distance";
    LevelParams pf; pf.mode = "flatten";
    LevelTool td(pd), tf(pf);

    auto rd = td.execute(makeInput(hm, pm));
    auto rf = tf.execute(makeInput(hm, pm));
    ASSERT_EQ(rd.status, ToolStatus::Ok);
    ASSERT_EQ(rf.status, ToolStatus::Ok);

    const auto& od = rd.output->heightmap0()->data;
    const auto& of = rf.output->heightmap0()->data;
    for (size_t i = 0; i < od.size(); ++i)
        EXPECT_NEAR(od[i], of[i], 0.01f);   // a=b=0 → inv_norm=1
}

// distance vs flatten: 기울기 있으면 inv_norm 차이
TEST(LevelToolTest, DistanceFlattenDifferWithTilt) {
    // a=1 → inv_norm = 1/sqrt(2) ≈ 0.707
    // pixel (0,0), x=0, y=0: z = 0.5mm, plane_z = 0. dz=0.5
    // flatten: 0.5mm → raw=500. distance: 0.5/sqrt(2)≈0.3536mm → raw≈353.6
    auto hm = makeHM(1, 1, 500.f);       // z=0.5mm at (0,0)
    auto pm = makePlane(1.0, 0, 0);      // plane: z = 1*x

    LevelParams pd; pd.mode = "distance";
    LevelParams pf; pf.mode = "flatten";
    LevelTool td(pd), tf(pf);

    auto rd = td.execute(makeInput(hm, pm));
    auto rf = tf.execute(makeInput(hm, pm));
    ASSERT_EQ(rd.status, ToolStatus::Ok);
    ASSERT_EQ(rf.status, ToolStatus::Ok);

    float dist_raw  = rd.output->heightmap0()->data[0];
    float flat_raw  = rf.output->heightmap0()->data[0];
    EXPECT_NEAR(flat_raw,  500.f,  0.1f);           // 0.5mm / 0.001
    EXPECT_NEAR(dist_raw,  500.f * (float)(1.0/std::sqrt(2.0)), 0.5f);
}

// NaN 보존 (keepInvalid=true)
TEST(LevelToolTest, NaNPreserved) {
    auto hm = makeHM(2, 2, 100.f);
    hm->data[0] = std::numeric_limits<float>::quiet_NaN();
    auto pm = makePlane(0, 0, 0);
    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_TRUE(std::isnan(res.output->heightmap0()->data[0]));
    EXPECT_FALSE(std::isnan(res.output->heightmap0()->data[1]));
}

// keepInvalid=false → NaN → 0
TEST(LevelToolTest, KeepInvalidFalse) {
    auto hm = makeHM(2, 2, 100.f);
    hm->data[0] = std::numeric_limits<float>::quiet_NaN();
    auto pm = makePlane(0, 0, 0);
    LevelParams p; p.keepInvalid = false;
    LevelTool tool(p);
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_FLOAT_EQ(res.output->heightmap0()->data[0], 0.f);
}

// 다채널: 채널0만 변경, 채널1 bit-identical
TEST(LevelToolTest, MultiChannelCh1BitIdentical) {
    auto hm = std::make_shared<HeightMap>();
    hm->width = 2; hm->height = 2; hm->channels = 2;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->data.resize(8);
    // ch0: z=0.1mm → raw=100
    hm->data[0] = hm->data[1] = hm->data[2] = hm->data[3] = 100.f;
    // ch1: arbitrary intensity values
    hm->data[4] = 11.f; hm->data[5] = 22.f; hm->data[6] = 33.f; hm->data[7] = 44.f;

    auto pm = makePlane(0, 0, 0.05);   // plane c=0.05mm
    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();
    // ch0: 0.1-0.05=0.05mm → raw=50
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(out.data[i], 50.f, 0.1f);
    // ch1: bit-identical
    EXPECT_FLOAT_EQ(out.data[4], 11.f);
    EXPECT_FLOAT_EQ(out.data[5], 22.f);
    EXPECT_FLOAT_EQ(out.data[6], 33.f);
    EXPECT_FLOAT_EQ(out.data[7], 44.f);
}

// plane.valid=false → Fail
TEST(LevelToolTest, InvalidPlaneFails) {
    auto hm = makeHM(2, 2, 100.f);
    auto pm = makePlane(0, 0, 0);
    pm->valid = false;
    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// HF != PF (둘 다 비어있지 않음) → Fail
TEST(LevelToolTest, FrameMismatchFails) {
    auto hm = makeHM(2, 2, 100.f, 1.f, 1.f, 0.001f, 0.f, "camA");
    auto pm = makePlane(0, 0, 0, "worldB");
    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    EXPECT_EQ(res.status, ToolStatus::Fail);
}

// HeightMap 없음 → Fail
TEST(LevelToolTest, NoHeightMapFails) {
    // port0 없이 port1(Plane)만
    auto d = makeInputHMPlane(nullptr, makePlane(0, 0, 0));
    LevelTool tool;
    EXPECT_EQ(tool.execute(d).status, ToolStatus::Fail);
}

// Plane 없음 → Fail
TEST(LevelToolTest, NoPlaneFails) {
    auto d = makeInputHM(makeHM(2, 2, 100.f));
    LevelTool tool;
    EXPECT_EQ(tool.execute(d).status, ToolStatus::Fail);
}

// frameId 전파 확인
TEST(LevelToolTest, FrameIdPropagated) {
    auto hm = makeHM(2, 2, 100.f, 1.f, 1.f, 0.001f, 0.f, "cam1");
    auto pm = makePlane(0, 0, 0);          // frameId="" (미지정, HF != PF 조건 해당 안됨)
    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_EQ(res.output->heightmap0()->frameId, "cam1");
}

// offsetMm 적용 확인
TEST(LevelToolTest, OffsetMm) {
    // z=0mm, plane c=0 → dz=0. offset=0.5mm → raw=500
    auto hm = makeHM(2, 2, 0.f);
    auto pm = makePlane(0, 0, 0);
    LevelParams p; p.offsetMm = 0.5;
    LevelTool tool(p);
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    for (float v : res.output->heightmap0()->data)
        EXPECT_NEAR(v, 500.f, 0.1f);
}

// HeightMeasure 정합 검증 (§3.5, §2-8)
// Level(distance) 출력 zMm() == PlaneModel::signedDistance(x,y,z) for every valid pixel
// 두 경로가 같은 부호·같은 수직거리 정의를 쓰는지 증명한다.
TEST(LevelToolTest, HeightMeasureCompatibility) {
    // 현실적인 기울어진 평면 파라미터 (top_inspect.json 수준)
    const float xRes = 0.0063f, yRes = 0.033f, zRes = 0.00105f;
    const float zZero = 32768.f;
    const double a = 0.005320, b = 0.001090, c_pl = 0.964534;

    // 5×5 격자, originCol/Row 임의값, 다양한 높이
    const int W = 5, H = 5;
    auto hm = std::make_shared<HeightMap>();
    hm->width = W; hm->height = H; hm->channels = 1;
    hm->xResMm = xRes; hm->yResMm = yRes;
    hm->zResMm = zRes; hm->zZeroCount = zZero;
    hm->originCol = 123.4f; hm->originRow = 567.8f;
    hm->data.resize(W * H);
    // raw = zZero + varying counts
    for (int i = 0; i < W * H; ++i)
        hm->data[i] = zZero + static_cast<float>(i * 7 - 17);
    // 중앙 픽셀은 NaN
    hm->data[2 * W + 2] = std::numeric_limits<float>::quiet_NaN();

    auto pm = makePlane(a, b, c_pl);

    LevelTool tool;
    auto res = tool.execute(makeInput(hm, pm));
    ASSERT_EQ(res.status, ToolStatus::Ok);
    const HeightMap& out = *res.output->heightmap0();

    double sumLevel = 0.0, sumDirect = 0.0;
    int count = 0;

    for (int r = 0; r < H; ++r) {
        for (int c = 0; c < W; ++c) {
            float raw_in = hm->rawAt(c, r, 0);
            if (std::isnan(raw_in)) continue;

            // Level 출력에서 읽기 (B-plan: zZeroCount=0)
            float raw_out = out.rawAt(c, r, 0);
            double level_mm = raw_out * out.zResMm;  // zZeroCount=0

            // 직접 계산: signedDistance
            double x_mm = (c - (double)hm->originCol) * hm->xResMm;
            double y_mm = (r - (double)hm->originRow) * hm->yResMm;
            double z_mm = (raw_in - (double)hm->zZeroCount) * hm->zResMm;
            double direct_mm = pm->signedDistance(x_mm, y_mm, z_mm);

            // 픽셀별 일치 (float 정밀도 한계 내)
            EXPECT_NEAR(level_mm, direct_mm, 1e-5)
                << "mismatch at (" << c << "," << r << ")"
                << " level=" << level_mm << " direct=" << direct_mm;

            sumLevel  += level_mm;
            sumDirect += direct_mm;
            ++count;
        }
    }

    ASSERT_GT(count, 0);
    // ROI 평균도 일치
    EXPECT_NEAR(sumLevel / count, sumDirect / count, 1e-6);
}
