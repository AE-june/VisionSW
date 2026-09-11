#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  ProfileSmoothTool — Profile 1D 스무딩
//  HALCON smooth_funct_1d_gauss 상당. ProfileFeature 엣지 검출 전처리용.
//  method = "gaussian" : 가우시안 커널 1D 합성곱
//         = "mean"     : 단순 이동 평균
//  NaN 픽셀은 무시(유효 샘플만으로 평균).
// ─────────────────────────────────────────────────────────────────────
struct ProfileSmoothParams {
    std::string method     = "gaussian";
    int         windowSize = 5;
    double      sigma      = 1.0;
};

class ProfileSmoothTool : public IAlgorithmTool {
public:
    explicit ProfileSmoothTool(ProfileSmoothParams params = {});
    std::string name() const override { return "ProfileSmooth"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    ProfileSmoothParams m_params;
};

} // namespace vision
