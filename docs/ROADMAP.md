# VisionSW — 로드맵

완료된 Phase 0~3 이후 남은 작업 목록.  
우선순위: ARCH 잔여 → T1 Profile Caliper → 장기 백로그.

---

## ARCH Phase 4 — Compare / CombineDecision 정합

Phase 3까지 측정값·판정 타입이 확정됐으니 판정 노드를 규약에 맞춘다.

- [x] 4-1 `Compare` 툴 — `Measurement` 이름으로 입력 매칭, `Decision` 출력
- [x] 4-2 `CombineDecision` — `Decision[]` 포트 입력, 집계 모드(all/any/count)
- [x] 4-3 `Collect` 노드 — 복수 포트 measurements+decisions 병합 (이름 충돌 시 포트 인덱스 prefix 자동 / `prefix` 파라미터 명시 가능). Region도 수집.
- [ ] 4-4 UI — Decision 결과를 Pass/Fail 색상으로 표시
- [x] 4-5 CsvWriter — Decision 컬럼 포함 출력 (`<name>_pass`, `<name>_value`)

---

## ARCH Phase 5 — 나머지 툴 포트 기반 전환

기존 `rois` params 방식을 쓰는 툴들을 Region 포트 입력으로 전환.

- [x] 5-1 `NoiseFilter` — 포트 1 = Region 마스크 (선택 연결), rois 파라미터 없음
- [x] 5-2 `GapFill` — 포트 1 = Region 마스크 (선택 연결, Region 안 NaN만 채움)
- [x] 5-3 `Level` — 포트 2 = Region 출력 마스크 (선택 연결, Region 밖 → NaN)
- [x] 5-4 `SurfaceCrop` — 포트 1 = Region 입력 (mode="region"), rect/region 선택
- [ ] 5-5 전환 완료 후 V2 불변량 테스트 통과 확인

---

## ARCH Phase 6 — 잔여 항목 (일부 완료)

- [x] 6-4 `RowStretch`, `LineCenter` rois → Region 포트 전환 (완료)
- [x] 6-5 `LineCenter` lastResult() 제거 (완료)
- [ ] 6-1 `ExtractProfile` — Profile 포트 출력, axisX/axisY/line 모드
- [ ] 6-2 `ProfileFeature` — Profile 포트 입력, Measurement[] 출력
- [ ] 6-3 `CsvWriter` — Profile[] 컬럼 방식 CSV 출력
- [ ] 6-6 schemaVersion 1 레거시 레시피 변환 도구 (선택 — 필요 시)

---

## T1 — Profile Caliper (설계: `docs/specs/DESIGN_T1_PROFILE_CALIPER.md`)

Phase 0 선행조건 완료 후 순차 진행.

- [ ] T1-1 `ExtractProfile` 툴 완성 — HeightMap + 선분 정의 → Profile
- [ ] T1-2 `ProfileFeature` 툴 — 엣지/능선/골/코너 검출 + 집계
- [ ] T1-3 합성 픽스처 `makeStep` 기반 단위 테스트
- [ ] T1-4 캘리퍼 쌍 거리 측정 (두 엣지 위치 차)
- [ ] T1-5 `CsvWriter` Profile 컬럼 출력
- [ ] T1-6 UI — Profile 뷰어 (2D 선 차트)

---

## 신규 툴 (2026-09-07)

| 툴 | 상태 |
|----|------|
| ConnectedComponents | ✅ 구현 완료 |
| RegionFilter | ✅ 구현 완료 |
| RegionSelect | ✅ 구현 완료 |
| RegionBoolean | ✅ 구현 완료 |
| RegionMorphology | ✅ 구현 완료 |
| ScalarMath | ✅ 구현 완료 |
| GeometryMeasure | ✅ 구현 완료 (Line + Plane 지원) |

## 신규 툴 (2026-09-08)

| 툴 | 상태 |
|----|------|
| CountRegions | ✅ 구현 완료 |
| CircleFit | ✅ 구현 완료 |
| HeightMapMath | ✅ 구현 완료 (abs/add/subtract/multiply) |
| GradientMap | ✅ 구현 완료 (Sobel magnitude/gx/gy) |
| ProfileSmooth | ✅ 구현 완료 (gaussian/mean) |
| RegionMeasure | ✅ perimMm + circularity 추가 |
| GeometryMeasure | ✅ planeAngle + planeDistance 추가 |

---

## 장기 백로그

- **Geometry 타입 완전 흡수** — 기존 `PlaneModel`/`LineModel`/`RefPoint`를 `Geometry`로 교체. 호환 접근자 제거.
- **ForEach 노드** — Region[] 또는 Profile[] 항목마다 하위 파이프라인 실행
- **UI 다중 뷰** — 노드별 결과를 탭/분할 화면으로 동시 표시
- **V3 반복성 기준선 측정** — 실데이터 N≥10 취득 후 `GOLDEN_NUMBERS.md` 채우기
- **V4 골든 수치 대장** — 대표 레시피 3개 고정값 기록
- **--repeat-analyze CLI 구현** — `VisionEngine.exe --repeat-analyze <recipe> <folder> <out.csv>`
- **3.5D 브랜치 통합** — wt-3.5D 작업물을 arch_develop에 머지
