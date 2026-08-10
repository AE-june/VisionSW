# 상세 설계 — T1-1 `Profile` 타입 · T1-2 특징점 검출 (캘리퍼)

> **구현 지시서.** Claude Code 작업 입력용.
> **선행 필수**: [DESIGN_ARCH_BREAKING_CHANGES.md](./DESIGN_ARCH_BREAKING_CHANGES.md) **Phase 0~3 완료.** §0.2 참조.
> 상위 문서: [3D_BASELINE_BENCHMARK.md](./3D_BASELINE_BENCHMARK.md) §2-D(최대 공백 영역) · §4 Tier 1 · §5 권장순서 3단계
> 작성: 2026-08-03 (2026-08-03 개정 — 아키텍처 우선 결정 반영). 대상 워크트리: `wt-refactor`.
> 다음 문서 예정: Geometry 1급화 (Line·Circle) + T1-3 피팅 + T1-5 기하 연산.

---

## 0. 작업 시작 전 필독

### 0.1 원칙 — 아키텍처가 호환보다 우선

**기존 코드·레시피와의 호환은 목표가 아니다** (사용자 결정, 2026-08-03).

- 기존 레시피가 깨지는 것을 허용한다.
- 기존 노드의 인터페이스를 바꿔야 한다면 바꾼다.
- 판단 기준은 **"목표하는 체계적 구조에 맞는가"**다. "지금 동작을 보존하는가"가 아니다.

따라서 이 문서의 신규 타입·노드는 **처음부터 새 규약으로 태어난다.** 과도기 호환 코드를 만들지 않는다.

검증은 [DESIGN_ARCH_BREAKING_CHANGES.md §1](./DESIGN_ARCH_BREAKING_CHANGES.md)의 **V1~V4 4계층**을 따른다. bit-identical CSV diff는 게이트가 아니다.

### 0.2 선행 조건 — ARCH Phase 0~3

이 문서를 시작하기 전에 [DESIGN_ARCH_BREAKING_CHANGES.md](./DESIGN_ARCH_BREAKING_CHANGES.md)의 다음이 완료돼야 한다.

| ARCH Phase | 왜 차단하는가 |
|---|---|
| **Phase 0** 검증체계 | 합성 픽스처(`makeStep`·`makeTiltedPlane`)가 없으면 캘리퍼를 검증할 수단이 없다. **이 문서의 주 검증 도구가 그 픽스처다** |
| **Phase 1** `Measurement`/`Decision` 타입 | `ProfileFeature`가 `Heights`를 출력하게 만들면 Phase 1에서 전부 다시 고친다 |
| **Phase 2** 포트 기반 입력 (`in(port)`) | 툴이 슬롯을 직접 읽는 방식으로 만들면 전환 시 재작업 |
| **Phase 3** ROI → `Region` 포트 | Region 포트 규약이 확정된 뒤 `ExtractProfile`이 그걸 따라야 한다. 규약이 흔들리는 중에 만들지 않는다 |

**차단하지 않는 것**: ARCH Phase 4(Compare) · Phase 5(RegionMeasure 확장) · Phase 6(전용 노드 정합)은 이 문서와 독립이다. **이 문서 완료 후에 진행한다.**

```
ARCH Phase 0~3  →  [이 문서]  →  ARCH Phase 4~6  →  Geometry 1급화
```

### 0.3 폐기된 내용

이 문서의 이전 판에 있던 **Part A(`byPort` 추가 + 레거시 슬롯 이원화 + `SurfaceSubtract`)는 폐기됐다.**
[DESIGN_ARCH_BREAKING_CHANGES.md](./DESIGN_ARCH_BREAKING_CHANGES.md) Phase 2가 **이원화 없이 포트 기반으로 직접 전환**하고 `SurfaceSubtract`도 거기서 구현된다. 이 문서는 캘리퍼만 다룬다.

### 0.4 이 프로그램의 목적

VisionSW는 **노드 그래프로 검사 레시피를 조립하는 범용 도구**다. 레시피가 프로그램이고 엔진이 인터프리터다.
특정 검사(예: 평면 대비 높이 반복성)는 이 도구로 만든 **하나의 레시피일 뿐 목적이 아니다.**

판단 기준은 **"이 검사가 되나"가 아니라 "임의의 검사를 조립할 수 있나"**다.

### 0.5 빌드

[WORKTREE-SETUP.md](../../WORKTREE-SETUP.md)를 먼저 읽고 그 명령을 그대로 쓴다 (vcpkg baseline 함정 회피).

```bash
cmake --build build --config Release --target VisionEngine
ctest --test-dir build -C Release --output-on-failure
```

### 0.6 대상 파일

| 파일 | 역할 |
|---|---|
| `Core/include/Profile.h` | **신규** — 1D 샘플 신호 타입 |
| `Core/include/VisionData.h` | `profiles` 벡터 + 접근 헬퍼 추가 |
| `VisionTools/include/Aggregate.h` | **신규** — 집계 공용 헬퍼 (§3.4. ARCH Phase 5가 재사용) |
| `VisionTools/include|src/ExtractProfileTool.*` | **신규** — 높이맵 → Profile |
| `VisionTools/include|src/ProfileFeatureTool.*` | **신규** — Profile → 특징점·측정값 |
| `VisionTools/CMakeLists.txt` · `VisionEngine/src/ToolFactory.cpp` | 등록 |
| `ui/.../types/tools.ts` | PortType `Profile` 추가 · 노드 정의 |
| `ui/.../components/ProfilePlot.tsx` | **신규** — 1D 플롯 |
| `ui/.../components/NodePanel.tsx` | Profile 결과 표시 연결 |
| `Tests/Core/ProfileTest.cpp` · `Tests/Tools/ExtractProfileTest.cpp` · `ProfileFeatureTest.cpp` · `AggregateTest.cpp` | **신규** |

---

## 1. 전제되는 새 규약 (ARCH Phase 0~3 산출물)

이 문서의 코드는 아래를 **전제로 작성한다.** 구현 시작 전 실제 코드로 확인할 것 — 다르면 보고한다.

### 1.1 입력은 포트로만 읽는다

```cpp
// ARCH Phase 2 산출물
VisionDataPtr in(std::size_t port) const;
HeightMapPtr  inHeightMap(std::size_t port, std::size_t idx = 0) const;
RegionPtr     inRegion(std::size_t port, std::size_t idx = 0) const;
ProfilePtr    inProfile(std::size_t port, std::size_t idx = 0) const;   // 이 문서에서 추가
```

**툴이 `heightmap`·`regions` 같은 슬롯을 직접 읽는 코드를 쓰지 말 것.** 그 방식은 ARCH Phase 2에서 제거됐다.

### 1.2 출력은 이름 있는 측정값으로

```cpp
// ARCH Phase 1 산출물
struct Measurement { std::string name; double value; std::string unit; bool valid; };
struct Decision    { std::string name; bool pass; std::string reason; double measured, nominal, tolerance; };

// VisionData
std::vector<Measurement> measurements;
std::vector<Decision>    decisions;
std::vector<Overlay>     overlays;    // 표시용 부산물 — 측정값과 섞지 않는다
// heights 는 폐기됨
```

`PortType`에서 `Heights`가 폐기되고 `Measurements`·`Decisions`가 생겼다.

### 1.3 결과는 `VisionData`로만 흐른다

`lastResult()` + `dynamic_cast` 사이드채널은 ARCH Phase 1에서 전면 제거됐다. **새 툴이 그 방식을 부활시키지 말 것.**

### 1.4 출력은 항상 벡터

단일 출력도 `size 1`로 넣는다. 예외를 만들지 않는다.

### 1.5 ROI는 Region 포트로 받는다

ARCH Phase 3에서 `rois` 파라미터가 제거됐다(`CreateROI` 제외). **새 툴에 `rois` 파라미터를 만들지 말 것.**

---

## 2. 왜 이게 최대 공백인가

[벤치마크 §2-D](./3D_BASELINE_BENCHMARK.md) — 조사한 5개 제품 중 **4개가 보유**하고 VisionSW만 0개다. 미보유는 HALCON뿐이고, HALCON은 애초에 높이맵 전용 타입이 없어서다.

| 제품 | 보유 형태 |
|---|---|
| Aurora | `1D Edge Detection 3D` 9필터 + `Shape Fitting 3D` 10필터 + `SurfaceProfileAlongPath` 등 4개 = **19+** |
| Gocator | Surface Section → Profile 툴 14개 + Feature Point **14종** |
| InsWorks | 3D 횡단면 툴 1개에 통합 (컨투어 획득 → 특징 추출 → 측정) |
| eVision | ZMap이 정사영이므로 2D 게이지(EasyGauge) 재사용 |

**"높이맵을 1급으로 다루는 제품은 예외 없이 이 캘리퍼를 갖는다."** 3D 측정의 대부분이 결국 "단면에서 재는" 일이기 때문이다.

### D-1 결정 — 분해형 vs 통합형

| | 분해형 (Aurora) | 통합형 (InsWorks) | 중간 (Gocator) |
|---|---|---|---|
| 구성 | 추출·검출·피팅을 19개 독립 필터로 | 단일 툴 하나 | 툴 소수 + Feature Point를 파라미터 14종으로 |
| 장점 | 조합 자유도 최대 | 노드 수 최소 | 균형 |
| 단점 | 노드 폭발, 팔레트 비대 | 조합 불가, 로직이 툴에 갇힘 | — |

**Gocator식 중간안 채택.** 노드는 2개(`ExtractProfile`, `ProfileFeature`)만 만들고 **특징 종류는 `kind` 파라미터로 열거**한다.

근거: (1) 1인 프로젝트에서 노드 19개는 유지 비용이 크다 (2) 그러면서도 `Profile`이 포트로 흐르므로 추출과 검출을 자유롭게 재조합할 수 있다 — 통합형의 조합 불가 문제를 피한다 (3) `RegionMeasure`가 ARCH Phase 5에서 집계를 파라미터로 열거하게 되므로 관례가 일관된다.

---

## 3. 설계

### 3.1 `Profile` 타입

```cpp
// Core/include/Profile.h  (신규)
#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cmath>

namespace vision {

// ─────────────────────────────────────────────────────────────────
//  Profile — 1D 샘플 신호 (높이맵 단면). 1급 iconic 타입.
//
//  SoA 배치: 같은 인덱스 i가 한 샘플. 캐시 효율 + 부분 접근 용이.
//    s[i]        경로 시작점부터의 호장(arc length), mm. 단조 증가.
//    x[i], y[i]  샘플 위치, 소속 프레임의 mm 좌표.
//    z[i]        샘플 높이, mm. NaN = 무효.
//
//  z를 mm로 저장한다 — HeightMap의 raw count 인코딩을 쓰지 않는다.
//  이유: Profile은 격자가 아니라 신호이므로 분해능·zZeroCount 개념이
//  불필요하고, 소비자(특징 검출·측정)가 전부 mm를 원한다.
// ─────────────────────────────────────────────────────────────────
struct Profile {
    std::vector<double> s;
    std::vector<double> x, y, z;
    std::string frameId;    // 이 Profile의 좌표가 유효한 프레임. "" = 미지정
    std::string label;      // 다중 추출 시 식별용 (예: "row:120")

    std::size_t size() const { return z.size(); }
    bool empty()       const { return z.empty(); }
    bool valid(std::size_t i) const { return i < z.size() && !std::isnan(z[i]); }
};

using ProfilePtr = std::shared_ptr<Profile>;

} // namespace vision
```

**`VisionData`에 추가** — 규약 §1.4에 따라 벡터로.

```cpp
std::vector<ProfilePtr> profiles;
ProfilePtr inProfile(std::size_t port, std::size_t idx = 0) const;
```

**UI PortType**: `'Profile'` 추가 + `PORT_COLORS`에 기존 7색과 구분되는 색 배정.

**프레임**: Profile은 **새 프레임을 만들지 않는다.** 소스 HeightMap의 `frameId`를 그대로 복사한다. 격자가 아니라 그 프레임 안의 경로일 뿐이다.

### 3.2 `ExtractProfile` — 높이맵 → 단면

```
입력  : 포트0 HeightMap · 포트1 Region (선택)
출력  : Profile[]
카테고리: 변환
```

**입력 획득**: `inHeightMap(0)`, `inRegion(1)`.

**파라미터**

| 이름 | 기본 | 의미 |
|---|---|---|
| `mode` | `"line"` | `line` \| `axisX` \| `axisY` |
| `p0` / `p1` | `{0,0}` | `mode=line` 경로 양 끝점 |
| `unit` | `"mm"` | `mm` \| `px` — `p0`/`p1` 해석 단위. **기본을 mm로 둔다** (§3.5) |
| `count` | `0` | `mode=line` 샘플 수. 0이면 경로 길이 기준 1px 간격 자동 |
| `interp` | `"bilinear"` | `nearest` \| `bilinear` |
| `index` | `0` | `mode=axisX/Y` 단일 행/열 인덱스 |
| `span` | `1` | `mode=axisX/Y` 이웃 N줄 평균 (노이즈 저감) |
| `repeat` | `1` | `mode=axisX/Y` N개 평행 단면 → `profiles` 배열. **D-3 참조** |
| `channel` | `0` | 어느 채널을 z로 쓸지 |

**모드별 동작**

- **`line`**: `p0`→`p1` 직선을 `count`개로 균등 샘플. 격자 사이는 `interp`로 보간. 경로가 격자 밖이면 그 샘플 `z = NaN`.
- **`axisX`**: `index` 행을 **그대로** 뽑는다 (보간 없음, 원본 값 보존). `span>1`이면 이웃 행의 유효 픽셀만 평균.
- **`axisY`**: `index` 열. 동일.

**`axisX/Y`에 보간이 없는 것이 중요하다.** 측정 경로에서 원본 값을 그대로 쓸 수 있는 통로를 남긴다. `line`은 임의 각도라 보간이 불가피하다.

**규약**
- Region 포트가 연결되면 **Region 밖 샘플은 `z = NaN`**으로 만든다. 경로를 자르지 않는다 — 인덱스가 유지돼야 다운스트림이 위치를 안다.
- 출력 `Profile::frameId` = 입력 HeightMap의 `frameId`.
- `x`/`y`는 **항상 mm**로 채운다 (`xMm()`/`yMm()` 사용).
- `s[0] = 0`, 단조 증가.
- `repeat > 1`이면 `profiles`에 **`index` 오름차순 고정**으로 넣는다 (결정론).
- Region의 `frameId`가 HeightMap과 다르면 `Fail`. `LevelTool`과 동일 방침 + `TODO(T0-1 P3)` 주석.

**결정론**: 샘플 위치를 **누적 덧셈으로 계산하지 말 것.** `i/(count-1)` 비율로 매번 계산한다. 누적은 부동소수 오차가 쌓여 재현성이 깨진다.

### 3.3 `ProfileFeature` — 단면 → 특징점·측정값

```
입력  : 포트0 Profile
출력  : Point[], Measurements
카테고리: 측정
```

**입력 획득**: `inProfile(0)`.

**파라미터**

| 이름 | 기본 | 의미 |
|---|---|---|
| `kind` | `"maxZ"` | 아래 표 |
| `edgeDir` | `"any"` | `rising` \| `falling` \| `any` — 엣지 계열에서만 |
| `edgeThresholdMm` | `0.05` | 엣지로 인정할 최소 Z 변화량 |
| `smoothWindow` | `3` | 검출 전 이동평균 창 크기(샘플). 1이면 없음 |
| `nth` | `0` | 조건 만족 중 몇 번째(0-based). 음수면 뒤에서부터 |
| `searchFromMm` / `searchToMm` | `0` / `0` | 검색 구간 `s` 범위. 둘 다 0이면 전체 |
| `percentile` | `50` | `kind=percentile`·`highTail`에서 쓰는 백분위 |

**`kind` 목록** (Gocator Feature Point 14종 대응)

| `kind` | 뜻 | 집계 계열? |
|---|---|---|
| `maxZ` / `minZ` | 최대/최소 높이 샘플 | ● |
| `maxS` / `minS` | 유효 샘플 중 s가 가장 큰/작은 것 (양 끝) | |
| `mean` / `median` | 유효 z의 평균/중앙값. 위치는 무게중심 | ● |
| `percentile` / `highTail` | 백분위 / 상위 N% 최대높이 | ● |
| `stdDev` | 유효 z의 표준편차 (위치 없음) | ● |
| `edge` | `edgeDir`·`edgeThresholdMm` 기준 계단 | |
| `ridge` | 국소 볼록 정점 | |
| `valley` | 국소 오목 저점 | |
| `corner` | 기울기 급변점 (2차 차분 최대) | |

**집계 계열(●)은 §3.4의 공용 헬퍼를 쓴다. 여기서 다시 구현하지 말 것.**

**출력**
- `Point[]` — 찾은 위치. `RefPoint`에 mm와 px 환산 모두 채운다.
- `Measurements` — 이름 있는 값. `name`은 `"<kind>"` 또는 `"<kind>_z"`/`"<kind>_s"`처럼 **무엇인지 드러나게**. `unit`은 `"mm"`.
- 못 찾으면 `ToolStatus::Fail` + 이유. **빈 결과를 Ok로 내지 말 것.**

**서브픽셀**: 엣지·리지·밸리·코너는 **인접 3점 포물선 피팅으로 서브픽셀 위치**를 낸다. 정수 인덱스만 내면 반복성이 격자 분해능에 갇힌다. 이 프로젝트는 σ 0.0015mm를 다루므로 서브픽셀이 필수다.

**NaN 처리**: 무효 샘플은 검출에서 제외한다. **무효 구간을 건너뛰며 이어붙이지 말 것** — 없는 엣지를 만들어낸다. 무효를 만나면 그 지점에서 연속 구간이 끊긴 것으로 취급한다.

### 3.4 `Aggregate.h` — 집계 공용 헬퍼 (**중요**)

`ProfileFeature`의 집계 계열과 ARCH Phase 5의 `RegionMeasure` 확장이 **같은 집계를 필요로 한다.** 두 곳에 따로 구현하면 미묘하게 달라지고, 특히 `HighTail`이 갈리면 [repeatability_findings.md](../repeatability_findings.md)의 기준이 무너진다.

**이 문서에서 공용 헬퍼를 먼저 만들고, ARCH Phase 5가 그것을 재사용한다.**

```cpp
// VisionTools/include/Aggregate.h  (신규)
#pragma once
#include <vector>
#include <cstddef>

namespace vision::agg {

// 전부 NaN을 건너뛴다. 유효 표본이 0이면 valid=false.
struct Result { double value = 0; bool valid = false; std::size_t n = 0; };

Result mean      (const double* v, std::size_t n);
Result median    (const double* v, std::size_t n);
Result maxV      (const double* v, std::size_t n);
Result minV      (const double* v, std::size_t n);
Result stdDev    (const double* v, std::size_t n);          // 표본 표준편차 (n-1)
Result percentile(const double* v, std::size_t n, double p); // p: 0~100

// 상위 pct% 표본의 평균 — repeatability findings의 핵심 지표.
// ⚠️ 구 HeightMeasure의 HighTail 알고리즘을 **원형 그대로** 이관할 것.
//    정렬 방식·경계 처리·표본 수 반올림이 σ에 직접 영향한다.
Result highTail  (const double* v, std::size_t n, double pct);

} // namespace vision::agg
```

**작업 지시**
1. 구 `HeightFromPlaneTool`(또는 `HeightMeasure`)의 `HighTail` 구현을 **읽고 그대로 옮긴다.** 개선하지 말 것.
2. 옮긴 뒤 **원 구현과 같은 입력에 같은 값을 내는지** 단위 테스트로 못박는다. 이게 나중에 `RegionMeasure` 이관의 안전망이 된다.
3. NaN·빈 입력·단일 표본·전부 동일값 경계를 전부 테스트한다.

### 3.5 좌표 단위 규약 — `unit` 기본을 mm로

기존 툴들은 좌표 파라미터가 px 기준이었다(`rois`, `LineCenter`의 `xRoi`). **새 노드는 mm를 기본으로 한다.**

근거: 프레임 트리(T0-1)가 mm를 정규 단위로 삼았고, 분해능이 바뀌어도(리샘플·halfRes) mm 파라미터는 유효하다. px 파라미터는 분해능 변경 시 조용히 의미가 달라진다. 벤치마크 §2-E2도 **"픽셀이 아니라 실물 단위여야 파트가 바뀌어도 파라미터가 유지된다"**를 표준 스펙으로 본다.

`unit: "px"`는 편의를 위해 남기되 **기본값은 `mm`**로 둔다.

### 3.6 UI — Profile 플롯

**1D 플롯이 없으면 사용자가 파라미터를 맞출 수 없다.** 5사 전부 결과를 눈으로 볼 수단을 제공한다(벤치 §2-G3).

`ui/.../components/ProfilePlot.tsx` 신규.

**최소 요구**
- x축 = `s`(mm), y축 = `z`(mm). 자동 범위 + 수동 고정 옵션.
- **NaN 구간은 선을 끊어 표시.** 0으로 잇지 말 것 — 없는 형상으로 보인다.
- `ProfileFeature`가 찾은 위치를 세로선/마커로 오버레이 (`overlays` 소비).
- `ExtractProfile`이 뽑은 경로를 `ImageViewer` 위에 선으로 오버레이 — 어디를 잘랐는지 봐야 한다.
- `profiles`가 여럿이면 드롭다운으로 선택 (`label` 표시).

**새 차트 라이브러리를 도입하지 말 것.** canvas/SVG로 직접 그린다. 기존 `ImageViewer`·`RoiCanvas`의 오버레이 방식을 재사용한다.

---

## 4. 노드 요약

| 노드 | 입력 | 출력 | 카테고리 | 새 프레임 | 벤치 대응 |
|---|---|---|---|---|---|
| `ExtractProfile` | 0:HeightMap · 1:Region? | Profile[] | 변환 | 아니오 | §2-D1 |
| `ProfileFeature` | 0:Profile | Point[], Measurements | 측정 | 아니오 | §2-D2, §2-D4 |

**신규 타입**: `Profile` (Core + PortType).
**신규 공용 모듈**: `Aggregate.h` (ARCH Phase 5가 재사용).

---

## 5. Phase 순서

```
Phase 0  ARCH Phase 0~3 완료 확인 (§0.2). 미완이면 여기서 중단하고 보고
Phase 1  Aggregate.h — HighTail 원형 이관 + 원 구현 대조 테스트
Phase 2  Profile 타입 + PortType + ProfilePlot 골격
Phase 3  ExtractProfile — axisX/axisY (보간 없음)
Phase 4  ExtractProfile — line 모드 + 보간
Phase 5  ProfileFeature — 집계 계열 (maxZ/minZ/mean/median/percentile/highTail/stdDev)
Phase 6  ProfileFeature — 검출 계열 (edge/ridge/valley/corner) + 서브픽셀
```

### 왜 이 순서인가

- **`Aggregate.h`가 먼저**: `HighTail` 이관이 이 문서에서 가장 되돌리기 어려운 작업이다. 원 구현과 대조하는 안전망을 먼저 세운다. ARCH Phase 5도 이걸 기다린다.
- **Phase 3이 Phase 4보다 먼저**: `axisX/Y`는 보간이 없어 **원본 행과 bit-identical 대조**가 가능하다. 여기서 좌표·NaN·프레임 규약을 확정하고 `line`이 따른다.
- **Phase 5가 Phase 6보다 먼저**: 집계는 서브픽셀이 없어 정답이 자명하다. 여기서 검색구간·NaN·`nth` 규약을 확정한다.

---

## 6. 검증 (V1~V4)

[DESIGN_ARCH_BREAKING_CHANGES.md §1](./DESIGN_ARCH_BREAKING_CHANGES.md)의 체계를 따른다.

### V1 — 합성 데이터 정답 대조 (주 게이트)

ARCH Phase 0의 픽스처를 쓴다. **정답을 구현과 같은 식으로 계산하지 말 것.**

| 테스트 | 픽스처 | 정답 |
|---|---|---|
| `axisX`가 원본 행과 일치 | 아무 것 | 원본 `rawAt`/`zMm` 값과 bit-identical |
| 기울어진 평면의 단면이 직선 | `makeTiltedPlane(a,b,c)` | `z(s)` 기울기 = `a·cosθ + b·sinθ` (해석적) |
| 계단 엣지 위치 | `makeStep(stepCol, ...)` | `stepCol`의 mm 좌표. **서브픽셀 오차 < 0.1 px** |
| 계단 높이 | `makeStep(baseZ, stepZ)` | `stepZ - baseZ` |
| 45° 경로 보간 | `makeTiltedPlane` | 해석적 평면값 |
| 집계값 | `makeTiltedPlane` + `injectNoise(σ, seed)` | 독립 경로로 계산한 평균·중앙값·σ |
| `HighTail` | 고정 배열 | **구 구현 출력과 동일** |

### V2 — 불변량

| 불변량 |
|---|
| `axisX` 결과가 원본 행과 **bit-identical** (보간 없음 보장) |
| 같은 입력·파라미터 반복 실행 시 동일 출력 (**멱등성**) |
| `repeat>1` 출력 순서가 실행마다 동일 (결정론) |
| Region 마스킹이 인덱스를 유지 (길이 불변, 밖은 NaN) |
| `s[0]=0` · `s` 단조 증가 |
| `Profile::frameId` == 소스 HeightMap `frameId` |
| 전부 NaN 입력 → `ProfileFeature`가 `Fail` (조용한 0 반환 금지) |
| NaN 구간 양쪽에 걸친 허위 엣지가 검출되지 않음 |

### V3 — 실데이터 반복성

신규 노드만 추가하므로 기존 σ에 영향이 없어야 한다.

```
after.csv의 avg σ ≤ baseline × 1.10  및  worst range ≤ baseline × 1.10
```

**추가로**: `ExtractProfile → ProfileFeature(edge)` 조합으로 실데이터 100장에서 **엣지 위치의 σ를 측정해 기록**한다. 이게 캘리퍼의 실전 성능 지표이고, 이후 개선의 기준선이 된다.

### V4 — 골든 수치

`docs/GOLDEN_NUMBERS.md`에 캘리퍼 측정값을 추가한다. 무단 갱신 금지.

---

## 7. 금지 사항

1. **ARCH Phase 0~3 미완 상태에서 시작 금지** (§0.2).
2. **`in(port)` 계열 헬퍼 외로 입력 읽기 금지.** 슬롯 직접 접근은 폐기된 방식이다.
3. **`lastResult()` 사이드채널 부활 금지.** 결과는 `VisionData`로만.
4. **`Heights` 출력 금지.** `Measurements`를 쓴다.
5. **`rois` 파라미터 신설 금지** (§1.5).
6. **집계를 `ProfileFeature`에 직접 구현 금지.** `Aggregate.h`를 쓴다 (§3.4).
7. **`HighTail` 알고리즘 개선 금지.** 원형 그대로 이관 (§3.4).
8. **NaN 구간을 이어붙이며 검출 금지** (§3.3).
9. **정수 인덱스만 내는 엣지 검출 금지.** 서브픽셀 필수 (§3.3).
10. **누적 덧셈으로 샘플 위치 계산 금지** (§3.2).
11. **측정값과 표시용 부산물을 섞지 말 것.** `measurements` vs `overlays`.
12. **새 차트 라이브러리 도입 금지** (§3.6).
13. **테스트가 구현과 같은 식으로 정답 계산 금지.** 상수 또는 독립 경로.
14. **난수 시드 미고정 테스트 금지.**
15. **병렬화 선제 투입 금지.** 직렬 구현 후 실측.
16. **자동 커밋 금지.** Phase 완료 후 diff 보고 + 승인.

---

## 7.5 완료 보고 규칙

[DESIGN_T0_SURFACE_PRIMITIVES.md §7.5](./DESIGN_T0_SURFACE_PRIMITIVES.md)와 동일.

**"확인했습니다"류 요약 금지. 명령과 출력 원문만.**

필수: ① 실행 명령 원문 ② 출력 원문(`diff`가 비었으면 "출력 없음"이라 명시) ③ `ctest` 실제 요약 줄 ④ 변경 파일 + diff(문서에 없는 파일은 이유) ⑤ 문서에 없는 결정 목록 ⑥ 체크리스트 항목별 상태(부분 완료를 완료로 보고 금지).

**이 문서 추가 항목**: **V3 σ 수치를 before/after로 함께 제시.** "악화 없음"이라 쓰지 말고 실제 숫자를 적는다. Phase 6에서는 **엣지 위치 σ 실측치**도 함께.

**검증 담당**: 빌드·테스트·V3·성능은 Claude Code. 코드 리뷰는 **subagent**(구현 컨텍스트 미노출). 체크리스트 실재 확인은 별도 감사 세션.

---

## 8. 작업 체크리스트

### Phase 0 — 선행 확인
- [ ] **0-1** ARCH Phase 0(검증체계)·1(Measurement)·2(포트기반)·3(Region 포트) 완료 확인. **미완이면 중단·보고**
- [ ] **0-2** §1의 전제 규약이 실제 코드와 일치하는지 확인. 다르면 보고
- [ ] **0-3** ARCH Phase 0의 합성 픽스처에 `makeStep`이 있는지 확인. 없으면 추가

### Phase 1 — `Aggregate.h`
- [ ] **1-1** `VisionTools/include/Aggregate.h` — mean/median/max/min/stdDev/percentile/highTail
- [ ] **1-2** 구 `HighTail` 구현을 **원형 그대로 이관** (개선 금지)
- [ ] **1-3** 원 구현과 동일 입력·동일 출력 대조 테스트
- [ ] **1-4** 경계 테스트: 빈 입력 · 전부 NaN · 단일 표본 · 전부 동일값 · `pct` 0/100
- [ ] **[검증]** V1 통과. **ARCH Phase 5가 이걸 재사용한다는 메모를 ARCH 문서에 追記**

### Phase 2 — `Profile` 타입
- [ ] **2-1** `Core/include/Profile.h` (§3.1). SoA, z는 mm
- [ ] **2-2** `VisionData`에 `profiles` 벡터 + `inProfile()` 헬퍼
- [ ] **2-3** `tools.ts` PortType `'Profile'` + `PORT_COLORS`
- [ ] **2-4** `ProfilePlot.tsx` 골격 — NaN 구간 선 끊기, 자동/수동 범위
- [ ] **[검증]** 빌드 + V3 (신규 타입뿐이므로 σ 변화 없어야)

### Phase 3 — `ExtractProfile` (axis 모드)
- [ ] **3-1** `ExtractProfileTool.h/.cpp` — `axisX`/`axisY`, `index`·`span`·`repeat`·`channel`
- [ ] **3-2** 입력을 `inHeightMap(0)`/`inRegion(1)`로 획득
- [ ] **3-3** `x`/`y` mm 채움, `s[0]=0` 단조증가, `frameId` 복사, Region 프레임 불일치 Fail
- [ ] **3-4** Region 밖 `z=NaN` (길이 불변)
- [ ] **3-5** `repeat>1` 출력 순서 `index` 오름차순 고정
- [ ] **3-6** 등록 3곳 + `ImageViewer` 경로 오버레이
- [ ] **3-7** 단위 테스트: **원본 행과 bit-identical** · `span` 평균 · NaN 전파 · Region 마스킹 · `repeat` 순서 · 멱등성
- [ ] **[검증]** V1·V2·V3

### Phase 4 — `ExtractProfile` (line 모드)
- [ ] **4-1** `line` 모드 + `p0`/`p1`/`unit`(**기본 mm**)/`count`/`interp`
- [ ] **4-2** 샘플 위치를 `i/(count-1)` 비율로 계산 (누적 금지)
- [ ] **4-3** 격자 밖 `z=NaN`. bilinear에서 이웃 중 NaN이면 결과 NaN
- [ ] **4-4** 단위 테스트: 축평행 line == `axisX` 결과 · 45° 경로 해석적 정답 · `nearest` vs `bilinear` · 경계 · `unit=px` 환산
- [ ] **[검증]** V1·V2·V3

### Phase 5 — `ProfileFeature` (집계 계열)
- [ ] **5-1** `ProfileFeatureTool.h/.cpp` — `maxZ`/`minZ`/`maxS`/`minS`/`mean`/`median`/`percentile`/`highTail`/`stdDev`
- [ ] **5-2** **집계는 `Aggregate.h` 호출만.** 직접 구현 금지
- [ ] **5-3** `searchFromMm`/`searchToMm` 구간 제한, `nth` 선택, NaN 제외
- [ ] **5-4** 출력 `Point[]` + `Measurements`(이름 있는 값, unit mm). 못 찾으면 Fail
- [ ] **5-5** 등록 3곳 + 플롯 마커 오버레이(`overlays`)
- [ ] **5-6** 단위 테스트: 각 `kind` 정답 · 전부 NaN이면 Fail · 검색구간 · `nth` 음수 · `Aggregate`와 값 일치
- [ ] **[검증]** V1·V2·V3

### Phase 6 — `ProfileFeature` (검출 계열)
- [ ] **6-1** `edge`(`rising`/`falling`/`any`) + `edgeThresholdMm` + `smoothWindow`
- [ ] **6-2** `ridge`/`valley`/`corner`
- [ ] **6-3** **서브픽셀** — 인접 3점 포물선 피팅
- [ ] **6-4** NaN 구간 이어붙이지 않음 확인 (§7-8)
- [ ] **6-5** 단위 테스트: `makeStep`의 알려진 위치를 **서브픽셀 오차 < 0.1 px로** 복원 · `edgeDir` · 임계 미달 미검출 · NaN 구간 허위 엣지 없음 · `smoothWindow` 효과
- [ ] **6-6** **실데이터 엣지 위치 σ 실측·기록** (V3 추가 항목)
- [ ] **[검증]** V1·V2·V3·V4 + 골든 수치 추가

---

## 9. 미결정 사항

| # | 항목 | 상태 |
|---|---|---|
| **D-1** | 캘리퍼 구성 — 분해형 vs 통합형 vs 중간 | **중간안 잠정 결정** (§2). 노드 2개 + `kind` 파라미터. Phase 6 완료 후 노드 수·사용성 재평가 |
| **D-2** | `Profile`의 z를 mm로 저장 | **잠정 결정** (§3.1). 성능 문제 없는지 Phase 3에서 확인 |
| **D-3** | `repeat>1`로 `Profile[]`을 내도 `ProfileFeature`가 원소별로 못 받는다 (브로드캐스트 미배선) | **Phase 3에서 결정.** 아래 참조 |
| D-4 | `ProfileFeature`가 여러 특징점을 한 번에 낼 필요가 있는지 (현재 `nth`로 1개) | Phase 6 이후 관찰 |
| D-5 | `corner`의 2차 차분 방식이 노이즈에 충분히 강한지 | Phase 6에서 실데이터로 확인 |
| D-6 | `LineCenter`(전용)와 `ProfileFeature`의 역할 중복 — 라인 찾기는 캘리퍼의 특수 사례다 | Phase 6 이후. 전용 유지 결정이 있으므로 통합은 보류 |

### D-3 상세 — 브로드캐스트 미배선

`repeat>1`로 여러 단면을 뽑아도 하류가 원소별로 못 받으면 기능이 반쪽이다. 선택지:

| | 내용 | 평가 |
|---|---|---|
| (a) | `repeat`를 Phase 3에서 빼고 단일 단면만 | 가장 안전. 나중에 추가 |
| (b) | `ProfileFeature`가 `profiles[0]`만 소비 | 진행 가능하나 사용자를 오해시킨다 |
| (c) | T0-2 브로드캐스트 실행 배선을 여기서 함께 | 범위 확대. **단 ARCH Phase 2 이후 비용이 크게 낮아졌다** |

**(c)의 비용이 예상보다 낮아졌다.** ARCH Phase 2가 입력을 포트 기반으로, 출력을 전부 벡터로 바꿔놓으므로 브로드캐스트는 "입력 배열 길이 N을 보고 노드를 N회 실행"만 추가하면 된다. `Broadcast.h`의 `computeBroadcast()`도 이미 순수 함수로 검증돼 있다.

**권장: Phase 3에서 (a)로 시작해 Phase 6까지 완주하고, 그 시점에 (c)를 재평가.** 배열 소비자가 `ProfileFeature`·`RegionMeasure` 둘이 되므로 배선 근거가 명확해진다. Phase 3에서 `repeat` 파라미터를 **정의만 하고 값을 1로 강제**해두면 나중에 여는 비용이 최소가 된다.

---

## 10. 이 문서 이후

```
1  ARCH Phase 4~6          Compare/Decision · RegionMeasure 확장(Aggregate 재사용) · 전용 노드 정합
2  Geometry 1급화           Line·Circle 타입 → T1-3 검출점 피팅 → T1-5 거리·각도·교점
3  브로드캐스트 배선 + T2   ConnectedComponents · 메트릭 객체 필터 (D-3의 (c))
4  T3-1 Fixture             Align 대체 → T3-2 정렬 → T3-3 골든 비교
5  T0-6 중첩 ToolBlock
```

### 왜 Geometry가 그 다음인가

`ProfileFeature`가 여러 단면에서 점을 찾아도 **그 점들을 직선·원으로 묶는 수단이 없으면 단발 측정에 머문다.** [벤치 §2-D3](./3D_BASELINE_BENCHMARK.md)가 "단발 측정을 노이즈에 강한 측정으로 바꾸는 단계"라고 짚은 부분이다.

그리고 [§2-C5](./3D_BASELINE_BENCHMARK.md) — 도면 치수는 대개 "면과 면 사이", "선과 원 중심 사이"처럼 **조합값**이므로, 기하 연산이 없으면 피팅 결과가 최종 측정값이 되지 못한다.

이 시점에 ARCHITECTURE_DIRECTION §3.0의 **"단일 `Geometry` 포트 + kind 태그"** 안이 유효한지 재검토한다. `Plane`·`Point`가 이미 개별 포트로 분리돼 있으므로 일관성 판단이 필요하다.

### 이 문서가 남기는 자산

| 자산 | 이후 소비처 |
|---|---|
| `Aggregate.h` | ARCH Phase 5 `RegionMeasure` 확장 — `HighTail` 단일 출처 확보 |
| `Profile` 타입 | T1-3 피팅(여러 단면의 점 수집), T2 객체별 단면 검사 |
| `ProfilePlot` | 이후 1D 신호 표시 전부 |
| 서브픽셀 검출 관례 | T1-3 피팅 정밀도의 전제 |
