#pragma once

#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// A5-3: OriginCoord 전용 슬롯 폐기 → Point(RefPoint)로 통일.
//   "어느 축을 정렬할지"는 useX/useY 파라미터로 결정한다.
//   포트 1의 points[0] (LineCenter 검출 기준점)을 사용한다.

struct AlignParams {
    bool useX = true;    // X축(col) 정렬 여부
    bool useY = true;    // Y축(row) 정렬 여부
};

class AlignTool : public IAlgorithmTool {
public:
    explicit AlignTool(AlignParams params = {}) : m_params(params) {}

    std::string name() const override { return "Align"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    AlignParams m_params;
};

} // namespace vision
