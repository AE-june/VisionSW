#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

struct SurfaceCropParams {
    std::string mode      = "rect"; // "rect" | "region"
    int         rect_x    = 0;
    int         rect_y    = 0;
    int         rect_w    = 0;      // 0 = 입력 전체 너비
    int         rect_h    = 0;      // 0 = 입력 전체 높이
    bool        outsideNaN = true;  // mode=region에서 Region 밖 픽셀 ch0 → NaN
    std::string nodeId;             // 신규 프레임 id 생성용 ("hm:<nodeId>")
};

class SurfaceCropTool : public IAlgorithmTool {
public:
    explicit SurfaceCropTool(SurfaceCropParams params = {});
    std::string name() const override { return "SurfaceCrop"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    SurfaceCropParams m_params;
};

} // namespace vision
