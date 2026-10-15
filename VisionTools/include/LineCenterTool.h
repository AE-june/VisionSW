#pragma once

#include "IAlgorithmTool.h"
#include "HeightMap.h"
#include <string>

namespace vision {

// A5-4: rois 파라미터 → Region 포트(포트 1, 선택).
//   Region이 없으면 전체 이미지. 검색 영역은 Region 바운딩박스(항상 축 정렬).
//   A5-3: xRoi/yRoi 제거 → AlignTool의 useX/useY 파라미터로 이동.

enum class ScanDir { Lr, Rl, Tb, Bt };
enum class Polarity { DarkToLight, LightToDark };

struct LineCenterParams {
    ScanDir  scanDir  = ScanDir::Lr;
    float    threshold = 1.f;
    Polarity polarity  = Polarity::DarkToLight;
};

class LineCenterTool : public IAlgorithmTool {
public:
    explicit LineCenterTool(LineCenterParams params = {}) : m_params(params) {}

    std::string name() const override { return "LineCenter"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    LineCenterParams m_params;

    // 픽셀 좌표 범위 [x0,x1)×[y0,y1) 내에서 에지 캘리퍼 → 라인 피팅.
    // 성공 시 true, outCx/outCy (픽셀), outAngleDeg, outCount 채워 반환.
    bool findLine(const HeightMap& map, int x0, int y0, int x1, int y1,
                  double& outCx, double& outCy, double& outAngleDeg, int& outCount) const;
};

} // namespace vision
