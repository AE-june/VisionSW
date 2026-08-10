#pragma once
// Aggregate.h — 집계 공용 헬퍼 (NaN 건너뜀)
// ⚠️ ARCH Phase 5 RegionMeasure가 재사용한다. 단일 출처 유지.
// ⚠️ highTail 알고리즘은 구 HeightFromPlaneTool 원형 그대로. 개선 금지.
#include <cstddef>

namespace vision::agg {

// 유효 표본이 0이면 valid=false. n = 유효 표본 수.
struct Result { double value = 0; bool valid = false; std::size_t n = 0; };

Result mean      (const double* v, std::size_t n);
Result median    (const double* v, std::size_t n);
Result maxV      (const double* v, std::size_t n);
Result minV      (const double* v, std::size_t n);
Result stdDev    (const double* v, std::size_t n);            // 표본 표준편차 (n-1)
Result percentile(const double* v, std::size_t n, double p);  // p: 0~100
// 상위 pct% 표본 평균. pct=20 → 상위 20%.
Result highTail  (const double* v, std::size_t n, double pct);

} // namespace vision::agg
