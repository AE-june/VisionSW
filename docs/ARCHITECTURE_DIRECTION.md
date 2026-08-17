# VisionSW — 구조·UI 방향 설계 문서 (Architecture & Direction)

> 이 문서는 VisionSW의 상위 설계 기준이다. 이후 모든 리팩토링·기능개발은 이 문서의 방향을 따른다.
> 최초 작성: 2026-07-22.

## Context (왜 이 문서인가)

VisionSW의 근본 목적: **2D/3D(주로 3D) 이미지를 받아 검사 툴을 조합해 원하는 검사를 수행하고 결과를 출력**한다. 다양한 2D/3D 데이터 포맷을 염두에 둔 툴 설계가 필요하다.

`ToolFactory.cpp`(1,460줄) 리팩토링을 논의하던 중, 로더/캐시의 계층 문제가 **개별 파일 정리 문제가 아니라 전체 데이터 모델·아키텍처 방향의 문제**임이 드러났다. 그래서 코드 수정 전에 (1) 동종 타사 최신 툴을 조사하고 (2) 그에 비춰 구조·UI 방향을 재점검해 (3) 설계를 먼저 문서화했다.

---

## 1. 경쟁 제품 조사 요약 — "사용 난이도" 축이 핵심

**설계 북극성 = 일반 유저(비전 비전문가)가 쉽게 쓸 수 있는 툴.** HALCON 같은 고성능 스크립트 툴은 UX 벤치마크가 아니라 **내부 데이터 모델 참조**로만 쓴다 (허들이 너무 높음).

**사용 난이도 스펙트럼 (쉬움 → 어려움):**

| 단계 | 패러다임 | 대표 | 타깃 유저 |
|------|---------|------|----------|
| 1. 교시(teach-by-example) AI | 좋은/불량 샘플 제시 → 학습 | LandingLens(Landing AI), Keyence auto-teach | 완전 비전문가·현장 |
| 2. 가이드 위저드 | 단계별 포인트&클릭 | Cognex In-Sight EasyBuilder | 초보·현장 |
| **3. 노드/플로우 그래프** | 비주얼 프로그래밍 | Aurora Vision Studio, Matrox Design Assistant, **← VisionSW** | 비전 엔지니어 |
| 4. 스크립트 | 코드 | HALCON (데이터모델만 참조) | 전문가 |

**VisionSW의 목표 위치 = 3단계(노드 기반) 유지 + Aurora/eVision 수준의 접근성.** 위저드-우선(2단계)이나 교시형 AI(1단계)로 내려가지 않고, **노드 패러다임 안에서** "코딩 없이 비주얼로 조립하고 실시간으로 확인하는" 접근성을 갖춘다.

**목표 레퍼런스:**
- **Zebra Aurora Vision Studio** — UX 벤치마크. 드래그&드롭, 실시간 프리뷰, 준비된 필터, 좋은 기본값, HMI Designer.
- **Euresys Open eVision** — 기능(특히 3D) 벤치마크. 레이저라인 추출, 캘리브레이션, 점군 처리, 3D 정렬/검사.

**데이터 모델 참조 (HALCON, 내부용):** iconic 데이터[Image(다채널)·Region(임의 픽셀집합)·XLD(서브픽셀 컨투어)·3D Object Model] ↔ control 데이터(결과값) 분리.

---

## 2. VisionSW의 위치 (Niche)

**PC 기반 + C++ 커스텀 알고리즘 + 3D(HeightMap) 특화 + 노드 그래프.** 현재 난이도 3단계.

**차별화 니치: "접근성 좋은 3D 높이맵 검사 툴".** Aurora/eVision은 범용(2D 중심), 쉬운 쪽(Keyence·EasyBuilder)도 대부분 2D. VisionSW는 노드 그래프의 유연함 + Aurora식 접근성을 **산업용 3D 센서 높이맵 정밀검사**에 특화한다. 범용 1000-필터 경쟁이 아니라 3D 정밀검사 + 낮은 진입장벽이 핵심.

---

## 3. 데이터 모델 방향 (확정 — HALCON 개념 증분 도입)

**베이스 = HALCON iconic/control 골격** (Image·Region·control 분리). XLD·메시는 보류, Geometry·Profile은 노드 그래프 편의를 위해 1급 타입으로 추가(HALCON은 control 튜플로 처리 → 의도적 이탈).

**현재 상태:** B1 완료 — `HeightMap`이 N채널 float 타입으로 확장되고 `using Image = HeightMap` 별칭 확정. 아직 `Image2D`(uint8)는 별도로 남아 있고 `VisionData`의 `image`/`heightmap` 슬롯도 분리 상태(완전 통합은 소비자 필요 시점에 증분).

### 3.0 확정 타입 모델 (골격 잠금 — 2026-07-22)

**Iconic (포트로 흐르는 이미지성):**
| 타입 | 담는 것 | HALCON 대응 | 상태 |
|---|---|---|---|
| **Image** | N채널 float 격자 + 보정 (역할채널 height/intensity/thickness…) | Image | ✅ B1 (HeightMap=Image) |
| **Region** | 픽셀 집합/마스크 | Region | B2 신규 |
| **PointCloud3D** | 점군 | 3D Object Model(점군만) | ✅ |
| **Geometry** | 피팅 기하 프리미티브 (Point·Line·Circle·Plane), kind 태그 + 파라미터 | (control 튜플) — 의도적 이탈 | 신규 (PlaneModel·RefPoint 흡수) |
| **Profile** | 1D 샘플 신호 (값 배열 + 좌표/경로) | (measure object) — 의도적 이탈 | 신규 |

**Control (결과값):**
| 타입 | 담는 것 | 상태 |
|---|---|---|
| **Scalar/Array** | 수치 측정값 (높이·거리·점수…) | `heights` 흡수 |
| **Decision** | 합/불 (+사유) | B3 신규 |

**포트 세분화:** `Geometry`·`Profile`은 **단일 포트 + kind 태그**로 시작(권장 ①, flat PortType에 적합). 타입안전 필요 시 도형별 포트로 승격.

**타입 통합 이행:** `PlaneModel`→`Geometry(Plane)`, `RefPoint`→`Geometry(Point)`, `heights(vector<double>)`→`Array`(control).

### 3.1 통합 N채널 Image (B1 완료)
Image2D와 HeightMap을 **하나의 N채널 Image 타입**으로 통합 (HALCON Image에 충실). 같은 W×H 격자에 정렬된 N개 채널.
- **채널 = 역할(role)** — 예: `height`, `intensity`, `thickness`. → **Z/I/L 세트가 한 Image의 3채널**이 되어 SDC 머지·리플렉션 필터가 1급 데이터 위에서 동작.
- **확정된 구현 결정:** ①전 채널 float 통일, ②HeightMap=Image 별칭으로 점진 이행(기존 접근자 보존), ③UI 포트 `HeightMap`/`Image2D` 라벨 유지.
- **남은 범위:** `Image2D` 흡수 + `image`/`heightmap` 슬롯 병합 (소비자 필요 시점에 증분).

### 3.2 Region 1급화 (B2)
픽셀 집합/마스크를 포트로 흐르는 1급 iconic 타입으로 추가. ROI(사각/회전/폴리곤)가 Region을 생산. → "세그먼트 → 그 영역 안에서 측정" 워크플로우 가능. 현재 `Rect2D`(파라미터 전용)를 Region 생산자로 승격.

### 3.3 iconic vs control 분리 (B3)
측정결과(heights/plane/score/합불)를 `VisionData`에서 별도 "results/control" 구조로 분리. iconic(Image/Region/Cloud/Geometry/Profile)과 명확히 구분.

**보류:** XLD(서브픽셀 컨투어), 3D 메시. 수요 생기면 추가.

---

## 4. 아키텍처 방향 (Narrow-Waist)

**모든 포맷 다양성을 경계(로더/세이버)에 가두고, 내부는 소수의 정규 타입으로 통일한다.**

```
외부 포맷 (PNG16/8, raw, CSV, TIFF, PLY, 센서 SDK, ...)
   │  로더/세이버 = 포맷 어댑터 (여기만 파일 포맷을 앎) + 정규화(무효→NaN, 분해능·단위 통일)
   ▼
정규 타입 (Image / PointCloud3D / Region ...)   ← Core(인터페이스), 구현은 라이브러리 링크한 계층
   │
검사 툴 (포맷 무관, 정규 타입만) ← VisionTools
   ▼
결과 출력
```

**계층 규칙:**
- **Core**: 정규 타입 + 인터페이스(`IHeightMapLoader` 등). 외부 이미지 라이브러리 의존 없음(순수 유지).
- **VisionTools**: 순수 알고리즘 + 포맷 구현체(예: `PngHeightMapLoader` — 이미 링크된 OpenCV 재사용).
- **VisionEngine**: WS 서버 + 런타임 인프라(캐시·preload·저장경로). 로더를 감싸는 **CachingLoader 데코레이터**로 캐시 분리. `ToolFactory`는 얇은 디스패처 + 의존성 주입.

**효과:** N포맷 × M툴 폭발 방지 → N로더 + M툴. 새 포맷=로더 하나(툴 불변), 새 툴=모든 포맷 자동 동작.

**계층 위반 사례(현재, 해소 대상):** `HeightMapLoaderTool`(ToolFactory 내부)이 엔진 전역 캐시(`g_heightmapFileCache`, `heightmapCachePut`, `loadHeightMapFromFile`)를 직접 호출 → 툴이 엔진 인프라에 묶임. `IHeightMapLoader` 인터페이스가 이미 Core에 있으나 실제 PNG 로딩은 이를 우회해 엔진에 인라인됨.

---

## 5. UI 방향 — 계층형 UX (쉬운 층 + 파워 기반)

**북극성: 일반 유저가 쉽게.** 노드 그래프(파워 기반)를 유지하되, 그 위에 접근성 층을 얹는다 (progressive disclosure).

**현재:** NodeCanvas/ToolNode/NodePanel/ParamPanel/ImageViewer/ResultPanel/FolderInspectPanel/PlaneView3D.

**확정 UI 추가:**
- **다중 뷰 결과 프리뷰 + 파라미터 실시간 갱신** (Aurora 핵심 접근성).
- **Region/ROI 편집·시각화** (Region 1급화와 짝).

**접근성 층 후보 (보류/향후):** 템플릿·레시피 프리셋, 가이드 모드(위저드), 툴 자동튜닝.

**전략적 선택(YAGNI 후보):** 오퍼레이터 HMI 런타임 모드(양산 배포 시), 교시형 딥러닝(니치 강화 시).

---

## 6. 결정사항

**확정 (2026-07-22):**
- UX 방향 = 노드 그래프 기반 + Aurora/eVision 수준 접근성. 3단계 유지.
- 아키텍처 = narrow-waist. 계층 규칙(4장) 준수.
- 데이터 모델 = **HALCON iconic/control 골격 베이스** + 증분 도입: ①통합 N채널 Image, ②Region 1급화, ③iconic/control 분리. XLD·메시 보류.
- **정규 타입 골격 잠금(§3.0):** Iconic = Image·Region·PointCloud3D·**Geometry·Profile**, Control = Scalar/Array·Decision.
- **Geometry·Profile = 1급 iconic 타입, 단일 포트 + kind 태그** (HALCON은 control 튜플이나 노드 그래프 편의 위해 이탈).
- **툴박스 = Aurora식 2계층** (§9): 저수준 프리미티브(타입별) + 제어흐름 + 고수준 작업 툴.
- UI 추가 = ①다중 뷰 결과 프리뷰+실시간 갱신, ②Region/ROI 편집·시각화.

**B1 열린 결정 → 확정됨:** ①전채널 float ②HeightMap=Image 별칭(점진) ③UI 포트 HeightMap/Image2D 라벨 유지.

---

## 7. 개발 항목 백로그 (카테고리별)

기존 동작 보존이 원칙(회귀검증).

**A. 아키텍처/리팩토링**
- A1. 로더 narrow-waist 리팩토링 — `PngHeightMapLoader : IHeightMapLoader`(VisionTools) + `CachingLoader`(엔진) + ToolFactory 주입. 한글경로 보존.
- A2. ToolFactory god파일 분해 — 순수 알고리즘→VisionTools/, I/O·캐시 글루→VisionEngine/src/tools/, ToolFactory는 얇은 디스패처.
- A3. ExposureMergeCore.h 위치 정리 — SDK도 참조 → Core/VisionTools로 이동.

**B. 데이터 모델 (HALCON 증분) — 툴박스 골격**
- B1. 통합 N채널 Image 타입 ← 기반. ✅ **완료** (토대; Image2D 흡수는 증분).
- B2. **Region 1급 타입** — 픽셀 마스크 포트 + ROI 생산자. 조합 자유도의 핵심.
- B3. iconic vs control 분리 — 측정결과(Scalar/Array/Decision)를 results 구조로.
- B4. **Geometry 1급 타입** — Point/Line/Circle/Plane 통합 (PlaneModel·RefPoint 흡수). 단일 포트+kind. (B2와 함께)
- B5. **Profile 1급 타입** — 1D 샘플 신호. (수요 시점)

**F. 프리미티브·제어흐름 (툴박스 조합력)**
- F1. **제어흐름 노드** — ForEach·Branch·Merge·Filter (임의 로직 조립의 핵심).
- F2. **타입변환 프리미티브** — Threshold(Image→Region), ConnectedComponents, Fit(→Geometry), ExtractProfile 등. (각 타입 성숙 시 증분)
- F3. **Control 연산** — Compare(공차→Decision)·ScalarMath·CombineDecision. (B3 위)

**C. UI (접근성)**
- C1. 다중 뷰 결과 프리뷰 + 파라미터 실시간 갱신.
- C2. Region/ROI 편집·시각화 UI (B2 의존).

**D. 문서/정리**
- D1. 설계 문서 리포지토리 반영 (이 문서). ✅
- D2. ONBOARDING 노드 카탈로그 갱신. ✅
- D3. ZMap→HeightMap 전면 rename. ✅

**A. 아키텍처/리팩토링** (그릇 정리 — 위 항목과 병행 가능)
- A1~A3 (위 참조).

**E. 보류 (로드맵)**
- 교시형 딥러닝 / 오퍼레이터 HMI / 템플릿·프리셋 / 툴 자동튜닝 / XLD·메시.

**권장 순서 (툴박스 우선 재정렬):** `B2`(Region) → `B4`(Geometry) → `F1`(제어흐름) → `B3`+`F3`(control 분리·연산) → `A1`(로더) → `A2·A3`(팩토리 분해) → `C2`(Region UI) → `C1`(멀티뷰) → `F2`·`B5`(증분).
> 근거: Region이 서면 임계값·블롭·ROI·측정영역이 열려 조합 자유도가 급증. Geometry·제어흐름이 뒤따르면 "기본 툴 조합으로 어떤 검사든"의 뼈대가 완성.

---

## 8. 검증 방법

- 각 구현 항목은 기존 동작 보존 회귀검증: `VisionEngine --repeat-analyze <recipe> <folder> <csv>`로 반복성/결과 무변화 확인, 머지 계열은 0오차/0px 검증.
- 빌드: `cmake --build build --config Release` + UI `npm run dev`.

---

## 9. 툴박스 분류체계 (Aurora식 2계층)

**목표:** 기본 툴 조합으로 어떤 검사든. 성립 조건 = ①소수 정규 타입(§3.0) + ②타입 간 변환/조합 연산 + ③제어흐름·결과연산.

**구성 = 저수준 프리미티브(타입별) + 제어흐름 + 고수준 작업 툴.** `■`=기존툴 재분류, `＋`=신규(조합 필수), `·`=향후.

### Layer 1 — 저수준 프리미티브 (타입별)
| 타입 | 프리미티브 |
|---|---|
| **Image** | ■NoiseFilter ■GapFill ■RowStretch ■EdgeDetector · ＋Threshold(→Region) ＋ChannelSelect/Merge ＋ImageMath ＋Crop/Resample · Gradient/Morphology |
| **Region** | ＋CreateROI(Rect/Circle/Polygon→Region) ＋Morphology ＋Boolean(∪∩−) ＋ConnectedComponents(→Region[]) ＋RegionMeasure(면적/무게중심/BBox→Control) |
| **PointCloud3D** | ■HeightMapToCloud ■ThicknessMeasure ＋CloudLoader(파일→Cloud) ＋CloudToProfiles(행별 Profile[], 다중Z 보존) ＋CloudCrop ·Downsample ·CloudToImage |
| **Geometry** | ■PlaneFit ＋FitLine/FitCircle(Region/Points→Geometry) ＋Intersect/Distance/Angle(→Control) |
| **Profile** | ＋ExtractProfile(Image+Line→Profile) ＋CloudToProfiles(Cloud→행별 Profile[]) ＋ProfileFeature(Profile[] 순회→Control) ＋ProfileEdge/Peak(→Point/Scalar) |
| **Control** | ■RefHeight ■HeightMeasure ＋Compare(값 vs 공칭±공차→Decision) ＋ScalarMath ＋CombineDecision |

> **Cloud 직접 분석 경로 (설계 확장, 2026-08):** 초기 설계는 HeightMap 중심(픽셀당 Z 1개)이라
> Cloud 분석을 `CloudToImage → 이미지 툴`로 상정했으나, **한 컬럼에 여러 Z가 있는 클라우드**(벽/오버행/
> 다중반사)는 HeightMap으로 표현 불가. 이를 위해 `CloudLoader`(파일→Cloud) + `CloudToProfiles`
> (행=Y bin별 Profile[], reduce=none 시 다중 Z 전부 보존) + `ProfileFeature`(Profile[] 순회)로
> **Cloud를 HeightMap 거치지 않고 직접 행별 분석**하는 경로를 추가했다.

### Layer 2 — 제어흐름 (F1)
＋ForEach(Region[]/Cloud/Array 순회) · ＋Condition/Branch · ＋Merge/Collect · ＋Select/Filter

### Layer 3 — 고수준 작업 툴 (기존 20개 재배치)
| 그룹 | 툴 |
|---|---|
| 입출력(IO) | ■HeightMapLoader ■ImageLoader ■ImageSaver ■CloudSaver ■CsvWriter |
| 3D 전처리(SDC) | ■ExposureMerge ■ExposureMerge2 ■ExposureMerge3 ■ExposureMergeCloud |
| 정렬 | ■LineCenter(Line Finder) ■Align |
| 측정 | ■PlaneFit ■RefHeight ■HeightMeasure ■ThicknessMeasure |
| (향후) | ·Blob검사 ·TemplateMatch ·EdgeGauge |

### UI 팔레트 그룹 (현행 입력/필터/정렬/측정/변환/출력 → 개편안)
```
[데이터] 입출력
[프리미티브] Image · Region · Cloud · Geometry · Profile · Control
[흐름] ForEach · Branch · Merge · Filter
[작업] 3D전처리 · 정렬 · 측정 · (검사)
```
저수준(프리미티브)·고수준(작업)을 시각적으로 구분 + 검색.

**현황 관찰:** 기존 20툴 중 저수준 프리미티브는 4개(NoiseFilter/GapFill/RowStretch/EdgeDetector)뿐. 조합 자유도 병목 = **Region·제어흐름·타입변환·결과연산 프리미티브 부재** → §7 B2/B4/F1/B3+F3 순으로 해소.
