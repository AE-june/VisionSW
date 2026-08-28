#pragma once
// ExposureFilter 코어 — split 없는 3노출 인터리브 HeightMap을 그대로 필터링.
//  격자: h행×w열, 클래스 e = r%3 (0=저,1=중,2=장). 값은 raw count.
//  Stage 0: 클래스별 Z datum 정규화(교대 추정) → z'(r,c) = z − offset[r%3].
//  Stage 1: 대칭 로컬 일관성(자기 제외 Y선피팅) → 이상치 픽셀 NaN 마킹.
//  Stage 2: 세로(Y) 선형보간 gap fill (streak > maxGapRows면 NaN 유지).
//  EM3와 달리 lo/mid/hi를 대칭 처리 → 저노출 리플렉션도 걸러진다.
//  ⚠ Z를 클래스 간 평균하지 않는다(3표본은 1피치씩 다른 지점 = Y블러). 각 행은 자기 Y값 유지.
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <mutex>
#include <opencv2/core.hpp>

namespace vision {

struct ExposureFilterParams {
    int   datumWindow      = 9;    // Stage0 선피팅 윈도우(3의 배수 강제 → 주기3 리플 상쇄)
    int   datumIters       = 3;    // Stage0 교대 추정 반복
    float tauBase          = 30.f; // Stage1 기본 tolerance(count)
    float tauSlope         = 0.5f; // Stage1 기울기 비례항 (τ = tauBase + tauSlope*|g|)
    int   consistWindow    = 9;    // Stage1 일관성 윈도우
    int   minClassNeighbors= 2;    // 근거 가드: 서로 다른 2개 이상 클래스에서 각각 이만큼 유효해야 판정
    int   maxGapRows       = 6;    // Stage2 세로 보간 최대 streak
};

// 한 열(column) 방향 로버스트 선피팅. z는 full 격자(index = row*w + c).
//  [r-hw, r+hw] 윈도우, NaN 제외, (excludeCenter면 r행 제외). OLS + Huber IRLS.
//  pred = x=0(=r행) 위치 예측값(intercept), slope = count/row 기울기.
//  classCnt[3] = 사용된 이웃의 클래스별(rr%3) 개수.
static inline void efRobustColFit(const float* z, int w, int h, int r, int c,
                                  int hw, bool excludeCenter,
                                  float& pred, float& slope, int& nUsed, int classCnt[3])
{
    classCnt[0] = classCnt[1] = classCnt[2] = 0;
    float xs[64], ys[64]; int n = 0;
    const int r0 = std::max(0, r - hw), r1 = std::min(h - 1, r + hw);
    for (int rr = r0; rr <= r1; ++rr) {
        if (excludeCenter && rr == r) continue;
        float v = z[(size_t)rr * w + c];
        if (std::isnan(v)) continue;
        classCnt[rr % 3]++;
        if (n < 64) { xs[n] = (float)(rr - r); ys[n] = v; ++n; }
    }
    nUsed = n;
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    if (n == 0) { pred = NaN; slope = 0.f; return; }
    if (n < 3) { float s = 0.f; for (int k = 0; k < n; ++k) s += ys[k]; pred = s / n; slope = 0.f; return; }

    float wts[64]; for (int k = 0; k < n; ++k) wts[k] = 1.f;
    float a = 0.f, b = 0.f;
    for (int it = 0; it < 3; ++it) {
        double Sw = 0, Swx = 0, Swy = 0, Swxx = 0, Swxy = 0;
        for (int k = 0; k < n; ++k) {
            double wk = wts[k], x = xs[k], y = ys[k];
            Sw += wk; Swx += wk * x; Swy += wk * y; Swxx += wk * x * x; Swxy += wk * x * y;
        }
        double det = Sw * Swxx - Swx * Swx;
        if (std::fabs(det) < 1e-9) { b = 0.f; a = (float)(Swy / std::max(1e-9, Sw)); }
        else { b = (float)((Sw * Swxy - Swx * Swy) / det); a = (float)((Swy - b * Swx) / Sw); }
        // 잔차 → MAD → Huber 가중 갱신
        float absr[64];
        for (int k = 0; k < n; ++k) absr[k] = std::fabs(ys[k] - (a + b * xs[k]));
        float tmp[64]; for (int k = 0; k < n; ++k) tmp[k] = absr[k];
        std::nth_element(tmp, tmp + n / 2, tmp + n);
        float sigma = std::max(1e-3f, tmp[n / 2] * 1.4826f);
        float cc = 1.345f * sigma;
        for (int k = 0; k < n; ++k) wts[k] = absr[k] <= cc ? 1.f : cc / std::max(1e-6f, absr[k]);
    }
    pred = a; slope = b;
}

static inline float efMedian(std::vector<float>& v) {
    if (v.empty()) return 0.f;
    size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + m, v.end());
    return v[m];
}

// 코어 실행: zin(h×w, NaN=무효) → zFilled(최종). offsetOut/madOut은 진단.
//  zNormOut(z') / removeMaskOut(1=제거) 는 프리뷰용(nullptr면 생략).
inline void exposureFilterRun(const float* zin, int w, int h, const ExposureFilterParams& Pin,
                              std::vector<float>& zFilled,
                              float offsetOut[3], float madOut[3],
                              std::vector<float>* zNormOut = nullptr,
                              std::vector<uint8_t>* removeMaskOut = nullptr)
{
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const size_t HW = (size_t)h * w;
    ExposureFilterParams P = Pin;
    if (P.datumWindow % 3 != 0) P.datumWindow += (3 - P.datumWindow % 3);   // 3의 배수 강제
    const int hwDatum   = std::max(1, P.datumWindow / 2);
    const int hwConsist = std::max(1, P.consistWindow / 2);

    // ── Stage 0: 클래스별 offset 교대 추정 ────────────────────────────────
    float offset[3] = { 0.f, 0.f, 0.f };
    madOut[0] = madOut[1] = madOut[2] = 0.f;
    std::vector<float> zp(HW);
    const int STRIDE = 4;   // offset median용 열 서브샘플
    for (int iter = 0; iter < std::max(1, P.datumIters); ++iter) {
        // z' 재계산
        cv::parallel_for_(cv::Range(0, h), [&](const cv::Range& rg) {
            for (int r = rg.start; r < rg.end; ++r) {
                float off = offset[r % 3];
                for (int c = 0; c < w; ++c) { size_t i = (size_t)r * w + c; float v = zin[i]; zp[i] = std::isnan(v) ? NaN : v - off; }
            }
        });
        // 잔차 수집(클래스별) — 열 서브샘플 병렬, 뮤텍스 병합
        std::vector<float> d0, d1, d2;
        std::mutex mtx;
        cv::parallel_for_(cv::Range(0, (w + STRIDE - 1) / STRIDE), [&](const cv::Range& rg) {
            std::vector<float> l0, l1, l2;
            for (int ci = rg.start; ci < rg.end; ++ci) {
                int c = ci * STRIDE; if (c >= w) continue;
                for (int r = 0; r < h; ++r) {
                    float v = zp[(size_t)r * w + c];
                    if (std::isnan(v)) continue;
                    float pred, slope; int nu, cc[3];
                    efRobustColFit(zp.data(), w, h, r, c, hwDatum, false, pred, slope, nu, cc);
                    if (std::isnan(pred) || nu < 3) continue;
                    float d = v - pred;
                    switch (r % 3) { case 0: l0.push_back(d); break; case 1: l1.push_back(d); break; default: l2.push_back(d); }
                }
            }
            std::lock_guard<std::mutex> lk(mtx);
            d0.insert(d0.end(), l0.begin(), l0.end());
            d1.insert(d1.end(), l1.begin(), l1.end());
            d2.insert(d2.end(), l2.begin(), l2.end());
        });
        float m0 = efMedian(d0), m1 = efMedian(d1), m2 = efMedian(d2);
        offset[0] += m0; offset[1] += m1; offset[2] += m2;
        float mean = (offset[0] + offset[1] + offset[2]) / 3.f;
        offset[0] -= mean; offset[1] -= mean; offset[2] -= mean;
        // 마지막 반복의 MAD(클래스별) 진단
        if (iter == std::max(1, P.datumIters) - 1) {
            auto mad = [](std::vector<float>& d, float med) {
                if (d.empty()) return 0.f;
                for (auto& x : d) x = std::fabs(x - med);
                return efMedian(d) * 1.4826f;
            };
            madOut[0] = mad(d0, m0); madOut[1] = mad(d1, m1); madOut[2] = mad(d2, m2);
        }
    }
    offsetOut[0] = offset[0]; offsetOut[1] = offset[1]; offsetOut[2] = offset[2];
    // 최종 z' 확정
    cv::parallel_for_(cv::Range(0, h), [&](const cv::Range& rg) {
        for (int r = rg.start; r < rg.end; ++r) {
            float off = offset[r % 3];
            for (int c = 0; c < w; ++c) { size_t i = (size_t)r * w + c; float v = zin[i]; zp[i] = std::isnan(v) ? NaN : v - off; }
        }
    });
    if (zNormOut) *zNormOut = zp;

    // ── Stage 1: 대칭 로컬 일관성 → 제거 마스크 ───────────────────────────
    std::vector<uint8_t> remove(HW, 0);   // 1 = 제거
    cv::parallel_for_(cv::Range(0, w), [&](const cv::Range& rg) {
        for (int c = rg.start; c < rg.end; ++c) {
            for (int r = 0; r < h; ++r) {
                size_t i = (size_t)r * w + c;
                float v = zp[i];
                if (std::isnan(v)) continue;
                float pred, slope; int nu, cc[3];
                efRobustColFit(zp.data(), w, h, r, c, hwConsist, true, pred, slope, nu, cc);
                // 근거 가드: 서로 다른 2개 이상 클래스가 각각 minClassNeighbors 이상? 아니면 보존.
                int nClass = (cc[0] >= P.minClassNeighbors) + (cc[1] >= P.minClassNeighbors) + (cc[2] >= P.minClassNeighbors);
                if (std::isnan(pred) || nClass < 2) continue;
                float tau = P.tauBase + P.tauSlope * std::fabs(slope);
                if (std::fabs(v - pred) > tau) remove[i] = 1;
            }
        }
    });
    if (removeMaskOut) *removeMaskOut = remove;

    // 제거 적용 → zRem (제거 픽셀 NaN)
    std::vector<float> zRem(HW);
    cv::parallel_for_(cv::Range(0, (int)((HW + 4095) / 4096)), [&](const cv::Range& rg) {
        for (int b = rg.start; b < rg.end; ++b) {
            size_t s = (size_t)b * 4096, e = std::min(HW, s + 4096);
            for (size_t i = s; i < e; ++i) zRem[i] = remove[i] ? NaN : zp[i];
        }
    });

    // ── Stage 2: 세로 선형보간 gap fill ──────────────────────────────────
    zFilled = zRem;
    cv::parallel_for_(cv::Range(0, w), [&](const cv::Range& rg) {
        for (int c = rg.start; c < rg.end; ++c) {
            int r = 0;
            while (r < h) {
                if (!std::isnan(zRem[(size_t)r * w + c])) { ++r; continue; }
                int r0 = r;
                while (r < h && std::isnan(zRem[(size_t)r * w + c])) ++r;
                int r1 = r;                       // [r0, r1) NaN run
                int va = r0 - 1, vb = r1;          // 위/아래 유효행
                if (va >= 0 && vb < h && (r1 - r0) <= P.maxGapRows) {
                    float za = zRem[(size_t)va * w + c], zb = zRem[(size_t)vb * w + c];
                    float span = (float)(vb - va);
                    for (int rr = r0; rr < r1; ++rr)
                        zFilled[(size_t)rr * w + c] = za + (zb - za) * ((rr - va) / span);
                }
            }
        }
    });
}

} // namespace vision
