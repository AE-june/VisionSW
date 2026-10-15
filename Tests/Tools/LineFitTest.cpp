#include <gtest/gtest.h>
#include "LineFitTool.h"
#include "VisionData.h"
#include "Region.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>

using namespace vision;
using namespace vision::test;

// base 위에 peak 능선을 그린 HeightMap. mode에 따라 세로/대각 능선.
//   vertical: col==ridge 에서 peak
//   diagonal: col==row 에서 peak
static std::shared_ptr<HeightMap> makeRidge(int w, int h, const char* mode, int ridge,
    float base = 0.f, float peak = 100.f, float res = 0.5f)
{
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = res; hm->yResMm = res;
    hm->data.assign(static_cast<size_t>(w) * h, base);
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) {
            bool onRidge = (std::string(mode) == "vertical") ? (col == ridge)
                         : (col == row);
            if (onRidge) hm->data[static_cast<size_t>(row) * w + col] = peak;
        }
    return hm;
}

// 좌→우 계단 엣지: col<edge 는 base, 이상은 high.
static std::shared_ptr<HeightMap> makeStep(int w, int h, int edge,
    float base = 0.f, float high = 100.f, float res = 0.5f)
{
    auto hm = std::make_shared<HeightMap>();
    hm->width = w; hm->height = h; hm->channels = 1;
    hm->zResMm = 0.001f; hm->zZeroCount = 0.f;
    hm->xResMm = res; hm->yResMm = res;
    hm->data.assign(static_cast<size_t>(w) * h, base);
    for (int row = 0; row < h; ++row)
        for (int col = edge; col < w; ++col)
            hm->data[static_cast<size_t>(row) * w + col] = high;
    return hm;
}

static double findMeasurement(const std::vector<Measurement>& ms, const std::string& name) {
    for (auto& m : ms) if (m.name == name) return m.value;
    return std::numeric_limits<double>::quiet_NaN();
}

// 각도를 [0,180) 로 정규화 (라인 방향은 ±180 동일)
static double norm180(double a) { a = std::fmod(a, 180.0); if (a < 0) a += 180.0; return a; }

// ─────────────────────────────────────────────────────────────────────
TEST(LineFitTest, RidgeVertical) {
    // col=30 세로 능선. scanDir=lr → 각 행에서 최대(col30) 검출 → 수직 라인.
    auto hm = makeRidge(64, 40, "vertical", 30);
    LineFitTool tool({ LineFeature::Ridge, LineScanDir::Lr, 0.f, true });
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    auto& ms = res.output->measurements;
    EXPECT_NEAR(norm180(findMeasurement(ms, "angleDeg")), 90.0, 1.0);
    EXPECT_NEAR(findMeasurement(ms, "cxMm"), 30 * 0.5, 0.5);        // col30 → 15mm
    EXPECT_EQ  (static_cast<int>(findMeasurement(ms, "pointCount")), 40);
    EXPECT_NEAR(findMeasurement(ms, "lengthMm"), 39 * 0.5, 0.5);    // row0..39
    EXPECT_NEAR(findMeasurement(ms, "straightness"), 0.0, 1e-3);

    ASSERT_NE(res.output->line0(), nullptr);
    EXPECT_TRUE(res.output->line0()->valid);
    EXPECT_NEAR(res.output->line0()->lengthMm, 39 * 0.5, 0.5);
}

TEST(LineFitTest, RidgeDiagonal) {
    // col==row 대각 능선 → 45도 라인.
    auto hm = makeRidge(50, 50, "diagonal", 0);
    LineFitTool tool({ LineFeature::Ridge, LineScanDir::Lr, 0.f, true });
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    auto& ms = res.output->measurements;
    EXPECT_NEAR(norm180(findMeasurement(ms, "angleDeg")), 45.0, 1.5);
    EXPECT_NEAR(findMeasurement(ms, "straightness"), 0.0, 1e-3);
}

TEST(LineFitTest, EdgeRising) {
    // col=40 계단. edge 모드, rising, threshold=50 → 각 행 col40 검출 → 수직 라인.
    auto hm = makeStep(80, 30, 40, 0.f, 100.f);
    LineFitTool tool({ LineFeature::Edge, LineScanDir::Lr, 50.f, true });
    auto res = tool.execute(makeInputHM(hm));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    auto& ms = res.output->measurements;
    EXPECT_NEAR(norm180(findMeasurement(ms, "angleDeg")), 90.0, 1.0);
    EXPECT_NEAR(findMeasurement(ms, "cxMm"), 40 * 0.5, 0.5);
    EXPECT_EQ  (static_cast<int>(findMeasurement(ms, "pointCount")), 30);
}

TEST(LineFitTest, RegionRestrictsSearch) {
    // 세로 능선 col=30. Region = 행 10..29만 → pointCount 20.
    auto hm = makeRidge(64, 40, "vertical", 30);
    auto rgn = std::make_shared<Region>(Region::makeEmpty(64, 40));
    for (int r = 10; r < 30; ++r)
        for (int c = 0; c < 64; ++c)
            rgn->mask[static_cast<size_t>(r) * 64 + c] = 1;

    LineFitTool tool({ LineFeature::Ridge, LineScanDir::Lr, 0.f, true });
    auto res = tool.execute(makeInputHMRegion(hm, rgn));
    ASSERT_EQ(res.status, ToolStatus::Ok);

    auto& ms = res.output->measurements;
    EXPECT_EQ(static_cast<int>(findMeasurement(ms, "pointCount")), 20);
    EXPECT_NEAR(findMeasurement(ms, "cxMm"), 30 * 0.5, 0.5);
}

TEST(LineFitTest, RansacRejectsOutliers) {
    // col=30 세로 능선. 위 8개 행에 col=5 더 큰 스파이크(이상점) 주입.
    auto hm = makeRidge(64, 40, "vertical", 30, 0.f, 100.f);
    for (int r = 0; r < 8; ++r)
        hm->data[static_cast<size_t>(r) * 64 + 5] = 200.f;   // 이상점(더 높음 → ridge가 여기 잡힘)

    // LeastSquares: 이상점 포함 → 각도 90에서 벗어남
    {
        LineFitTool ls({ LineFeature::Ridge, LineScanDir::Lr, 0.f, true,
                         LineFitMethod::LeastSquares, 0.5f, 100 });
        auto res = ls.execute(makeInputHM(hm));
        ASSERT_EQ(res.status, ToolStatus::Ok);
        // 이상점 8개가 각도를 크게 틀어놓음
        EXPECT_GT(std::abs(norm180(findMeasurement(res.output->measurements, "angleDeg")) - 90.0), 3.0);
    }
    // RANSAC: 이상점 배제 → 각도 ~90, 인라이어 32개
    {
        LineFitTool rs({ LineFeature::Ridge, LineScanDir::Lr, 0.f, true,
                         LineFitMethod::Ransac, 0.5f, 200 });
        auto res = rs.execute(makeInputHM(hm));
        ASSERT_EQ(res.status, ToolStatus::Ok);
        auto& ms = res.output->measurements;
        EXPECT_NEAR(norm180(findMeasurement(ms, "angleDeg")), 90.0, 1.0);
        EXPECT_NEAR(findMeasurement(ms, "cxMm"), 30 * 0.5, 0.5);
        EXPECT_EQ  (static_cast<int>(findMeasurement(ms, "pointCount")), 32);  // 40 - 8 이상점
        EXPECT_NEAR(findMeasurement(ms, "straightness"), 0.0, 1e-3);
    }
}

TEST(LineFitTest, FailNoHeightMap) {
    LineFitTool tool;
    auto res = tool.execute(std::make_shared<VisionData>());
    EXPECT_EQ(res.status, ToolStatus::Fail);
}
