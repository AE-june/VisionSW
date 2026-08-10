#pragma once
// SyntheticFixtures.h — V1 합성 데이터 생성기
// 규칙: 모든 난수는 시드 고정. 정답 계산은 소비자가 독립 경로로.
// 이 파일은 생성만 한다 — 테스트가 구현과 같은 식을 쓰지 않도록.

#include "HeightMap.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace vision::test {

static constexpr double kPi = 3.14159265358979323846;

// ── 결정론적 LCG (Knuth MMIX 계수) ──────────────────────────────────────

inline uint64_t lcgNext(uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
}

inline double lcgDouble(uint64_t& state) {
    return static_cast<double>(lcgNext(state) >> 11) / static_cast<double>(1ULL << 53);
}

// Box-Muller: LCG → N(0,1)
inline double lcgGaussian(uint64_t& state) {
    double u1 = lcgDouble(state);
    if (u1 < 1e-15) u1 = 1e-15;
    double u2 = lcgDouble(state);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
}

// ── 내부 헬퍼 ───────────────────────────────────────────────────────────

inline HeightMap makeBlank(int w, int h, float xRes, float yRes, float zRes) {
    HeightMap m;
    m.width = w; m.height = h; m.channels = 1;
    m.xResMm = xRes; m.yResMm = yRes;
    m.zResMm = zRes; m.zZeroCount = 0.f;
    m.data.assign(static_cast<size_t>(w) * h, 0.f);
    return m;
}

// ── 생성기 (문서 §1 V1 시그니처) ─────────────────────────────────────────

// 기울어진 평면: z(col,row) = a * xMm + b * yMm + c  (mm)
// 정답: PlaneFit이 a,b,c를 복원해야 함. Level(distance) 출력이 전부 0.
// 허용오차: float 격자 왕복 ≤ zResMm/2
inline HeightMap makeTiltedPlane(int w, int h,
                                  double a, double b, double c,
                                  float xRes = 0.05f,
                                  float yRes = 0.05f,
                                  float zRes = 0.001f) {
    HeightMap m = makeBlank(w, h, xRes, yRes, zRes);
    for (int r = 0; r < h; ++r) {
        for (int col = 0; col < w; ++col) {
            double xMm = col * static_cast<double>(xRes);
            double yMm = r   * static_cast<double>(yRes);
            double zMm = a * xMm + b * yMm + c;
            m.data[static_cast<size_t>(r) * w + col] = static_cast<float>(zMm / zRes);
        }
    }
    return m;
}

// 계단: col < stepCol → baseZmm, col >= stepCol → baseZmm + stepZmm
// 정답: 엣지 위치 = stepCol, 높이 차 = stepZmm
inline HeightMap makeStep(int w, int h, int stepCol,
                           double baseZmm, double stepZmm,
                           float xRes = 0.05f,
                           float yRes = 0.05f,
                           float zRes = 0.001f) {
    HeightMap m = makeBlank(w, h, xRes, yRes, zRes);
    for (int r = 0; r < h; ++r) {
        for (int col = 0; col < w; ++col) {
            double zMm = (col < stepCol) ? baseZmm : (baseZmm + stepZmm);
            m.data[static_cast<size_t>(r) * w + col] = static_cast<float>(zMm / zRes);
        }
    }
    return m;
}

// 원형 구멍: 원 내부 NaN, 외부 zFloorMm
// 정답: 구멍 면적 ≈ π * rMm² (픽셀 단위 근사)
inline HeightMap makeHole(int w, int h,
                           double cxMm, double cyMm, double rMm,
                           double zFloorMm = 0.0,
                           float xRes = 0.05f,
                           float yRes = 0.05f,
                           float zRes = 0.001f) {
    HeightMap m = makeBlank(w, h, xRes, yRes, zRes);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int r = 0; r < h; ++r) {
        for (int col = 0; col < w; ++col) {
            double xMm = col * static_cast<double>(xRes);
            double yMm = r   * static_cast<double>(yRes);
            double dx  = xMm - cxMm, dy = yMm - cyMm;
            m.data[static_cast<size_t>(r) * w + col] =
                (dx*dx + dy*dy <= rMm*rMm) ? nan
                                            : static_cast<float>(zFloorMm / zRes);
        }
    }
    return m;
}

// 돌기 격자: nx×ny 개의 Gaussian bump
// bumpRmm = 돌기 σ(mm), bumpZmm = 돌기 최대 높이(mm)
// 정답: 돌기 수 = nx*ny, 각 돌기 최대값 ≈ bumpZmm
inline HeightMap makeBumpGrid(int w, int h,
                               int nx, int ny,
                               double bumpZmm, double bumpRmm,
                               float xRes = 0.05f,
                               float yRes = 0.05f,
                               float zRes = 0.001f) {
    HeightMap m = makeBlank(w, h, xRes, yRes, zRes);
    double widthMm  = (w - 1) * static_cast<double>(xRes);
    double heightMm = (h - 1) * static_cast<double>(yRes);
    for (int r = 0; r < h; ++r) {
        for (int col = 0; col < w; ++col) {
            double xMm = col * static_cast<double>(xRes);
            double yMm = r   * static_cast<double>(yRes);
            double maxContrib = 0.0;
            for (int iy = 0; iy < ny; ++iy) {
                double by = (iy + 0.5) / ny * heightMm;
                for (int ix = 0; ix < nx; ++ix) {
                    double bx = (ix + 0.5) / nx * widthMm;
                    double dx = xMm - bx, dy = yMm - by;
                    double contrib = bumpZmm *
                        std::exp(-(dx*dx + dy*dy) / (2.0 * bumpRmm * bumpRmm));
                    if (contrib > maxContrib) maxContrib = contrib;
                }
            }
            m.data[static_cast<size_t>(r) * w + col] =
                static_cast<float>(maxContrib / zRes);
        }
    }
    return m;
}

// 지정 비율의 픽셀을 NaN으로 결정론적으로 주입 (시드 고정)
// 허용오차: 실제 NaN 비율 ≈ ratio (연속 픽셀 ±1)
inline void injectInvalid(HeightMap& m, double ratio, uint32_t seed) {
    uint64_t state = static_cast<uint64_t>(seed) * 6364136223846793005ULL + 1;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (std::size_t i = 0; i < m.data.size(); ++i) {
        if (lcgDouble(state) < ratio) m.data[i] = nan;
    }
}

// 지정 σ의 Gaussian 노이즈를 결정론적으로 주입 (시드 고정, NaN 픽셀 건드리지 않음)
inline void injectNoise(HeightMap& m, double sigmaMm, uint32_t seed) {
    uint64_t state = static_cast<uint64_t>(seed) * 6364136223846793005ULL + 1;
    for (std::size_t i = 0; i < m.data.size(); ++i) {
        if (!std::isnan(m.data[i]))
            m.data[i] += static_cast<float>(lcgGaussian(state) * sigmaMm / m.zResMm);
    }
}

} // namespace vision::test
