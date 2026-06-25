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
    // 검색 ROI (이미지 전체 대비 %, RoiCanvas와 동일). 여러 개 → 여러 라인 검색.
    // 에지 극성은 ROI마다 개별 지정.
    struct ROI {
        float xPct = 0, yPct = 0, wPct = 1, hPct = 1, angleDeg = 0;
        Polarity polarity = Polarity::DarkToLight;
    };
    std::vector<ROI> rois;

    // 에지 캘리퍼: 스캔 방향으로 진행하며 임계값 교차점(에지)을 찾아 라인피팅
    ScanDir  scanDir   = ScanDir::Lr;
    float    threshold = 1.f;   // 이진화 임계값 (raw count)

    // 출력 좌표 선택: X출력은 xRoi 라인의 x, Y출력은 yRoi 라인의 y (ROI 인덱스)
    int xRoi = 0;
    int yRoi = 0;
};

struct LineCenterResult {
    // 찾은 라인 하나
    struct Line {
        double cx = 0, cy = 0;       // 라인 중심 (픽셀: col, row)
        double cxMm = 0, cyMm = 0;   // 라인 중심 (mm)
        double angleDeg = 0;         // 라인 방향 (주축 각도, deg)
        int    roiIndex = 0;         // 어느 검색 ROI에서 나왔는지
        int    pointCount = 0;       // 에지점 수
    };
    std::vector<Line> lines;         // 찾은 모든 라인
    bool   valid = false;            // 하나라도 찾으면 true
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

    // 단일 ROI에서 라인 검색 (찾으면 out 채우고 true)
    bool findLine(const ZMap& map, const LineCenterParams::ROI& roi,
                  LineCenterResult::Line& out) const;
};

} // namespace vision
