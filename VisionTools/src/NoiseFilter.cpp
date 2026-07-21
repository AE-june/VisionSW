#include "NoiseFilter.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <limits>
#include <cstring>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vision {

static std::vector<float> makeGaussianKernel(int size);   // 전방 선언 (filterZMap보다 아래 정의)

NoiseFilter::NoiseFilter(Params params) : m_params(params) {}

ToolResult NoiseFilter::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "null input" };
    if (input->hasZMap()) return filterZMap(input);   // ZMap 우선
    if (input->hasImage()) return filter2D(input);
    if (input->hasCloud()) return filter3D(input);
    return { ToolStatus::Skip, "no data to filter" };
}

// ─────────────────────────────────────────────────────────────────────
//  filterZMap — ZMap(높이맵) 필터. 전부 NaN(무효 픽셀) 인지: 유효 이웃만 사용.
//   Mean/Median/Gaussian: 평활화 (창 내 유효값으로 대체)
//   SOR: 창 내 유효 이웃의 평균/표준편차로 이상치 판정 → 제거(NaN)
// ─────────────────────────────────────────────────────────────────────
ToolResult NoiseFilter::filterZMap(VisionDataPtr input) {
    const ZMap& src = *input->zmap;
    const int W = src.width, H = src.height;
    if (W <= 0 || H <= 0) return { ToolStatus::Fail, "NoiseFilter: 빈 ZMap" };

    const int kx = std::max(3, m_params.kernelSizeX | 1);   // 홀수 보장
    const int ky = std::max(3, m_params.kernelSizeY | 1);
    const Type type = m_params.type;
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const float EPS = 1e-6f;
    const size_t N = static_cast<size_t>(W) * H;

    const char* modeName = type == Type::Mean ? "mean" : type == Type::Median ? "median"
                         : type == Type::Gaussian ? "gaussian" : type == Type::Bilateral ? "bilateral" : "sor";
    VISION_LOG_INFO("NoiseFilter::filterZMap type={} kx={} ky={} stdRatio={:.2f}",
                    modeName, kx, ky, m_params.stdRatio);

    auto out  = std::make_shared<VisionData>(*input);
    auto zmap = std::make_shared<ZMap>(src);   // 메타데이터+데이터 복사 (ROI 밖은 원본 유지)

    // ── 코어 필터: float Mat(NaN=무효) → 같은 크기 필터 결과 Mat ────────────
    //   전체/부분 영역 공용. 영역 dims는 인자 Mat에서 읽는다.
    // 경계 처리: 모든 창 연산에 BORDER_CONSTANT(0) 사용.
    //  NaN 인지 정규화(num/den)에서 이미지 밖은 값 0·가중치 0 → 자동 제외되므로,
    //  최상/최하단 행도 '실제 이웃'만으로 필터링됨. (BORDER_REPLICATE면 가장자리 행을
    //  복제해 이웃 통계가 자기 편향 → SOR가 가장자리 이상치를 못 걸러내는 문제가 있었음)
    auto filterRegion = [&](const cv::Mat& data) -> cv::Mat {
        const int w = data.cols, h = data.rows;
        const size_t n = static_cast<size_t>(w) * h;
        cv::Mat valid = (data == data);            // CV_8U: 255=유효 (NaN은 자기 자신과 != )
        cv::Mat maskF; valid.convertTo(maskF, CV_32F, 1.0 / 255.0);   // 1.0/0.0
        cv::Mat filled; data.copyTo(filled);
        filled.setTo(0.f, ~valid);                 // NaN→0 (선형필터가 유효값만 합산하도록)

        cv::Mat res(h, w, CV_32F);
        float* rp = res.ptr<float>();
        const uchar* vp = valid.ptr<uchar>();
        const float* dp = reinterpret_cast<const float*>(data.data);

        // NaN 인지 정규화: 유효값 합 / 유효 가중치 합 (선형 필터 mean/gaussian 공용)
        auto normalize = [&](const cv::Mat& num, const cv::Mat& den) {
            const float* np = num.ptr<float>();
            const float* dnp = den.ptr<float>();
            for (size_t i = 0; i < n; ++i)
                rp[i] = (vp[i] && dnp[i] > EPS) ? np[i] / dnp[i] : NaN;
        };

        if (type == Type::Mean) {
            cv::Mat num, den;
            cv::blur(filled, num, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            cv::blur(maskF,  den, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            normalize(num, den);
        }
        else if (type == Type::Gaussian) {
            cv::Mat num, den;
            cv::GaussianBlur(filled, num, cv::Size(kx, ky), 0, 0, cv::BORDER_CONSTANT);
            cv::GaussianBlur(maskF,  den, cv::Size(kx, ky), 0, 0, cv::BORDER_CONSTANT);
            normalize(num, den);
        }
        else if (type == Type::Median) {
            // OpenCV medianBlur: CV_32F는 정사각 커널 3/5만 지원
            const int mk = (std::max(kx, ky) <= 3) ? 3 : 5;
            cv::Mat num, den;
            cv::blur(filled, num, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            cv::blur(maskF,  den, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            const float* np = num.ptr<float>(); const float* dnp = den.ptr<float>();
            cv::Mat base(h, w, CV_32F); float* bp = base.ptr<float>();
            for (size_t i = 0; i < n; ++i)
                bp[i] = vp[i] ? dp[i] : (dnp[i] > EPS ? np[i] / dnp[i] : 0.f);
            cv::Mat m; cv::medianBlur(base, m, mk);
            const float* mp = m.ptr<float>();
            for (size_t i = 0; i < n; ++i) rp[i] = vp[i] ? mp[i] : NaN;
        }
        else if (type == Type::Bilateral) {
            // NaN → 지역평균으로 채운 뒤 bilateral, 이후 원래 NaN 복원
            // bilateral은 정사각 윈도우만 지원 → max(kx,ky) 사용
            const int kb = std::max(kx, ky);
            cv::Mat num, den;
            cv::blur(filled, num, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            cv::blur(maskF,  den, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            const float* np = num.ptr<float>(); const float* dnp = den.ptr<float>();
            cv::Mat base(h, w, CV_32F); float* bp = base.ptr<float>();
            for (size_t i = 0; i < n; ++i)
                bp[i] = vp[i] ? dp[i] : (dnp[i] > EPS ? np[i] / dnp[i] : 0.f);
            double sigmaRangeCount = (src.zResMm > 0)
                ? static_cast<double>(m_params.sigmaRangeMm) / src.zResMm
                : static_cast<double>(m_params.sigmaRangeMm);
            double sigmaSpace = std::max(1.0, kb / 3.0);
            cv::Mat bil;
            cv::bilateralFilter(base, bil, kb, sigmaRangeCount, sigmaSpace, cv::BORDER_CONSTANT);
            const float* bilp = bil.ptr<float>();
            for (size_t i = 0; i < n; ++i) rp[i] = vp[i] ? bilp[i] : NaN;
        }
        else {  // SOR — 이웃 통계(중심 픽셀 제외)로 이상치 판정 → NaN
            cv::Mat num, den, sq, numSq;
            cv::blur(filled, num, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            cv::blur(maskF,  den, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            cv::multiply(filled, filled, sq);
            cv::blur(sq, numSq, cv::Size(kx, ky), cv::Point(-1,-1), cv::BORDER_CONSTANT);
            const float* np = num.ptr<float>(); const float* dnp = den.ptr<float>();
            const float* nsp = numSq.ptr<float>();
            const float k2 = static_cast<float>(kx * ky);
            for (size_t i = 0; i < n; ++i) {
                if (!vp[i]) { rp[i] = NaN; continue; }
                // 중심 픽셀 기여를 제거해 이웃만으로 통계 계산
                float neighborSum   = np[i]  * k2 - dp[i];
                float neighborSumSq = nsp[i] * k2 - dp[i] * dp[i];
                float neighborCnt   = dnp[i] * k2 - 1.f;   // 중심은 항상 유효
                if (neighborCnt <= EPS) { rp[i] = dp[i]; continue; }  // 이웃 없으면 유지
                float mean = neighborSum / neighborCnt;
                float var  = std::max(0.f, neighborSumSq / neighborCnt - mean * mean);
                float sd   = std::sqrt(var);
                rp[i] = (std::abs(dp[i] - mean) > m_params.stdRatio * sd) ? NaN : dp[i];
            }
        }
        return res;
    };

    if (type == Type::Median && std::max(kx, ky) > 5)
        VISION_LOG_WARN("NoiseFilter: median은 커널 3/5만 지원 (요청={}x{})", kx, ky);

    cv::Mat full(H, W, CV_32F, const_cast<float*>(src.data.data()));

    if (m_params.rois.empty()) {
        // ROI 없음 → 전체 이미지 필터 (기존 동작)
        cv::Mat res = filterRegion(full);
        std::memcpy(zmap->data.data(), res.ptr<float>(), sizeof(float) * N);
    } else {
        // ROI별로 (경계 halo 포함) 잘라서 필터 후, halo 제외한 ROI 코어만 되쓰기.
        // halo = max(kx,ky) → ROI 내부 결과는 전체 필터와 동일 (커널 도달범위 확보).
        const int halo = std::max(kx, ky);
        for (const auto& roi : m_params.rois) {
            int rx0 = std::clamp(static_cast<int>(roi.xPct * W), 0, W - 1);
            int ry0 = std::clamp(static_cast<int>(roi.yPct * H), 0, H - 1);
            int rx1 = std::clamp(static_cast<int>((roi.xPct + roi.wPct) * W), 0, W);
            int ry1 = std::clamp(static_cast<int>((roi.yPct + roi.hPct) * H), 0, H);
            if (rx1 <= rx0 || ry1 <= ry0) continue;

            int ex0 = std::max(0, rx0 - halo), ey0 = std::max(0, ry0 - halo);
            int ex1 = std::min(W, rx1 + halo), ey1 = std::min(H, ry1 + halo);
            cv::Mat sub = full(cv::Rect(ex0, ey0, ex1 - ex0, ey1 - ey0)).clone();
            cv::Mat subRes = filterRegion(sub);

            for (int row = ry0; row < ry1; ++row) {
                const float* sr = subRes.ptr<float>(row - ey0);
                float* dz = &zmap->data[static_cast<size_t>(row) * W];
                for (int col = rx0; col < rx1; ++col)
                    dz[col] = sr[col - ex0];
            }
        }
    }
    out->zmap = zmap;
    return { ToolStatus::Ok, "", out };
}

// Separable Gaussian kernel
static std::vector<float> makeGaussianKernel(int size) {
    if (size % 2 == 0) ++size;
    std::vector<float> k(size);
    int half = size / 2;
    float sigma = std::max(1.f, half / 2.f);
    float sum = 0.f;
    for (int i = 0; i < size; ++i) {
        float x = static_cast<float>(i - half);
        k[i] = std::exp(-0.5f * x * x / (sigma * sigma));
        sum += k[i];
    }
    for (auto& v : k) v /= sum;
    return k;
}

ToolResult NoiseFilter::filter2D(VisionDataPtr input) {
    VISION_LOG_DEBUG("NoiseFilter::filter2D kernel={}", m_params.kernelSizeX);
    const auto& src = *input->image;
    int W = src.width, H = src.height, C = src.channels;

    auto out = std::make_shared<VisionData>(*input);
    out->image = std::make_shared<Image2D>();
    out->image->width = W;
    out->image->height = H;
    out->image->channels = C;
    out->image->data.resize(static_cast<size_t>(W) * H * C);

    int k = std::max(3, m_params.kernelSizeX | 1);  // ensure odd
    auto kernel = makeGaussianKernel(k);
    int half = k / 2;

    // Horizontal pass into temp buffer
    std::vector<float> tmp(static_cast<size_t>(W) * H * C);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int c = 0; c < C; ++c) {
                float sum = 0.f;
                for (int dx = -half; dx <= half; ++dx) {
                    int xx = std::clamp(x + dx, 0, W - 1);
                    sum += kernel[dx + half] * src.data[(y * W + xx) * C + c];
                }
                tmp[(y * W + x) * C + c] = sum;
            }
        }
    }

    // Vertical pass to output
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int c = 0; c < C; ++c) {
                float sum = 0.f;
                for (int dy = -half; dy <= half; ++dy) {
                    int yy = std::clamp(y + dy, 0, H - 1);
                    sum += kernel[dy + half] * tmp[(yy * W + x) * C + c];
                }
                out->image->data[(y * W + x) * C + c] =
                    static_cast<uint8_t>(std::clamp(sum, 0.f, 255.f));
            }
        }
    }

    return { ToolStatus::Ok, "", out };
}

ToolResult NoiseFilter::filter3D(VisionDataPtr input) {
    VISION_LOG_DEBUG("NoiseFilter::filter3D radius={} minNeighbors={}",
                     m_params.radius, m_params.minNeighbors);
    const auto& src = *input->cloud;

    // Guard against excessive computation for very large clouds
    const size_t maxBruteForce = 50000;
    if (src.points.size() > maxBruteForce) {
        VISION_LOG_WARN("NoiseFilter::filter3D: {} points exceeds limit, skipping",
                        src.points.size());
        return { ToolStatus::Ok, "cloud too large, skipped", input };
    }

    float r2 = m_params.radius * m_params.radius;
    int minN = m_params.minNeighbors;

    auto out = std::make_shared<VisionData>(*input);
    out->cloud = std::make_shared<PointCloud3D>();
    out->cloud->frameId = src.frameId;

    for (size_t i = 0; i < src.points.size(); ++i) {
        const auto& p = src.points[i];
        int cnt = 0;
        for (size_t j = 0; j < src.points.size(); ++j) {
            if (i == j) continue;
            const auto& q = src.points[j];
            float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
            if (dx*dx + dy*dy + dz*dz <= r2) {
                if (++cnt >= minN) break;
            }
        }
        if (cnt >= minN) out->cloud->points.push_back(p);
    }

    VISION_LOG_DEBUG("NoiseFilter::filter3D {} → {} points",
                     src.points.size(), out->cloud->points.size());
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
