#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  HeightMapNormalizeTool — HeightMap 정규화/평활화 (OpenCV 래핑)
//  포트0=HeightMap(필수), 포트1=Region(선택). 채널0(height) raw 값을 변환.
//  mode:
//    "minmax"  : 유효 픽셀 [min,max] → [outMin,outMax] 선형 스케일.
//    "zscore"  : (v - mean) / std 표준화. (유효 픽셀 통계)
//    "equalize": 히스토그램 평활화. 유효 픽셀만 8bit 양자화 후 cv::equalizeHist,
//                다시 [min,max]로 역매핑.
//    "clahe"   : cv::createCLAHE (clipLimit, tileGrid) 지역 적응 대비 향상.
//  NaN(무효) 픽셀은 통계 제외 + 출력에서도 NaN 유지.
//  Region(포트1) 연결 시: 영역 내 픽셀만 통계·변환, 영역 밖은 NaN.
//  출력 HeightMap: zResMm=1, zZeroCount=0 → zMm = raw(정규화값) 그대로.
// ─────────────────────────────────────────────────────────────────────
struct HeightMapNormalizeParams {
    std::string mode      = "minmax";  // minmax | zscore | equalize | clahe
    double      outMin    = 0.0;       // minmax 출력 하한
    double      outMax    = 1.0;       // minmax 출력 상한
    double      clipLimit = 2.0;       // clahe clipLimit
    int         tileGrid  = 8;         // clahe tileGridSize (정사각)
};

class HeightMapNormalizeTool : public IAlgorithmTool {
public:
    explicit HeightMapNormalizeTool(HeightMapNormalizeParams params = {});
    std::string name() const override { return "HeightMapNormalize"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    HeightMapNormalizeParams m_params;
};

} // namespace vision
