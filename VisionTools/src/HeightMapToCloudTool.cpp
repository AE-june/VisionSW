#include "HeightMapToCloudTool.h"
#include "Logger.h"

namespace vision {

ToolResult HeightMapToCloudTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "HeightMap→Cloud: HeightMap 입력이 필요합니다" };
    const HeightMap& zm = *input->inHeightMap(0);
    auto cloud = std::make_shared<PointCloud3D>();
    cloud->frameId = input->sourceId;
    cloud->points.reserve((size_t)(zm.width / m_step + 1) * (zm.height / m_step + 1));
    for (int row = 0; row < zm.height; row += m_step)
        for (int col = 0; col < zm.width; col += m_step) {
            if (!zm.valid(col, row)) continue;   // NaN 제외
            cloud->points.push_back({ zm.xMm(col), zm.yMm(row), zm.zMm(col, row) });
        }
    // 타입화 출력: 클라우드만 전달(다운스트림 저장/처리용). 결과창 이미지는 입력 heightmap으로 폴백.
    auto out = std::make_shared<VisionData>();
    out->setCloud(cloud);
    out->sourceId = input->sourceId;
    VISION_LOG_INFO("HeightMapToCloud: {} points (step={}, {}x{})",
                    cloud->points.size(), m_step, zm.width, zm.height);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
