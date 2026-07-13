# 반복성(Repeatability) 분석 — TOP merged 워크플로우

대상: `D:\Feasibility Study\260511_SDC\0710` · 원본 100장(`top_100_zmap/test`)
흐름: `merge.json`(ExposureMerge2로 머지 이미지 생성) → `recipe_top_merged.json`
(glass 19개 ref ROI로 PlaneFit → measure 12개 ROI의 최대높이 HighTail 0.5% 대비 평면).
반복성 = 동일 대상 100 스캔에서 영역별 측정값 산포(σ, range).

## 결과 (100장, 영역별 σ 평균 / 최악 range, mm)
| 구성 | avg σ | worst range | 평면 tilt σ |
|---|---|---|---|
| **Raw**(recipe_top, 저노출 분리) | **0.00149** | 0.00798 | 0.0007° |
| Merged **loose**(기존 tolX10/tolY100/gapK2) | 0.03619 | 0.14637 | 0.0818° |
| Merged loose + 평면 RANSAC thr 0.5→0.1 | 0.03367 | 0.19386 | 0.0075° |
| **Merged strict**(tolX5/tolY30/gapK0) | **0.00147** | 0.00788 | 0.0018° |

## 진단
1. **merged가 raw보다 ~24배 나쁨.** 전 영역에서 merged σ가 raw의 5.7~74.5배.
2. **원인은 평면이 아니라 measure 영역의 고노출 fill 스파이크.** 평면 RANSAC을
   조여 평면을 10배 안정화해도(tilt σ 0.082→0.0075) 영역 σ는 거의 안 변함(0.036→0.034).
3. **ExposureMerge2의 느슨한 리플렉션 제거**(tolX10/tolY100/gapK2)가 저노출이 못 본
   영역을 불안정한 고노출로 채우고, 그 fill 스파이크(반사)를 연속성 필터가 일관되게
   제거하지 못함. HighTail(상위 0.5% 최대높이)이 이 스파이크를 잡아 측정이 ±0.1mm로
   요동(loose는 영역 평균도 raw 대비 -0.063~+0.118mm로 제멋대로).
   glass·투명 영역이 특히 심해 사용자 가설(glass 영역 데이터)과 부합.

## 해결
ExposureMerge2 리플렉션 제거를 **strict**로: `tolX=5, tolY=30, gapK=0`.
불안정한 fill 스파이크를 제거 → merged 측정이 raw 저노출 경로와
**값(영역 평균 +0.001~+0.021mm 일관 바이어스)·반복성(σ 0.0015mm)** 모두 일치.
평면 RANSAC threshold 변경은 불필요.

- 적용: 사용자 워크플로우의 `D:\Feasibility Study\260511_SDC\0710\merge.json`
  node-2 파라미터를 strict로 업데이트함.
- 코드 기본값(tolX10/tolY100/gapK2)은 다른 용도(구멍 메우기 완성도) 때문에 유지.
  측정·반복성 목적 레시피에서는 strict 권장.

## 검증 도구
`VisionEngine --repeat-analyze <recipe.json> <folder> <out.csv>` (이 브랜치 추가):
레시피를 폴더 전 이미지에 헤드리스로 적용해 영역별 높이·PlaneFit 파라미터를 수집,
영역별 σ/range를 산출. `runPipeline`을 conn 포인터화(널이면 헤드리스)해 재사용.
