#pragma once

#include "IAlgorithmTool.h"
#include "ZMap.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  LineCenterTool
//    ZMap을 임계값으로 이진화 → 설정한 ROI 내 전경(라인) 픽셀에
//    라인피팅 → 라인 중심 (x, y)을 출력.
//    (좌표 정렬: 타겟 엣지를 찾아 기준점으로 사용)
// ─────────────────────────────────────────────────────────────────────
// 스캔 방향 (ROI 로컬 프레임 기준 — 회전과 함께 적용)
//   Lr 좌→우(+x), Rl 우→좌(-x), Tb 위→아래(+y), Bt 아래→위(-y)
enum class ScanDir { Lr, Rl, Tb, Bt };

// 에지 극성 (스캔 진행 중 임계값을 넘는 방향)
//   DarkToLight 흑→백 (raw가 threshold 미만 → 이상)
//   LightToDark 백→흑 (raw가 threshold 이상 → 미만)
enum class Polarity { DarkToLight, LightToDark };

struct LineCenterParams {
    // 검색 ROI (이미지 전체 대비 %, RoiCanvas와 동일)
    float xPct = 0.f, yPct = 0.f, wPct = 1.f, hPct = 1.f;

    // 검색 ROI 회전 (중심 기준, deg, 시계방향). 대각선 라인 검색용
    float angleDeg = 0.f;

    // 에지 캘리퍼: 스캔 방향으로 진행하며 임계값 교차점(에지)을 찾아 라인피팅
    ScanDir  scanDir  = ScanDir::Lr;
    Polarity polarity = Polarity::DarkToLight;
    float    threshold = 1.f;   // 이진화 임계값 (raw count)
};

struct LineCenterResult {
    double cx = 0, cy = 0;       // 라인 중심 (픽셀: col, row)
    double cxMm = 0, cyMm = 0;   // 라인 중심 (mm)
    double angleDeg = 0;         // 라인 방향 (주축 각도, deg)
    int    pointCount = 0;       // 전경 픽셀 수
    bool   valid = false;
    std::string message;
};

class LineCenterTool : public IAlgorithmTool {
public:
    explicit LineCenterTool(LineCenterParams params = {}) : m_params(params) {}

    std::string name() const override { return "LineCenter"; }
    ToolResult  execute(VisionDataPtr input) override;

    const LineCenterResult& lastResult() const { return m_result; }

private:
    LineCenterParams m_params;
    LineCenterResult m_result;
};

} // namespace vision
