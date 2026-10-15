#include "ProfileSmoothTool.h"
#include "Profile.h"
#include <cmath>
#include <vector>

namespace vision {

ProfileSmoothTool::ProfileSmoothTool(ProfileSmoothParams params) : m_params(std::move(params)) {}

static std::vector<double> buildGaussKernel(int halfW, double sigma) {
    std::vector<double> k(2 * halfW + 1);
    double sum = 0;
    for (int i = -halfW; i <= halfW; ++i) {
        k[i + halfW] = std::exp(-0.5 * i * i / (sigma * sigma));
        sum += k[i + halfW];
    }
    for (auto& v : k) v /= sum;
    return k;
}

static std::shared_ptr<Profile> smoothProfile(
    const Profile& src,
    const std::string& method,
    int windowSize,
    double sigma)
{
    const int n = static_cast<int>(src.z.size());
    if (n == 0) return nullptr;

    int halfW = std::max(1, windowSize / 2);
    std::vector<double> kernel;
    if (method == "gaussian")
        kernel = buildGaussKernel(halfW, sigma > 0 ? sigma : 1.0);

    auto out = std::make_shared<Profile>(src);   // 복사 (s/x/y/label 유지)
    for (int i = 0; i < n; ++i) {
        double wsum = 0, vsum = 0;
        for (int j = -halfW; j <= halfW; ++j) {
            int idx = i + j;
            if (idx < 0 || idx >= n) continue;
            if (std::isnan(src.z[idx])) continue;
            double w = (method == "gaussian") ? kernel[j + halfW] : 1.0;
            vsum += w * src.z[idx];
            wsum += w;
        }
        out->z[i] = (wsum > 0) ? vsum / wsum : src.z[i];
    }
    return out;
}

ToolResult ProfileSmoothTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "ProfileSmooth: 입력이 없습니다." };

    const auto& profs = input->inProfiles(0);
    if (profs.empty())
        return { ToolStatus::Fail, "ProfileSmooth: Profile 입력이 없습니다." };

    auto vd = std::make_shared<VisionData>();
    for (const auto& prof : profs) {
        if (!prof) continue;
        auto s = smoothProfile(*prof, m_params.method, m_params.windowSize, m_params.sigma);
        if (s) vd->profiles.push_back(s);
    }
    vd->sourceId = input->sourceId;
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
