#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ReduceDomainTool — (HeightMap + Region) → HeightMap
//  HALCON의 reduce_domain: 이미지의 "연산 도메인"을 Region으로 제한한다.
//  Region 밖 픽셀의 전 채널을 NaN(무효)으로 만든다. 하류 측정툴은 이미
//  valid()로 NaN을 건너뛰므로, 코드 수정 없이 "그 영역 안에서만" 측정된다.
//  입력은 병합된 VisionData(heightmap + region 둘 다 세팅)로 들어온다.
//  invert=true면 반대로 동작 — Region "안쪽"을 NaN 처리(검사 제외 마스크).
// ─────────────────────────────────────────────────────────────────────
struct ReduceDomainParams {
    bool invert = false;   // false: 안쪽 유지(포함) · true: 안쪽 제거(제외)
};

class ReduceDomainTool : public IAlgorithmTool {
public:
    explicit ReduceDomainTool(ReduceDomainParams params = {}) : m_params(params) {}
    std::string name() const override { return "ReduceDomain"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    ReduceDomainParams m_params;
};

} // namespace vision
