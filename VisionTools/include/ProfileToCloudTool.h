#pragma once

#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────
//  ProfileToCloudTool
//  Profile[] → PointCloud3D. CloudToProfiles의 역변환.
//  CloudToProfiles는 scanAxis 설정과 무관하게 항상 profile.x=횡(lateral), profile.y=스캔위치로
//  저장하므로, 원본 클라우드 관례(X=스캔/transport, Y=횡/lateral — NotchMeasure 등 다운스트림
//  고정 가정)로 복원하기 위해 x/y를 다시 스왑해 (y,x,z) 순서로 점을 만든다.
//  z가 NaN인 샘플은 건너뜀.
// ─────────────────────────────────────────────
class ProfileToCloudTool : public IAlgorithmTool {
public:
    struct Params {
        // >0이면 스캔축(transport) 좌표를 이 값의 배수로 스냅(round). CloudToProfiles의
        // scanStepMm과 같은 값을 넣으면 부동소수점 오차를 없애 NotchMeasure 등 다운스트림의
        // pt.x/transportResMm 재그룹핑과 정확히 맞물린다. 0이면 profile에 저장된 값을 그대로 사용.
        double transportResMm = 0.0;
    };

    explicit ProfileToCloudTool(Params params = {}) : m_params(params) {}
    std::string name() const override { return "ProfileToCloud"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    Params m_params;
};

} // namespace vision
