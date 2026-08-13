#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// HeightMap(높이맵) → PointCloud3D. 유효 픽셀마다 (x,y,z)mm 점 생성.
//   x = (col-originCol)*xRes, y = (row-originRow)*yRes, z = (raw-zZero)*zRes (mm)
//   step으로 서브샘플(대용량 클라우드 감축). NaN(무효) 픽셀은 건너뜀.
class HeightMapToCloudTool : public IAlgorithmTool {
public:
    explicit HeightMapToCloudTool(int step = 1) : m_step(step < 1 ? 1 : step) {}
    std::string name() const override { return "HeightMapToCloud"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    int m_step;
};

} // namespace vision
