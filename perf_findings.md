# 노드 속도 개선 (결과 보존) — 프로파일 & 조치

대표 레시피 `recipe_top_merged.json`(TOP, 7400×4638=34M px ZMap) 1장 노드별 실측.
원칙: **출력 결과 불변**(부동소수 오차 이내)인 최적화만.

## 프로파일 (개선 전, ms)
| 노드 | ms | 성격 |
|---|---|---|
| ZMapLoader | 359 | PNG 16-bit 디코드 (I/O·직렬, stbi) |
| NoiseFilter ×4 | 462 / 258 / 394 / 274 (합 1388) | OpenCV blur/gaussian/bilateral + NaN glue |
| ExposureMerge(split) | 103 | |
| **HeightMeasure** | **436** | ROI 12개 추출 + nth_element (연산 바운드) |
| Align/LineCenter/PlaneFit/CsvWriter | <30 각 | 무시 가능 |

## 조치
### HeightMeasure: 436 → 78 ms (5.6배), 결과 bit-identical ✅
measure ROI 12개는 서로 독립 → **ROI 단위 `cv::parallel_for_` 병렬화**. 결과는
인덱스로 기록해 직렬과 동일 순서/값. merged 100장으로 최적화 전(직렬) CSV와 대조 →
**완전 동일**(avg σ 0.0014713, worst range 0.00788455 일치, 전체 파일 diff 0).

### NoiseFilter: 개선 불가 → 원복 (측정 기반 결정)
직렬 glue 루프(normalize/base/restore/SOR, 각 34M)를 `cv::parallel_for_`로 병렬화
시도 → **오히려 느려짐**(462→666, 258→351 등 전부 악화). 이 루프들은 단순 divide/copy
로 **메모리 대역폭 바운드**라 스레드 분할 오버헤드만 추가됨. 실제 비용은 OpenCV 필터
호출 자체(이미 내부 최적화)와 메모리 대역폭 → 결과 보존 범위에서 추가 이득 없음. 원복.
(레시피에서 NoiseFilter를 전체 이미지 대신 필요 ROI로 한정하면 크게 줄지만, 이는 다운스트림이
읽는 영역이 바뀌어 결과 보존이 아니므로 코드 최적화가 아닌 레시피 설계 문제로 분리.)

### 나머지 노드
- ZMapLoader: PNG 디코드(직렬 stbi)·I/O 바운드 — 결과 보존 범위 내 이득 낮음.
- PlaneFit: RANSAC inlier 루프 이미 OpenMP 병렬.
- LineCenter/Align/PlaneFit/CsvWriter: <30ms, 최적화 불필요.
- GapFill/EdgeDetector/ZMapToCloud: 이 레시피 hot-path 아님. 연산 바운드 반복부는
  동일 방식(독립 반복 `cv::parallel_for_`)으로 개선 가능 — 별도 검증 레시피 필요, 후속.

## 교훈
"직렬 루프=병렬화 대상"이 아니다. **연산 바운드(HeightMeasure의 추출+정렬)는 큰 이득,
메모리 바운드 선형 패스(NoiseFilter glue)는 오히려 손해.** 반드시 실측으로 판단.
