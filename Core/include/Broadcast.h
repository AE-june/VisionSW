#pragma once
#include <vector>
#include <cstddef>

namespace vision {

// ─────────────────────────────────────────────────────────────────
//  브로드캐스트 규칙 (설계 §4.4)
//  노드의 배열 입력 길이들로 실행 횟수 N을 결정한다.
//    - 배열 입력 없음 또는 전부 길이 1        → N = 1 (현재와 동일, 1회 실행)
//    - 길이 N(>1) 배열 존재                    → N회 실행.
//        · 스칼라/길이-1 배열은 매 반복 동일값으로 확장.
//        · 다중 길이가 서로 다르면(둘 다 1 아님) → Fail.
//    - 길이 0 배열                              → N = 0 (0회 실행, 빈 출력).
//  출력 다중성 = N.
//
//  arrayLengths: 배열 입력들의 길이만 넘긴다(스칼라 입력은 포함하지 않음).
//  이 함수는 순수(부작용 없음) — 단위 테스트로 규칙을 못박는다. 실제 실행 루프
//  배선은 배열 생산 노드(T2-1 ConnectedComponents)가 생기는 시점에 연결한다.
// ─────────────────────────────────────────────────────────────────
struct BroadcastPlan {
    std::size_t count = 1;   // 실행 횟수 N
    bool        ok    = true; // 길이 불일치면 false
};

inline BroadcastPlan computeBroadcast(const std::vector<std::size_t>& arrayLengths) {
    std::size_t n = 1;
    bool nSet = false;   // 다중 길이(>=1, 단 확장 대상 1 제외)를 한 번이라도 채택했는가
    for (std::size_t len : arrayLengths) {
        if (len == 1) continue;              // 길이-1 배열은 스칼라처럼 확장 → N에 영향 없음
        if (!nSet) { n = len; nSet = true; } // 첫 비-1 길이 채택 (0 포함)
        else if (n != len) return { 0, false };
    }
    return { n, true };
}

} // namespace vision
