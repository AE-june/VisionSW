#pragma once
#include "IAlgorithmTool.h"

namespace vision {

// ─────────────────────────────────────────────
//  CloudSorFilterTool — PointCloud3D 통계적 이상치 제거(SOR: Statistical Outlier Removal),
//  균일 공간 격자(uniform grid hash) 기반 이웃 탐색으로 대용량 클라우드에서도 실용적 속도.
//  (기존 NoiseFilterTool::filter3D는 브루트포스 O(n²)에 5만 점 초과 시 필터를 건너뛰던
//   레거시 경로 — 이 툴이 그 대체.)
//
//  알고리즘(PCL SOR과 동일한 통계 기준):
//    1. 점마다 k-최근접 이웃까지의 평균거리를 구한다(격자 셀 + 주변 셀만 탐색 — 브루트포스 아님).
//    2. 전체 점의 평균거리들로 전역 평균μ·표준편차σ를 구한다.
//    3. meanDist > μ + stdRatio·σ 인 점을 이상치로 제거.
//  (한쪽 방향만 검사 — 이웃과 너무 가까운 건 노이즈가 아니라 정상 밀집이므로 제거 안 함)
//
//  이웃 탐색: 점의 셀을 중심으로 링을 1칸씩 넓혀가며 후보를 모으고, k개 이상 모인 뒤
//  한 링을 더 넓혀 더 가까운 점이 있는지 확인 후 k개를 추린다(완전탐색 대비 근사지만
//  실용적으로 충분 — cellSizeMm이 점 간격보다 지나치게 크면 근사 오차가 커짐).
//
//  stdRatio 기본값에 대해: 실제 스캔면의 경계(edge) 점들은 노이즈가 아니어도 한쪽 방향에
//  이웃이 없어 k-최근접 평균거리가 내부 점보다 구조적으로 크다 — 전역 임계값 하나로 판정하는
//  SOR의 근본적 특성(PCL도 동일)이라 완전히 없앨 수는 없다. stdRatio=1.0은 균일한 격자
//  테스트에서 σ가 거의 0에 가까워질 만큼 데이터가 조밀할 때 경계점의 20%+ 를 오탐 제거했고,
//  stdRatio=2.0에서는 진짜 이상치는 여전히 잘 걸러내면서 오탐이 ~3%로 크게 줄어 기본값으로 채택.
// ─────────────────────────────────────────────
struct CloudSorFilterParams {
    double cellSizeMm = 0.02;   // 공간 해시 격자 셀 크기(mm) — 점 간격과 비슷한 정도로 설정
    int    kNeighbors = 8;      // k-최근접 이웃 개수
    double stdRatio   = 2.0;    // meanDist > 전역평균 + stdRatio*표준편차 → 이상치로 제거 (경계점 오탐 방지 위해 2.0)
};

class CloudSorFilterTool : public IAlgorithmTool {
public:
    explicit CloudSorFilterTool(CloudSorFilterParams p = {}) : m_p(p) {}
    std::string name() const override { return "CloudSorFilter"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    CloudSorFilterParams m_p;
};

} // namespace vision
