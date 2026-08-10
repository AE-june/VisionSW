# 골든 수치 대장 (V4)

> **규칙**: 값이 바뀌면 변경이 의도적인지 판단하고, 의도적이면 표를 갱신하며 사유를 적는다.
> 무단 갱신 금지. V3(σ 비교)가 절댓값 이동을 못 잡는 것을 이 대장이 잡는다.

---

## V3 기준선 (반복성 — 변경 불허 기준)

출처: `repeatability_findings.md` · 데이터셋: `D:\Feasibility Study\260511_SDC\0710` (100장)

| 경로 | avg σ (mm) | worst range (mm) | 허용 상한 (×1.10) |
|---|---|---|---|
| Raw (저노출 분리, `recipe_top.json`) | **0.00149** | **0.00798** | 0.00164 / 0.00878 |
| Merged strict (`tolX5/tolY30/gapK0`) | **0.00147** | **0.00788** | 0.00162 / 0.00867 |

V3 판정 기준: `after.csv avg σ ≤ baseline avg σ × 1.10` AND `worst range ≤ baseline × 1.10`.
이 임계를 넘으면 "허용 범위" 처리 금지 — 원인 규명 후 보고.

---

## V4 골든 수치 — 대표 레시피 최종 측정값

커밋: `fa24716` (feat(T0-1/T0-2): 프레임 부여·전파, 포트 기반 병합, 슬롯 다중화)
기록일: 2026-08-04

### recipe_top_merged.json — merged strict 경로

레시피: `D:\Feasibility Study\260511_SDC\0710\recipe_top_merged.json`
데이터: `D:\Feasibility Study\260511_SDC\0710\top_100_zmap\test` (100장)
실행: `VisionEngine.exe --repeat-analyze recipe_top_merged.json <folder> baseline_merged.csv`

| 노드 | 측정값 | Phase 0 기준값 |
|---|---|---|
| PlaneFit | planeA (avg) | *(--repeat-analyze 실행 후 기입)* |
| PlaneFit | planeB (avg) | *(기입 예정)* |
| PlaneFit | tiltDeg (avg) | *(기입 예정)* |
| PlaneFit | rmse (avg, mm) | *(기입 예정)* |
| HeightMeasure | d1..d12 영역별 avg (mm) | *(기입 예정)* |

> **미기입 사유**: 기준 측정값은 VisionEngine 빌드 후 실데이터로 `--repeat-analyze`를 실행해
> `baseline_merged.csv`를 생성한 뒤 기입한다. 데이터 파일은 로컬 스토리지에 있으며 CI에서
> 접근 불가. 사용자가 Phase 0 빌드·검증 시 직접 기입.

### 기입 절차

```
cd D:\GitHub\VisionSWTool\wt-refactor\build
VisionEngine.exe --repeat-analyze \
  "D:\Feasibility Study\260511_SDC\0710\recipe_top_merged.json" \
  "D:\Feasibility Study\260511_SDC\0710\top_100_zmap\test" \
  baseline_phase0.csv
```

CSV의 planeA/planeB/planeC/rmse/tiltDeg 열 평균과 d1..d12 열 평균을 위 표에 기입.

---

## 변경 이력

| 날짜 | 커밋 | 변경 항목 | 사유 |
|---|---|---|---|
| 2026-08-04 | fa24716 | 문서 최초 작성. V4 측정값 미기입(빌드 선행) | Phase 0 — 검증 체계 수립 |
