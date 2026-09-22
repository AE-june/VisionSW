#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  RegionToHeightMapTool — Region → HeightMap(이진 이미지)
//  마스크 내부=insideValue, 외부=outsideValue 인 단일채널 HeightMap 생산.
//  HALCON region_to_bin 상당. Threshold의 Region을 0/1 이미지로 되돌려
//  ExtractProfile 등 HeightMap 입력 툴에 넘길 때 사용(이진 프로파일 추출).
//
//  좌표: z는 항상 0/1 그대로(zResMm=1, zZeroCount=0). 옵션 HeightMap(포트1)이
//  주어지면 XY 지오메트리(xRes/yRes/원점/frameId)만 복사해 s좌표를 mm로 유지.
//  없으면 픽셀 단위.
// ─────────────────────────────────────────────────────────────────────
struct RegionToHeightMapParams {
    float insideValue  = 1.f;   // 마스크 내부 픽셀 값
    float outsideValue = 0.f;   // 마스크 외부 픽셀 값
};

class RegionToHeightMapTool : public IAlgorithmTool {
public:
    explicit RegionToHeightMapTool(RegionToHeightMapParams params = {});
    std::string name() const override { return "RegionToHeightMap"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    RegionToHeightMapParams m_params;
};

} // namespace vision
