# 상세 설계 — 파괴적 아키텍처 정합 (A1~A6)

> **구현 지시서.** Claude Code 작업 입력용.
> **이 문서는 하위 호환을 포기한다.** 기존 레시피 재사용은 목표가 아니고 아키텍처 정합이 우선이다 (사용자 결정, 2026-08-03).
> 선행 문서: [DESIGN_T0_FRAME_AND_COLLECTION.md](./DESIGN_T0_FRAME_AND_COLLECTION.md) → [DESIGN_T0_SURFACE_PRIMITIVES.md](./DESIGN_T0_SURFACE_PRIMITIVES.md)
> 상위 문서: [3D_BASELINE_BENCHMARK.md](./3D_BASELINE_BENCHMARK.md) · [ARCHITECTURE_DIRECTION.md](./ARCHITECTURE_DIRECTION.md)
> 작성: 2026-08-03. 대상 워크트리: `wt-refactor`.

---

## 0. 이 문서가 앞 문서들을 덮어쓰는 부분

### ⚠️ 폐기되는 원칙

앞선 세 문서의 **"절대 원칙 — 기존 동작 보존 / CSV bit-identical"**은 이 문서에서 **폐기된다.**

기존 레시피가 깨지는 것을 허용한다. 대신 §1의 **대체 검증 체계**를 게이트로 쓴다. 검증 없이 하는 게 아니라, **검증 기준을 값 일치에서 정확성·불변량으로 바꾸는 것**이다.

### 폐기·교체되는 계획

| 앞 문서의 계획 | 이 문서의 처분 |
|---|---|
| [DESIGN_T1_PROFILE_CALIPER.md](./DESIGN_T1_PROFILE_CALIPER.md) §2 `byPort` 추가 + 레거시 슬롯 유지(이원화) | **교체.** 호환 제약이 없으므로 §4에서 **포트 기반으로 직접 전환**한다. 이원화 과도기를 만들지 않는다 |
| `memory/project_deferred_tasks.md`의 `heightmaps` 벡터화 안 | **폐기.** §4가 대체 |
| CSV `diff baseline.csv after.csv` = 0 게이트 | **폐기.** §1의 4계층 검증으로 교체 |

### 이 문서 이후에 할 것

[DESIGN_T1_PROFILE_CALIPER.md](./DESIGN_T1_PROFILE_CALIPER.md)의 **Part B(Profile 타입 + 캘리퍼)는 유효하다.** 이 문서를 먼저 끝내고 그 위에 올린다 — 새 규약이 확정된 뒤 신규 타입을 만드는 게 두 번 안 만지는 길이다.

---

## 1. 대체 검증 체계 (**A6 — 가장 먼저 할 일**)

**검증 수단 없이 파괴적 변경을 시작하면 뭐가 깨졌는지 알 수 없다.** 다른 무엇보다 이것이 선행한다.

### 4계층 게이트

| # | 계층 | 무엇을 보장 | 도구 |
|---|---|---|---|
| **V1** | **합성 데이터 정답 대조** | 수치 정확성 | 단위 테스트. 코드로 만든 도형 + 해석적 정답 |
| **V2** | **불변량 테스트** | 규약 준수 | 단위 테스트. 왕복·보존 성질 |
| **V3** | **실데이터 반복성** | 실전 성능 비퇴행 | `--repeat-analyze`. **기준을 diff 0 → σ 비교로 변경** |
| **V4** | **골든 수치 대장** | 의도치 않은 변화 감지 | 대표 레시피 최종 측정값을 문서에 기록 |

### V1 — 합성 데이터 정답 대조 (주 게이트)

**값 일치 대신 정확성을 검증한다.** 실데이터는 정답을 모르지만 합성 데이터는 안다.

`Tests/Tools/`에 공용 픽스처를 만든다:

```cpp
// Tests/Tools/SyntheticFixtures.h (신규)
namespace vision::test {

// 기울어진 평면 — PlaneFit·Level의 정답이 해석적으로 알려짐
HeightMap makeTiltedPlane(int w, int h, double a, double b, double c,
                          float xRes = 0.05f, float yRes = 0.05f, float zRes = 0.001f);

// 평면 + 알려진 위치·높이의 계단 — 엣지 검출·높이 측정의 정답
HeightMap makeStep(int w, int h, int stepCol, double baseZmm, double stepZmm);

// 평면 + 알려진 반지름·중심의 원형 구멍 (NaN) — Region·면적의 정답
HeightMap makeHole(int w, int h, double cxMm, double cyMm, double rMm);

// 알려진 개수·크기의 돌기 배열 — ConnectedComponents·객체 필터의 정답
HeightMap makeBumpGrid(int w, int h, int nx, int ny, double bumpZmm, double bumpRmm);

// 지정 비율의 무효 픽셀을 결정론적으로 주입 (시드 고정)
void injectInvalid(HeightMap& m, double ratio, uint32_t seed);

// 지정 σ의 가우시안 노이즈를 결정론적으로 주입 (시드 고정)
void injectNoise(HeightMap& m, double sigmaMm, uint32_t seed);

} // namespace vision::test
```

**규칙**
- 난수는 **반드시 시드 고정**. 재현 불가능한 테스트는 게이트가 못 된다.
- 허용오차를 명시한다. 기하 계산은 `1e-9`, float 격자 왕복은 `zResMm/2`, 서브픽셀 검출은 `0.1 px` 등 **항목마다 근거를 주석에 남긴다.**
- **정답을 코드로 계산하지 말고 상수로 박거나 독립 경로로 계산한다.** 테스트가 구현과 같은 식을 쓰면 아무것도 검증하지 못한다.

### V2 — 불변량 테스트

값을 몰라도 반드시 성립해야 하는 성질이다. **아키텍처 규약을 못박는 수단이다.**

| 불변량 | 대상 |
|---|---|
| 크롭·리샘플 전후 **같은 물리점의 `xMm()`/`yMm()`이 동일** | `SurfaceCrop`, `SurfaceResample` |
| 프레임 변환 왕복 `F1→F2→F1` = 원본 | `FrameRegistry`, 평면 변환 |
| `Level(distance)` 출력을 ROI 평균한 값 == 직접 계산한 수직거리 평균 | `Level` ↔ 측정 경로 정합 |
| `ExtractProfile(axisX)` 결과가 원본 행과 **bit-identical** | `ExtractProfile` (보간 없는 경로) |
| NaN 입력 → NaN 출력 (전파 정책이 `propagate`일 때) | 모든 값-변경 노드 |
| 채널0만 바꾸는 노드가 다른 채널을 bit-identical 보존 | `Level`, `SurfaceSubtract` |
| 컬렉션 출력 순서가 실행마다 동일 | 배열 생산 노드 전부 |
| 같은 입력·같은 파라미터 → 같은 출력 (**멱등성**) | 전 노드. 병렬 실행기 검증 겸 |

### V3 — 실데이터 반복성 (기준 변경)

`--repeat-analyze`는 계속 쓴다. **다만 판정 기준을 바꾼다.**

```
[폐기] diff baseline.csv after.csv  →  출력 없음 (bit-identical)
[신규] after.csv의 avg σ ≤ baseline.csv의 avg σ × 1.10
       그리고 worst range ≤ baseline × 1.10
```

즉 **값이 달라지는 것은 허용하고, 반복성이 나빠지는 것은 불허**한다. 측정값 자체는 규약이 바뀌면 달라질 수 있지만(예: `Level`의 Z 인코딩, ROI를 Region으로 받는 방식), 같은 대상을 100번 재서 흔들리는 정도가 나빠지면 그건 진짜 퇴행이다.

기준 수치 ([repeatability_findings.md](../repeatability_findings.md)):

| 경로 | avg σ | worst range |
|---|---|---|
| raw (저노출 분리) | 0.00149 mm | 0.00798 mm |
| merged strict | 0.00147 mm | 0.00788 mm |

**10% 여유의 근거**: 부동소수 인코딩 변경·집계 경로 변경으로 σ가 미세하게 움직일 수 있다. 10%를 넘으면 알고리즘이 실제로 나빠진 것이다. **이 임계를 넘으면 반드시 원인을 규명하고 보고한다** — "허용 범위 안"으로 넘기지 말 것.

### V4 — 골든 수치 대장

대표 레시피의 최종 측정값을 **문서에 표로 기록**한다. `docs/GOLDEN_NUMBERS.md`(신규).

```
레시피 · 데이터셋 · 노드별 주요 출력값 · 측정일 · 커밋 해시
```

**용도**: V3이 σ만 보므로 절댓값이 조용히 이동하는 것을 못 잡는다. 골든 수치가 그걸 잡는다.

**규칙**: 값이 바뀌면 **그 변경이 의도적인지 판단하고, 의도적이면 표를 갱신하며 사유를 적는다.** 무단 갱신 금지. 이게 이 문서에서 bit-identical을 대체하는 실질적 안전장치다.

### 레시피 스키마 버전 (필수 부수 작업)

현재 레시피 JSON에 **버전 필드가 없다** (`main.cpp`·`App.tsx`에 `version`/`schemaVersion` 없음). 파괴적 변경을 하려면 이게 있어야 한다:

```json
{ "schemaVersion": 2, "nodes": [...], "edges": [...] }
```

- 버전 없음(레거시) → 로드 시 **명확한 에러**: "이 레시피는 구 스키마입니다. 마이그레이션이 필요합니다."
- **조용히 로드해서 잘못 동작하는 것이 최악이다.** 거부가 낫다.
- 마이그레이션 스크립트는 best-effort로 제공한다 (재사용은 "되면 좋은" 수준이므로 완전성을 요구하지 않는다).

---

## 2. A1 — 결과 반환 경로 통일 (**가장 근본적인 위반**)

### 문제

`main.cpp`가 **툴의 구체 타입을 알고 `dynamic_cast`로 결과를 꺼낸다.**

```cpp
// main.cpp L334, 361, 380, 396, 407, 415 — 6곳
auto* m = dynamic_cast<PlaneFitTool*>(tool.get());
if (m && m->lastResult().valid) { const auto& r = m->lastResult(); jr["planeA"] = r.a; ... }
```

`lastResult()`를 가진 툴 6개: `AlignTool` `CsvWriterTool` `HeightFromPlaneTool` `LineCenterTool` `PlaneFitTool` `RegionMeasureTool`.
`ns.type == "..."` 분기 9곳: `Align` `CsvWriter` `ExposureMergeCloud` `HeightMapLoader` `HeightMapToCloud` `HeightMeasure` `LineCenter` `PlaneFit` `RegionMeasure`.

**왜 이게 최우선 문제인가**

1. **narrow-waist 위반.** `VisionEngine`이 `VisionTools`의 구체 클래스를 안다. 계층 규칙의 정면 위반이다.
2. **노드 추가 비용의 주범.** 새 측정 노드를 만들면 `main.cpp`에 분기를 또 추가해야 한다. 문서에 "등록 3곳"이라고 써왔지만 실제로는 4곳이고, 그 4번째가 god 파일을 키운다.
3. **툴이 stateful.** 지금은 노드당 툴을 새로 만들어 병렬에서 안전하지만, 구조적으로 위험을 안고 있다.
4. **결과가 두 경로로 흐른다.** `VisionData`(포트)와 `lastResult()`(사이드채널). 사이드채널 결과는 하류 노드가 못 받는다 — 즉 **UI에만 보이고 조합할 수 없는 결과**가 존재한다.

4번이 범용성 관점에서 결정적이다. `PlaneFit`의 RMSE·tilt·inlierCount는 지금 조합 불가능하다. "RMSE가 임계 초과면 불합격" 같은 레시피를 만들 수 없다.

### 설계

**모든 결과를 `VisionData`에 이름 있는 형태로 싣는다. `lastResult()`와 `dynamic_cast`를 전부 제거한다.**

```cpp
// Core/include/Measurement.h (신규)
namespace vision {

// 이름 붙은 스칼라 측정값. control 데이터의 기본 단위.
struct Measurement {
    std::string name;    // "rmse", "tiltDeg", "areaMm2", "height", ...
    double      value = 0;
    std::string unit;    // "mm", "mm2", "deg", "px", "" (무차원)
    bool        valid = true;
};

// 합/불 판정 + 사유
struct Decision {
    std::string name;
    bool        pass = true;
    std::string reason;      // 실패 사유 또는 판정 근거
    double      measured = 0, nominal = 0, tolerance = 0;
};

} // namespace vision
```

`VisionData`에 추가하고 **`heights`를 폐기한다**:

```cpp
struct VisionData {
    // ... iconic 슬롯 ...
    std::vector<Measurement> measurements;   // 신규 — 이름 있는 측정값
    std::vector<Decision>    decisions;      // 신규 — 판정
    // std::shared_ptr<std::vector<double>> heights;  ← 삭제
};
```

**엔진의 직렬화가 타입 무관해진다**:

```cpp
// main.cpp — ns.type 분기 9곳 전부 삭제, 이것으로 대체
for (const auto& m : out.measurements)
    jr["measurements"].push_back({{"name",m.name},{"value",m.value},{"unit",m.unit},{"valid",m.valid}});
for (const auto& d : out.decisions)
    jr["decisions"].push_back({{"name",d.name},{"pass",d.pass},{"reason",d.reason}});
```

**미리보기·오버레이는 별도 처리.** `PlaneFit`의 `cloudPoints`(3D 뷰용)나 `LineCenter`의 라인 시각화는 측정값이 아니라 **표시용 부산물**이다. `stages`처럼 전용 슬롯을 쓰거나 `VisionData::overlays`를 신설한다. **측정값과 섞지 말 것** — 섞으면 다시 타입별 분기가 생긴다.

### 이관 대상 (전수)

| 툴 | 현재 `lastResult()` 내용 | 이관 후 |
|---|---|---|
| `PlaneFit` | a, b, c, rmse, tiltDeg, refPointCount, inlierCount, cloudPoints | `planes[0]` + `measurements`(rmse·tiltDeg·refPointCount·inlierCount) + `overlays`(cloudPoints) |
| `HeightFromPlane` | 측정값 배열 + 합불 | `measurements`(ROI별 이름) + `decisions` |
| `RegionMeasure` | areaPx, areaMm2, cxMm, cyMm, meanZmm | `measurements` 5개 (**이름 부여 — 순서 의존 제거**) |
| `LineCenter` | lines 배열 | `points` + `overlays`(라인) |
| `Align` | 적용된 원점 | `measurements`(originXmm·originYmm) — 또는 A5의 Fixture 전환으로 소멸 |
| `CsvWriter` | 기록된 행 정보 | `measurements`(rowCount) 또는 로그만 |

**`heights` 폐기의 파급**: `CsvWriterTool`이 `heights`를 순서로 읽는다. `measurements`의 **`name`을 CSV 헤더로 쓰게** 바꾼다. 이게 A2의 실질적 이득이다 — CSV 열이 자기 이름을 갖는다.

---

## 3. A2 — Control 데이터 타입화 · 판정 분리

A1과 **같은 작업의 다른 면**이다. 함께 한다.

### 문제

`RegionMeasureTool.cpp` L40-48:
```cpp
auto heights = std::make_shared<std::vector<double>>();
heights->push_back(m_result.areaPx);
if (m_result.hasHeight) heights->push_back(m_result.areaMm2);   // ← 조건부 push!
heights->push_back(m_result.cxMm);
heights->push_back(m_result.cyMm);
if (nz) heights->push_back(m_result.meanZmm);                    // ← 조건부 push!
```

**조건부 push 때문에 배열 길이가 입력에 따라 달라진다.** `CsvWriter`가 순서로 해석하므로 어떤 이미지에서는 3번째가 `cxMm`이고 어떤 이미지에서는 `areaMm2`다. **이미 조용히 깨져 있을 가능성이 높다.**

### 신규 노드 2개

**`Compare` — 측정값 → 판정** ([벤치 §2-G1](./3D_BASELINE_BENCHMARK.md), Gocator Decisions)

```
입력  : Measurements
출력  : Measurements, Decisions
카테고리: 판정
```

| 파라미터 | 의미 |
|---|---|
| `target` | 대상 측정값 이름. 빈 문자열이면 전체 |
| `mode` | `tolerance`(공칭±공차) \| `range`(min~max) \| `max` \| `min` |
| `nominal`·`tolerance`·`min`·`max` | 기준값 |

**`CombineDecision` — 판정 결합**

```
입력  : Decisions
출력  : Decisions
파라미터: mode = all(AND) | any(OR) | count(N개 이상 통과)
```

**효과**: `HeightMeasure`·`ThicknessMeasure`에 박혀 있던 tolerance가 노드로 분리된다. 같은 측정값에 여러 기준을 걸 수 있고, 판정 기준만 바꿔 재평가할 수 있다.

### `PortType` 개편

```ts
// 폐기: 'Heights'
// 신규: 'Measurements' | 'Decisions'
```

`Heights` 포트를 쓰는 노드(`RegionMeasure`, `HeightMeasure`, `CsvWriter`)를 전부 새 타입으로 옮긴다.

---

## 4. A3 — `VisionData` 포트 기반 전환

### 설계 — 레거시 슬롯 제거, 직접 전환

[DESIGN_T1_PROFILE_CALIPER.md](./DESIGN_T1_PROFILE_CALIPER.md)의 `byPort` **이원화 안을 폐기한다.** 호환 제약이 없으므로 한 번에 간다.

```cpp
// Core/include/VisionData.h
struct VisionData {
    // 포트별 입력 — 인덱스 = 이 노드의 입력 포트 번호. 미연결은 nullptr.
    std::vector<VisionDataPtr> inputs;

    // 이 노드의 출력 (자기 데이터)
    std::vector<HeightMapPtr>   heightmaps;   // 다중 — 단일 슬롯 폐기
    std::vector<CloudPtr>       clouds;       // 다중
    std::vector<RegionPtr>      regions;
    std::vector<PlanePtr>       planes;
    std::vector<ProfilePtr>     profiles;     // T1에서 추가
    std::vector<RefPoint>       points;
    std::vector<Measurement>    measurements;
    std::vector<Decision>       decisions;

    // 메타
    std::shared_ptr<FrameRegistry> frames;
    std::vector<Frame>             definedFrames;
    std::vector<Overlay>           overlays;   // 표시용 부산물
    std::string                    sourceId;
    int64_t                        timestampUs = 0;

    // 포트 접근 — 범위 밖/미연결은 nullptr
    VisionDataPtr in(std::size_t port) const {
        return port < inputs.size() ? inputs[port] : nullptr;
    }
    HeightMapPtr inHeightMap(std::size_t port, std::size_t idx = 0) const {
        auto p = in(port);
        return (p && idx < p->heightmaps.size()) ? p->heightmaps[idx] : nullptr;
    }
    // Region/Plane/Profile/Cloud 동형 헬퍼
};
```

**규약**
- 툴은 **`in(port)`로만 입력을 읽는다.** 슬롯 직접 접근 금지. 이 규칙이 앞으로 같은 문제가 재발하지 않게 하는 핵심이다.
- `inputs[port]`는 **상류 출력 원본 포인터**다. 읽기 전용. 병렬 실행에서 공유되므로 **절대 수정 금지.**
- 출력 벡터는 **단일 출력도 size 1**로 넣는다. 예외를 만들지 않는다.
- `main.cpp` 병합 루프는 **병합을 하지 않는다.** `inputs[dstPort] = 상류출력`만 채운다. 기존의 슬롯 병합·first-wins·concat 로직 전부 삭제.

**`heights`/`points` concat 동작이 사라지는 것에 주의.** 여러 측정 노드의 결과를 한 CSV 행으로 모으던 동작이다. **`Collect` 노드로 대체**한다 — 암묵적 concat보다 명시적 노드가 맞다 ([DESIGN_T0_FRAME_AND_COLLECTION.md](./DESIGN_T0_FRAME_AND_COLLECTION.md)의 `PLANNED_REDUCTION_NODES`에 계약이 이미 정의돼 있다).

### 얻는 것

- HeightMap ×2 입력 노드가 가능해진다 → `SurfaceSubtract`, 골든 비교, 멀티센서 병합
- Region ×2 → Boolean 연산
- Plane ×2 → 각도·교선
- 배열 출력이 자연스러워진다 → T2-1 ConnectedComponents, T0-2 브로드캐스트 배선의 전제

---

## 5. A4 — ROI를 Region 포트로 전면 이관

### 문제 — 노드 그래프 원리 위배

`rois`를 파라미터로 갖는 툴: `CreateRoiTool` `LineCenterTool` `NoiseFilter` + tools.ts상 `PlaneFit` `HeightMeasure` `RowStretch`.

**파라미터는 사람이 손으로 넣는 값이고, 포트는 상류가 계산해 주는 값이다.** ROI가 파라미터면 **상류 결과로 ROI를 만들 수 없다.**

이것이 [벤치 §2-E1](./3D_BASELINE_BENCHMARK.md)(개수 가변 대상 검사)이 막힌 실제 원인이다. `Threshold`로 만든 Region, `ConnectedComponents`로 쪼갠 Region을 측정에 쓸 수 없다.

### 설계

**측정·필터 툴은 `Region` 포트로 대상 영역을 받는다.** `rois` 파라미터를 제거한다.

| 툴 | 현재 | 변경 후 |
|---|---|---|
| `PlaneFit` | `rois`(ref 영역) 파라미터 | `HeightMap` + **`Region` 포트**. Region 내부 픽셀으로 피팅 |
| `NoiseFilter` | `rois` 파라미터 | `HeightMap` + **`Region` 포트**(선택). 없으면 전체 |
| `RowStretch` (전용) | `rois` 파라미터 | `HeightMap` + **`Region` 포트** |
| `LineCenter` (전용) | `rois`(검색 영역) | `HeightMap` + **`Region` 포트**(검색 영역) |
| `HeightMeasure` | `rois`(측정 영역) | **A6에서 분해** — `RegionMeasure`가 대체 |
| `CreateROI` | `rois` 파라미터 | **유지.** 이 툴의 존재 이유가 "사람이 찍은 ROI를 Region으로 만드는 것"이다 |

**`PlaneFit`이 가장 중요하다.** ref ROI를 Region으로 받으면 `Threshold` → `PlaneFit`으로 **자동 평면 피팅**이 된다. 지금은 사람이 19개 glass ROI를 손으로 찍는다.

**여러 영역을 쓰던 툴은 어떻게 되나**: `Region` 하나가 여러 연결 성분을 담을 수 있으므로(마스크이므로) `PlaneFit`은 그냥 마스크 전체를 쓴다. **영역별로 따로 처리해야 하는 경우는 `Region[]` + 브로드캐스트**로 표현한다 — 그게 올바른 분해다.

**전용 노드도 예외 없이 적용한다** (사용자 지시). `RowStretch`·`LineCenter`가 Region 포트를 받게 되면, 전용 노드도 범용 조합에 참여할 수 있다.

---

## 6. A5 — 전용 노드 아키텍처 정합

**유지 대상 5개**: `Exposure Split`(ExposureMerge) · `Exposure Merge`(ExposureMerge2) · `Exposure Merge (3)`(ExposureMerge3) · `Row Stretch` · `Line Finder`(LineCenter).

기능은 유지하되 **규약 위반을 고친다.**

| # | 위반 | 대상 | 조치 |
|---|---|---|---|
| A5-1 | **`halfRes`가 분해능을 바꾸는데 새 프레임을 안 만든다** | `ExposureMerge2/3` | T0-1 규약 적용. `yResMm*=2`·`height/2`면 `SurfaceResample`과 **동일하게 새 프레임 정의** + `definedFrames` 기록 |
| A5-2 | **`chunkMode`/`chunkRows`/`overlapRows`가 레시피에 노출** | `ExposureMerge2/3` | 메모리 관리는 **엔진 관심사**다. 레시피에서 제거하고 엔진이 이미지 크기 보고 자동 판단. 필요하면 엔진 설정으로 |
| A5-3 | **`OriginCoord` 전용 슬롯** | `LineCenter` → `Align` | `Point`(RefPoint)로 통일. 전용 슬롯 폐기. "어느 축을 쓸지"는 하류 노드의 파라미터 |
| A5-4 | `rois` 파라미터 | `RowStretch`, `LineCenter` | A4에 따라 `Region` 포트로 |
| A5-5 | `lastResult()` 사이드채널 | `LineCenter` | A1에 따라 `measurements` + `overlays`로 |
| A5-6 | `outputStage` 파라미터 (미리보기 단계 선택) | `ExposureMerge`, `GapFill` | `stages` 슬롯이 이미 있다. 출력을 파라미터로 고르는 대신 **`stages`에 전부 싣고 UI가 고르게** 한다 |

### 별도 카테고리로 격리

팔레트에서 전용 노드를 **`SDC 전용`** 카테고리로 묶는다. `tools.ts`의 `category` 변경만으로 되고, 범용 툴과 시각적으로 구분된다. [벤치 §2-H](./3D_BASELINE_BENCHMARK.md)의 팔레트 개편안과 정합한다.

### `ExposureMergeCloud` — 판단 필요

전용 목록에 없으나 파라미터(`matchTol`/`tolX`/`tolY`/`gapK`)가 `ExposureMerge2` 계열이다. 커밋 `190bc6d`가 "조직화 point cloud 이중노출 머지 (X 보존)"이라고 밝힌다.

**판단**: `ExposureMerge2 → HeightMapToCloud` 조합으로 X 보존이 안 되는 게 확실하면 **전용으로 분류해 유지**, 조합으로 대체 가능하면 **삭제**. 구현을 읽고 판단해 §10에 기록한다.

---

## 7. A6 — 측정 노드 재편 (`HeightMeasure` 분해)

A1~A5가 끝나면 `HeightMeasure`는 **조합 가능한 것을 하드코딩한 상태**가 된다.

```
현재: HeightMeasure(rois, aggregation, tolerance)
대체: Level(distance) → CreateROI/Threshold → RegionMeasure(aggregation) → Compare(tolerance)
```

### `RegionMeasure` 확장 (흡수처)

| 추가 | 내용 |
|---|---|
| 집계 파라미터 | `Mean`·`Median`·`Max`·`Min`·**`HighTail`**·`Percentile`·`StdDev` — **`VisionTools/include/Aggregate.h`를 호출만 한다. 직접 구현 금지** |
| 기하 특징 | BBox 길이·폭·종횡비·방향각·면적·체적·평탄도 ([벤치 §2-E2](./3D_BASELINE_BENCHMARK.md) eVision range 필터 10종이 표준 스펙) |
| 출력 | `measurements`에 **이름 있는 값**으로 (A2) |

**`HighTail` 이관이 가장 중요하다.** 상위 N% 최대높이 집계는 [repeatability_findings.md](../repeatability_findings.md)의 핵심 지표다. **같은 알고리즘을 그대로 옮겨야** V3(σ 비교)가 성립한다. 알고리즘을 바꾸면 σ 비교의 의미가 사라진다.

> **⚠️ 집계 구현 위치 — `Aggregate.h`를 재사용한다**
>
> [DESIGN_T1_PROFILE_CALIPER.md §3.4](./DESIGN_T1_PROFILE_CALIPER.md)가 **`VisionTools/include/Aggregate.h`를 먼저 만들고 `HighTail`을 원형 그대로 이관 + 원 구현 대조 테스트**를 수반한다. 그 문서는 이 문서 Phase 0~3 직후에 진행된다.
>
> 따라서 Phase 5 시점에는 `Aggregate.h`가 이미 존재한다. **`RegionMeasure`는 그것을 호출만 한다.** 여기서 집계를 다시 구현하면 두 구현이 미묘하게 갈리고 `HighTail`이 어긋나면 V3 기준 자체가 무너진다.
>
> 만약 순서가 바뀌어 이 Phase가 먼저 오게 되면, **`Aggregate.h`를 이 Phase에서 만들고** `DESIGN_T1_PROFILE_CALIPER.md` Phase 1을 건너뛴다. 어느 쪽이든 **단일 출처를 유지하는 것이 규칙이다.**

이관·검증 완료 후 `HeightMeasure`를 삭제한다.

### `ImageSaver` → `HeightMapSaver`

**문제**: `Any` 입력이 타입 시스템을 무력화한다. 더 중요한 건 **저장 시 분해능·원점·`frameId`·`channelRoles`가 소실**되어 다시 읽으면 mm가 복원되지 않는다는 것이다. narrow-waist 경계에서 정규화를 담당해야 할 노드가 정보를 버린다.

**조치**
- 입력을 `HeightMap`으로 좁힌다
- 메타를 **사이드카 JSON**으로 함께 쓴다: `xResMm`·`yResMm`·`zResMm`·`zZeroCount`·`originCol`·`originRow`·`frameId`·`channelRoles`·`width`·`height`·`channels`
- `HeightMapLoader`가 사이드카를 읽으면 **왕복이 성립**한다 → 이게 V2 불변량 테스트 항목이 된다

**이 항목은 시간이 지날수록 손해가 커진다.** 지금 저장되는 파일이 메타 없이 쌓이고 있어서 나중에 재사용이 안 된다.

---

## 8. Phase 순서

```
Phase 0  V1 픽스처 + V2 불변량 테스트 골격 + V4 골든 수치 대장 작성
         레시피 schemaVersion 도입 (구 스키마 명확히 거부)
         ← 검증 수단 없이 아래를 시작하지 말 것

Phase 1  A1+A2  결과 경로 통일 + Measurement/Decision 타입
         lastResult()·dynamic_cast 전면 제거, heights 폐기
         → main.cpp의 ns.type 분기 9곳 소멸

Phase 2  A3     VisionData 포트 기반 전환 (레거시 슬롯 제거)
         → SurfaceSubtract 즉시 구현 가능해짐 (A3의 실전 검증)

Phase 3  A4     ROI → Region 포트 (PlaneFit 우선)
         → Threshold → PlaneFit 자동 피팅 성립

─────────  여기서 DESIGN_T1_PROFILE_CALIPER.md 로 이동  ─────────
         Phase 3까지가 캘리퍼 작업의 선행 조건이다. Phase 4~6은 캘리퍼와 독립이므로
         Profile·캘리퍼를 먼저 세우고 돌아온다. (최대 공백 영역을 먼저 채운다)
         그 문서가 Aggregate.h 를 만들어 아래 Phase 5가 재사용한다.
────────────────────────────────────────────────────────────────

Phase 4  A2b    Compare / CombineDecision 노드 신설

Phase 5  A6     RegionMeasure 확장 (Aggregate.h 재사용) → HeightMeasure 삭제
         HeightMapSaver 메타 저장 + 왕복 테스트

Phase 6  A5     전용 노드 정합 (A5-1~A5-6) + SDC 전용 카테고리 격리
```

### 전체 작업 순서

```
[이 문서] Phase 0~3   검증체계 · 결과경로 · 포트기반 · ROI→Region
       ↓
[DESIGN_T1_PROFILE_CALIPER.md]   Aggregate.h · Profile 타입 · 캘리퍼
       ↓
[이 문서] Phase 4~6   Compare · RegionMeasure 확장 · 전용 노드 정합
       ↓
Geometry 1급화 (Line·Circle) → T1-3 피팅 → T1-5 기하 연산
```

**Phase 3에서 끊고 캘리퍼로 넘어가는 이유**: Phase 0~3이 규약을 확정하므로 신규 타입이 두 번 고쳐지지 않는다. 반면 Phase 4~6은 캘리퍼와 독립이고, 캘리퍼가 [벤치 §2-D](./3D_BASELINE_BENCHMARK.md)의 **최대 공백 영역**이므로 먼저 채우는 것이 기능 진척에 낫다.

### 왜 이 순서인가

- **Phase 0 절대 선행**: 파괴적 변경에서 검증 체계가 없으면 무엇이 깨졌는지 알 수 없다.
- **A1+A2가 먼저**: 출력 경로다. 이걸 먼저 정리하면 이후 모든 노드가 새 규약으로 태어난다. 나중에 하면 그사이 만든 노드를 다시 고친다.
- **A3이 A4보다 먼저**: Region을 포트로 받으려면 포트 기반 입력이 먼저 서야 한다.
- **A5가 마지막**: 위 규약이 전부 확정된 뒤 전용 노드를 맞춘다. 규약이 흔들리는 중에 맞추면 두 번 한다.
- **`SurfaceSubtract`는 Phase 2의 검증 수단**으로 구현한다. 단위 테스트로는 입력 라우팅을 검증할 수 없다.

---

## 9. 금지 사항

1. **Phase 0을 건너뛰지 말 것.** 검증 체계가 이 문서의 전제다.
2. **`inputs[port]`를 수정하지 말 것.** 상류 출력 원본이고 병렬 실행에서 공유된다.
3. **툴이 슬롯을 직접 읽지 말 것.** `in(port)` 계열 헬퍼만 쓴다 (§4 규약).
4. **`lastResult()` 방식을 새로 만들지 말 것.** 결과는 `VisionData`로만 흐른다.
5. **측정값과 표시용 부산물을 섞지 말 것.** `measurements` vs `overlays`. 섞으면 타입별 분기가 부활한다.
6. **`HighTail` 알고리즘을 바꾸지 말 것** (§7). 바꾸면 V3 σ 비교의 기준이 무너진다.
7. **구 스키마 레시피를 조용히 로드하지 말 것.** 명확히 거부한다 (§1).
8. **V3 임계(σ ×1.10)를 넘겼을 때 "허용 범위"로 넘기지 말 것.** 원인 규명 후 보고.
9. **테스트가 구현과 같은 식으로 정답을 계산하지 말 것** (§1 V1). 상수 또는 독립 경로.
10. **난수 시드를 고정하지 않은 테스트 금지.**
11. **병렬화를 선제적으로 넣지 말 것.** 직렬 구현 후 실측.
12. **자동 커밋 금지.** Phase 완료 후 diff 보고 + 승인.

---

## 9.5 완료 보고 규칙

[DESIGN_T0_SURFACE_PRIMITIVES.md §7.5](./DESIGN_T0_SURFACE_PRIMITIVES.md)와 동일. 요약:

**"확인했습니다"류 요약 금지. 명령과 출력 원문만.**

보고 필수: ① 실행 명령 원문 ② 출력 원문 ③ `ctest` 실제 요약 줄 ④ 변경 파일 + diff(문서에 없는 파일은 이유) ⑤ 문서에 없는 결정 목록 ⑥ 체크리스트 항목별 상태(부분 완료를 완료로 보고 금지).

**이 문서에서 추가되는 보고 항목**: **V3 σ 수치를 before/after로 함께 제시**한다. "σ 악화 없음"이라고 쓰지 말고 실제 숫자를 적는다.

**검증 담당**: 빌드·테스트·V3·성능은 Claude Code. 코드 리뷰는 **subagent**(구현 컨텍스트 미노출). 체크리스트 실재 확인은 별도 감사 세션.

---

## 10. 작업 체크리스트

### Phase 0 — 검증 체계 (**선행 필수**)
- [ ] **0-1** `Tests/Tools/SyntheticFixtures.h` — 5개 생성기 + 시드 고정 노이즈·무효 주입
- [ ] **0-2** V2 불변량 테스트 골격 — §1 표의 8개 항목
- [ ] **0-3** `docs/GOLDEN_NUMBERS.md` 신규 — 대표 레시피 현재 측정값 기록 (커밋 해시 포함)
- [ ] **0-4** V3 기준선 — 현재 avg σ / worst range 측정·기록
- [ ] **0-5** 레시피 `schemaVersion` 도입. 버전 없으면 **명확한 에러로 거부**
- [ ] **0-6** `Tests/Tools/ThicknessMeasureTest.cpp` 고아 파일 삭제
- [ ] **[검증]** 신규 테스트 전부 통과 + 골든 수치 기록 완료

### Phase 1 — A1+A2 결과 경로 통일
- [ ] **1-1** `Core/include/Measurement.h` — `Measurement`, `Decision`
- [ ] **1-2** `VisionData`에 `measurements`·`decisions`·`overlays` 추가, **`heights` 삭제**
- [ ] **1-3** 툴 6개에서 `lastResult()` 제거 → `VisionData`로 이관 (§2 표 전수)
- [ ] **1-4** `main.cpp`의 `dynamic_cast` 6곳 + `ns.type` 분기 9곳 **전부 삭제**, 타입 무관 직렬화로 교체
- [ ] **1-5** `RegionMeasure`의 조건부 push 제거 → 이름 있는 `measurements`
- [ ] **1-6** `CsvWriter`가 `measurements`의 `name`을 헤더로 쓰게
- [ ] **1-7** `PortType`: `Heights` 폐기 → `Measurements`·`Decisions`
- [ ] **1-8** UI: 결과 패널이 이름 있는 측정값을 표시
- [ ] **[검증]** V1·V2 통과 + V3 σ before/after 제시 + 골든 수치 변화 사유 기록

### Phase 2 — A3 포트 기반 전환
- [ ] **2-1** `VisionData`를 §4 구조로 전환 — `inputs` + 출력 벡터화, 단일 슬롯 전부 제거
- [ ] **2-2** `in()`/`inHeightMap()`/`inRegion()`/`inPlane()` 헬퍼
- [ ] **2-3** `main.cpp` 병합 루프를 **`inputs[dstPort]` 대입만** 하도록. 병합·first-wins·concat 전부 삭제
- [ ] **2-4** 전 툴을 `in(port)` 방식으로 전환 (컴파일 에러로 전수 발견)
- [ ] **2-5** 사라진 concat 동작을 `Collect` 노드로 대체 (엔진 구현)
- [ ] **2-6** **`SurfaceSubtract` 구현** — A3의 실전 검증. `inHeightMap(0)`/`inHeightMap(1)`
- [ ] **2-7** 멱등성 테스트 — 같은 입력 반복 실행 시 동일 출력 (병렬 안전성 겸)
- [ ] **[검증]** V1·V2·V3 + `memory/project_deferred_tasks.md`의 SurfaceSubtract 항목 해소

### Phase 3 — A4 ROI → Region 포트
- [ ] **3-1** `PlaneFit` — `rois` 제거, `Region` 포트 추가 (**우선**)
- [ ] **3-2** `NoiseFilter` — `rois` 제거, `Region` 포트(선택)
- [ ] **3-3** `Threshold → PlaneFit` 자동 피팅이 동작하는지 확인 (이 변경의 목적)
- [ ] **3-4** `CreateROI`는 `rois` 유지 (존재 이유)
- [ ] **[검증]** V1·V2·V3 + 자동 피팅 σ를 손찍기 ROI σ와 대조 기록

### Phase 4 — Compare / CombineDecision
- [ ] **4-1** `CompareTool` — `mode`(tolerance/range/max/min), `target`
- [ ] **4-2** `CombineDecisionTool` — `mode`(all/any/count)
- [ ] **4-3** 등록 + UI 판정 표시 (pass/fail + 사유)
- [ ] **[검증]** V1 (합성 측정값으로 경계 조건 전수)

### Phase 5 — A6 측정 노드 재편
- [ ] **5-1** `RegionMeasure` 집계 확장 — **`HighTail` 알고리즘 원형 그대로 이관**
- [ ] **5-2** `RegionMeasure` 기하 특징 추가 (BBox·종횡비·방향·면적·체적·평탄도)
- [ ] **5-3** `Level → CreateROI → RegionMeasure → Compare`가 구 `HeightMeasure`와 **같은 값을 내는지** 대조
- [ ] **5-4** 확인 후 `HeightMeasure` 삭제
- [ ] **5-5** `ImageSaver` → `HeightMapSaver`. 입력 `HeightMap`으로 좁히고 사이드카 JSON 메타
- [ ] **5-6** `HeightMapLoader`가 사이드카를 읽어 **왕복 불변량 테스트** 통과
- [ ] **[검증]** V1·V2·V3. 5-3 대조 결과를 수치로 기록

### Phase 6 — A5 전용 노드 정합
- [ ] **6-1** `ExposureMerge2/3` `halfRes` → 새 프레임 정의 (A5-1)
- [ ] **6-2** `chunkMode`/`chunkRows`/`overlapRows` 레시피에서 제거, 엔진 자동 판단 (A5-2)
- [ ] **6-3** `OriginCoord` 폐기 → `Point`로 통일 (A5-3)
- [ ] **6-4** `RowStretch`·`LineCenter` `rois` → `Region` 포트 (A5-4)
- [ ] **6-5** `LineCenter` `lastResult()` 제거 (A5-5)
- [ ] **6-6** `outputStage` 제거 → `stages`에 전부 싣고 UI 선택 (A5-6)
- [ ] **6-7** `tools.ts` `category`를 `SDC 전용`으로 (5개)
- [ ] **6-8** `ExposureMergeCloud` 판단 — 조합 대체 가능 여부 확인 후 유지/삭제 결정을 §11에 기록
- [ ] **[검증]** V1·V2·V3

---

## 11. 미결정 사항

| # | 항목 | 상태 |
|---|---|---|
| **D-1** | `overlays` 타입 설계 — `PlaneFit` cloudPoints, `LineCenter` 라인, ROI 시각화를 담을 공통 형태 | Phase 1에서 결정 |
| **D-2** | `Collect`가 `measurements` 이름 충돌을 어떻게 처리할지 (같은 이름 두 개) | Phase 2. 노드 id를 접두어로 붙이는 안 |
| **D-3** | `PlaneFit`이 Region을 받을 때 **가중치**가 필요한지 (기존 ROI별 가중이 있었는지 확인) | Phase 3 |
| **D-4** | `ExposureMergeCloud` 유지 vs 삭제 | Phase 6 |
| **D-5** | `chunkMode` 자동 판단 기준 (이미지 크기? 가용 메모리?) | Phase 6 |
| **D-6** | 레시피 마이그레이션 스크립트를 만들지 여부 (best-effort) | 아무 때나 |
| **D-7** | V3 임계 ×1.10이 적절한지 — Phase 1 결과를 보고 조정 | Phase 1 이후 |

---

## 12. 이 문서 이후

이 문서가 끝나면 **아키텍처 규약이 확정된다.** 그 위에 기능을 올린다.

```
1  DESIGN_T1_PROFILE_CALIPER.md Part B  — Profile 타입 + 캘리퍼
   (Part A byPort는 이 문서 Phase 2로 대체됨)
2  Geometry 1급화 (Line·Circle) + T1-3 피팅 + T1-5 기하 연산
3  T0-2 브로드캐스트 실행 배선 + T2-1 ConnectedComponents + T2-2 메트릭 필터
4  T3-1 Fixture (Align 대체) + T3-2 정렬 + T3-3 골든 비교
5  T0-6 중첩 ToolBlock
```

**Phase 2(포트 기반)와 Phase 1(결과 경로)이 끝나면 노드 추가 비용이 크게 떨어진다.** `main.cpp`에 분기를 추가할 필요가 없어지고, 등록 지점이 진짜로 3곳(툴 파일 / CMakeLists+ToolFactory / tools.ts)이 된다. 이후 기능 개발 속도가 이 문서의 실질적 성과다.

### 병행 정리 항목

| 항목 | 내용 |
|---|---|
| 병렬 실행기 문서화 | 설계 문서에 없는 변경이다. ONBOARDING에 실행 모델 절 추가 |
| `perf_findings.md` 재측정 | 직렬 기준 수치가 무효화됨 |
| `ARCHITECTURE_DIRECTION.md` 갱신 | B3(iconic/control 분리)이 이 문서 A1+A2로 완결됨. 상태 표기 갱신 |
| A1~A3 리팩토링 (원 문서 기준) | 로더 narrow-waist, `ToolFactory` god 파일 분해. Phase 1이 `main.cpp`를 크게 줄이므로 그 뒤가 적기 |
