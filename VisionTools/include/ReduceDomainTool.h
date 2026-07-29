#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ReduceDomainTool — (HeightMap + Region) → HeightMap
//  HALCON의 reduce_domain: 이미지의 "연산 도메인"을 Region으로 제한한다.
//  Region 밖 픽셀의 전 채널을 NaN(무효)으로 만든다. 하류 측정툴은 이미
//  valid()로 NaN을 건너뛰므로, 코드 수정 없이 "그 영역 안에서만" 측정된다.
//  입력은 병합된 VisionData(heightmap + region 둘 다 세팅)로 들어온다.
// ─────────────────────────────────────────────────────────────────────
class ReduceDomainTool : public IAlgorithmTool {
public:
    std::string name() const override { return "ReduceDomain"; }
    ToolResult  execute(VisionDataPtr input) override;
};

} // namespace vision
