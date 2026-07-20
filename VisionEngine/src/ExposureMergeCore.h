#pragma once
// 이중노출 머지 "결정" 코어 — ZMap 경로와 PointCloud 경로가 공유.
//  본질은 Z 그리드 결정 연산(오프셋 보정 → 저노출우선 → 연속성 BFS 리플렉션 제거).
//  low/high = bn*w Z 그리드(NaN=무효, 짝수/홀수 프로파일에서 분리된 저/고 노출).
//  출력 source[i]: 0=제거, 1=저노출(값=low[i]-offset), 2=장노출(값=high[i]).
//  forcedOffset가 finite면 그대로 사용(청크/전역 오프셋 공유용), NaN이면 (low-high) 중앙값 산출.
//  반환: 사용된 offset. (X/Y는 쓰지 않음 — 호출측이 source로 Z든 점이든 적용)
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <opencv2/core.hpp>

namespace vision {

// 재사용 가능한 작업 버퍼 — 밴드/청크 병렬에서 밴드 슬롯마다 하나씩 두고 재사용하면
//  결정 호출마다 큰 벡터를 새로 할당하는 힙 경합을 없앤다(nullptr면 함수가 로컬로 할당 = 기존 동작).
struct ExposureMergeScratch {
    std::vector<float> lowC, merged;
    std::vector<uint8_t> mvalid, visited;
    std::vector<int> q;
};

inline float exposureMergeDecision(
    const float* low, const float* high,
    int w, int bn, float matchTol, float tolX, float tolY, int gapK,
    float forcedOffset, std::vector<uint8_t>& source,
    ExposureMergeScratch* scratch = nullptr)
{
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const size_t BN = (size_t)bn * w;

    // ② 오프셋: 겹침 일치 픽셀 (low-high) 중앙값(stride-4 서브샘플). forcedOffset 우선.
    float offset;
    if (!std::isnan(forcedOffset)) {
        offset = forcedOffset;
    } else {
        std::vector<float> diffs; diffs.reserve(BN/4 + 1);
        for (size_t i = 0; i < BN; i += 4)
            if (!std::isnan(low[i]) && !std::isnan(high[i]) && std::fabs(low[i]-high[i]) <= matchTol)
                diffs.push_back(low[i]-high[i]);
        offset = 0.f;
        if (!diffs.empty()) { size_t mid=diffs.size()/2; std::nth_element(diffs.begin(),diffs.begin()+mid,diffs.end()); offset=diffs[mid]; }
    }

    // 작업 버퍼(스크래치) — 제공되면 재사용, 아니면 로컬. 크기는 BN으로 맞춤(용량 유지 → 재할당 회피).
    ExposureMergeScratch localScratch;
    ExposureMergeScratch& s = scratch ? *scratch : localScratch;
    s.lowC.resize(BN); s.merged.resize(BN); s.mvalid.resize(BN);
    float*   lowC   = s.lowC.data();
    float*   merged = s.merged.data();
    uint8_t* mvalid = s.mvalid.data();

    // ③ 저노출우선 머지용 lowC/merged/유효마스크 (BFS 조회용)
    cv::parallel_for_(cv::Range(0, bn), [&](const cv::Range& rg) {
        for (size_t i=(size_t)rg.start*w; i<(size_t)rg.end*w; ++i) {
            float lc = std::isnan(low[i]) ? NaN : low[i]-offset;
            lowC[i] = lc;
            float m = !std::isnan(lc) ? lc : (!std::isnan(high[i]) ? high[i] : NaN);
            merged[i] = m; mvalid[i] = std::isnan(m) ? 0 : 1;
        }
    });

    // ④ 연속성 필터: 씨앗 = 저노출 유효 && 겹침 일치. 경계 씨앗만 큐에.
    s.visited.assign(BN, 0);
    uint8_t* visited = s.visited.data();
    for (size_t i = 0; i < BN; ++i)
        if (!std::isnan(lowC[i]) && !std::isnan(high[i]) && std::fabs(lowC[i]-high[i]) <= matchTol) visited[i]=1;
    std::vector<int>& q = s.q; q.clear(); q.reserve(BN/2 + 1);
    for (int r = 0; r < bn; ++r) for (int c = 0; c < w; ++c) {
        size_t i=(size_t)r*w+c;
        if (!visited[i]) continue;
        if ((c>0 && !visited[i-1]) || (c<w-1 && !visited[i+1]) ||
            (r>0 && !visited[i-w]) || (r<bn-1 && !visited[i+w])) q.push_back((int)i);
    }
    auto tryRay = [&](int r, int c, float z, int dr, int dc, float tol) {
        for (int k = 1; k <= gapK+1; ++k) {
            int nr=r+dr*k, nc=c+dc*k;
            if (nr<0||nr>=bn||nc<0||nc>=w) break;
            size_t ni=(size_t)nr*w+nc;
            if (visited[ni]) break;
            if (!mvalid[ni]) continue;
            if (std::fabs(merged[ni]-z) <= tol*k) { visited[ni]=1; q.push_back((int)ni); }
            break;
        }
    };
    while (!q.empty()) {
        int idx=q.back(); q.pop_back();
        int r=idx/w, c=idx%w; float z=merged[idx];
        tryRay(r,c,z, 0,-1,tolX); tryRay(r,c,z, 0,1,tolX);
        tryRay(r,c,z,-1, 0,tolY); tryRay(r,c,z, 1,0,tolY);
    }

    // source: 저노출 유효→1, (저 무효 & 고 유효 & 연결됨)→2, 그 외→0(제거)
    source.assign(BN, 0);
    for (size_t i = 0; i < BN; ++i)
        source[i] = !std::isnan(lowC[i]) ? 1 : ((!std::isnan(high[i]) && visited[i]) ? 2 : 0);
    return offset;
}

} // namespace vision
