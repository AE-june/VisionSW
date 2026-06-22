#include <gtest/gtest.h>
#include "ThicknessMeasure.h"

using namespace vision;

static VisionDataPtr makeFlatCloud(float zValue, int count = 100) {
    auto cloud = std::make_shared<PointCloud3D>();
    for (int i = 0; i < count; ++i)
        cloud->points.push_back({ (float)i, (float)i, zValue });

    auto data = std::make_shared<VisionData>();
    data->cloud = cloud;
    return data;
}

static VisionDataPtr makeCloudWithThickness(float zMin, float zMax) {
    auto cloud = std::make_shared<PointCloud3D>();
    for (int i = 0; i < 50; ++i)
        cloud->points.push_back({ (float)i, (float)i, zMin });
    for (int i = 0; i < 50; ++i)
        cloud->points.push_back({ (float)i, (float)i, zMax });

    auto data = std::make_shared<VisionData>();
    data->cloud = cloud;
    return data;
}

TEST(ThicknessMeasureTest, FlatSurfaceReturnsZero) {
    ThicknessMeasure tool;
    auto data = makeFlatCloud(10.0f);
    auto result = tool.execute(data);

    EXPECT_EQ(result.status, ToolStatus::Ok);
    EXPECT_NEAR(tool.lastResult().thicknessMm, 0.0f, 0.001f);
}

TEST(ThicknessMeasureTest, KnownThickness) {
    ThicknessMeasure tool;
    auto data = makeCloudWithThickness(0.f, 5.f);
    auto result = tool.execute(data);

    EXPECT_EQ(result.status, ToolStatus::Ok);
    EXPECT_NEAR(tool.lastResult().thicknessMm, 5.0f, 0.001f);
}

TEST(ThicknessMeasureTest, PassFailWithNominal) {
    ThicknessMeasure::Params params;
    params.nominalMm   = 5.0f;
    params.toleranceMm = 0.05f;

    ThicknessMeasure tool(params);

    // Should pass: 5.02mm within ±0.05
    auto pass = makeCloudWithThickness(0.f, 5.02f);
    tool.execute(pass);
    EXPECT_TRUE(tool.lastResult().pass);

    // Should fail: 5.1mm outside ±0.05
    auto fail = makeCloudWithThickness(0.f, 5.1f);
    tool.execute(fail);
    EXPECT_FALSE(tool.lastResult().pass);
}

TEST(ThicknessMeasureTest, NullInputFails) {
    ThicknessMeasure tool;
    auto result = tool.execute(nullptr);
    EXPECT_EQ(result.status, ToolStatus::Fail);
}
