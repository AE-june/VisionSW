#include <gtest/gtest.h>
#include "CloudToProfilesTool.h"
#include "ProfileFeatureTool.h"
#include "VisionData.h"
#include <cmath>
#include <limits>

using namespace vision;

// 3행 클라우드. 한 컬럼(x=1)에 다중 Z(5,9). yStep=1 기준 3행.
static VisionDataPtr makeInputCloud() {
    auto cloud = std::make_shared<PointCloud3D>();
    cloud->frameId = "test";
    // row0 (y=0.2): x=0(z3), x=1(z5), x=1(z9)  다중Z, x=2(z4)
    cloud->points.push_back({0.f, 0.2f, 3.f});
    cloud->points.push_back({1.f, 0.2f, 5.f});
    cloud->points.push_back({1.f, 0.2f, 9.f});
    cloud->points.push_back({2.f, 0.2f, 4.f});
    // row1 (y=1.2): x=0(z2), x=1(z6), x=2(z1)
    cloud->points.push_back({0.f, 1.2f, 2.f});
    cloud->points.push_back({1.f, 1.2f, 6.f});
    cloud->points.push_back({2.f, 1.2f, 1.f});
    // row2 (y=2.5): x=0(z7), x=2(z8)
    cloud->points.push_back({0.f, 2.5f, 7.f});
    cloud->points.push_back({2.f, 2.5f, 8.f});

    auto port0 = std::make_shared<VisionData>();
    port0->setCloud(cloud);
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::move(port0));
    return d;
}

static double findMeas(const std::vector<Measurement>& ms, const std::string& name) {
    for (auto& m : ms) if (m.name == name) return m.value;
    return std::numeric_limits<double>::quiet_NaN();
}

TEST(CloudToProfilesTest, NoneKeepsAllPoints) {
    // reduce=None → 행별 모든 점 보존. row0 = 4샘플(다중Z 포함). scanAxis=Y(행=Y bin), 횡=X.
    CloudToProfilesTool tool({ CloudToProfilesTool::Axis::Y, 1.0, CloudToProfilesTool::Reduce::None, 0.1, 1 });
    auto res = tool.execute(makeInputCloud());
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->profiles.size(), 3u);

    const auto& p0 = *res.output->profiles[0];
    EXPECT_EQ(p0.size(), 4u);              // 다중 Z 전부 유지
    EXPECT_EQ(p0.label, "row:0");
    double mx = -1e9; for (double z : p0.z) mx = std::max(mx, z);
    EXPECT_DOUBLE_EQ(mx, 9.0);             // 컬럼 x=1의 두 Z 중 큰 값 존재
    EXPECT_EQ(res.output->profiles[2]->size(), 2u);  // row2 = 2점
}

TEST(CloudToProfilesTest, MaxReducesColumn) {
    // reduce=Max, 횡(X) step=1 → 횡 x=1 대표값 = max(5,9)=9. scanAxis=Y.
    CloudToProfilesTool tool({ CloudToProfilesTool::Axis::Y, 1.0, CloudToProfilesTool::Reduce::Max, 1.0, 1 });
    auto res = tool.execute(makeInputCloud());
    ASSERT_EQ(res.status, ToolStatus::Ok);
    ASSERT_EQ(res.output->profiles.size(), 3u);

    const auto& p0 = *res.output->profiles[0];
    EXPECT_EQ(p0.size(), 3u);              // 컬럼 3개 (x=0,1,2)
    EXPECT_DOUBLE_EQ(p0.z[1], 9.0);        // x=1 컬럼 max
    EXPECT_DOUBLE_EQ(p0.z[0], 3.0);
    EXPECT_DOUBLE_EQ(p0.z[2], 4.0);
}

TEST(CloudToProfilesTest, ProfileFeatureIteratesRows) {
    // CloudToProfiles(Max) → ProfileFeature(maxZ) 가 행별 라벨 프리픽스로 측정.
    CloudToProfilesTool ctp({ CloudToProfilesTool::Axis::Y, 1.0, CloudToProfilesTool::Reduce::Max, 1.0, 1 });
    auto cres = ctp.execute(makeInputCloud());
    ASSERT_EQ(cres.status, ToolStatus::Ok);

    // ProfileFeature 입력: 포트0 = CloudToProfiles 출력(profiles[])
    auto feedIn = std::make_shared<VisionData>();
    feedIn->inputs.push_back(cres.output);

    ProfileFeatureParams pf; pf.kind = "maxZ";
    ProfileFeatureTool feat(pf);
    auto fres = feat.execute(feedIn);
    ASSERT_EQ(fres.status, ToolStatus::Ok);

    auto& ms = fres.output->measurements;
    EXPECT_DOUBLE_EQ(findMeas(ms, "row:0.maxZ"), 9.0);
    EXPECT_DOUBLE_EQ(findMeas(ms, "row:1.maxZ"), 6.0);
    EXPECT_DOUBLE_EQ(findMeas(ms, "row:2.maxZ"), 8.0);
}

TEST(CloudToProfilesTest, FailEmptyCloud) {
    auto d = std::make_shared<VisionData>();
    d->inputs.push_back(std::make_shared<VisionData>());  // 포트0 있으나 cloud 없음
    CloudToProfilesTool tool;
    EXPECT_EQ(tool.execute(d).status, ToolStatus::Fail);
}
