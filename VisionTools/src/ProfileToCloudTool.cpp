#include "ProfileToCloudTool.h"
#include "Logger.h"
#include <cmath>

namespace vision {

ToolResult ProfileToCloudTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "null input" };
    const auto& profiles = input->inProfiles(0);
    if (profiles.empty())
        return { ToolStatus::Fail, "ProfileToCloud: Profile[] 입력이 필요합니다" };

    size_t total = 0;
    for (const auto& p : profiles) total += p->size();

    auto cloud = std::make_shared<PointCloud3D>();
    cloud->frameId = profiles.front()->frameId;
    cloud->points.reserve(total);

    const double tres = m_params.transportResMm;

    for (const auto& p : profiles) {
        const Profile& prof = *p;
        for (size_t i = 0; i < prof.size(); ++i) {
            if (std::isnan(prof.z[i])) continue;   // 무효 샘플 제외
            // CloudToProfiles는 scanAxis 설정과 무관하게 항상 x=횡(lateral), y=스캔위치로 저장한다
            // (CloudToProfilesTool.cpp 참조). 원본 클라우드 관례(X=스캔/transport, Y=횡/lateral —
            // NotchMeasure 등 다운스트림 툴의 고정 가정)로 복원하려면 x/y를 다시 스왑해야 한다.
            double scanPos = prof.y[i];
            // transportResMm > 0: scanStepMm 배수로 스냅 — 부동소수점 오차 제거,
            // 다운스트림(NotchMeasure)의 pt.x/transportResMm 재그룹핑과 정확히 맞물리게 함.
            if (tres > 1e-9) scanPos = std::round(scanPos / tres) * tres;
            cloud->points.push_back({
                static_cast<float>(scanPos),
                static_cast<float>(prof.x[i]),
                static_cast<float>(prof.z[i]),
            });
        }
    }

    auto out = std::make_shared<VisionData>();
    out->setCloud(cloud);
    out->sourceId = input->sourceId;
    VISION_LOG_INFO("ProfileToCloud: {} profiles → {} points", profiles.size(), cloud->points.size());
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
