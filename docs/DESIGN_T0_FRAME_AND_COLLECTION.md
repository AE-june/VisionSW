# 상세 설계 — T0-1 좌표계 프레임 트리 · T0-2 컬렉션 & ForEach

> **구현 지시서.** Claude Code 작업 입력용으로 작성됨.
> 상위 문서: [3D_BASELINE_BENCHMARK.md](./3D_BASELINE_BENCHMARK.md) §4 Tier 0, [ARCHITECTURE_DIRECTION.md](./ARCHITECTURE_DIRECTION.md)
> 작성: 2026-07-29. 대상 워크트리: `wt-refactor`.

---

## 0. 작업 시작 전 필독

### 이 문서의 범위

T0-1(좌표계)과 T0-2(컬렉션)만 다룬다. 다음은 **이 문서의 범위 밖**이고 별도 작업이다:
T1 캘리퍼 · T2 객체분할 · T3-1 Fixture 구현 자체 · T0-6 중첩 ToolBlock · A1~A3 리팩토링.

단, **T0-1 Phase 3이 T3-1(Fixture)의 진입점**이고 **T0-2 옵션 B가 T0-6과 같은 기계장치**이므로, 그 경계는 §5에 명시했다.

### 절대 원칙 — 기존 동작 보존

**모든 Phase는 측정 결과가 bit-identical해야 한다.** 이 프로젝트는 반복성 σ 0.0015mm 수준을 다루므로 부동소수 오차조차 회귀로 취급한다. 근거: [perf_findings.md](../perf_findings.md)의 "결과 보존 최적화만" 원칙, [repeatability_findings.md](../repeatability_findings.md).

각 Phase 완료 시 §6 회귀검증을 통과해야 다음 Phase로 간다. **통과 못 하면 되돌린다.**

### 빌드

빌드/세팅은 반드시 [WORKTREE-SETUP.md](../../WORKTREE-SETUP.md)를 먼저 읽고 그 명령을 그대로 쓴다 (vcpkg baseline 함정 회피).

```bash
cmake --build build --config Release --target VisionEngine
```

### 대상 파일

| 파일 | 역할 | T0-1 | T0-2 |
|---|---|---|---|
| `Core/include/Frame.h` | **신규** — 프레임·변환·레지스트리 | ● | |
| `Core/include/VisionData.h` | 파이프라인 공용 컨테이너 | ● | ● |
| `Core/include/HeightMap.h` | 격자 이미지 + px↔mm | ● | |
| `Core/include/Region.h` | 픽셀 마스크 | ● | ● |
| `VisionEngine/src/main.cpp` | `runPipeline` (topoSort·입력병합·캐시) L94~ | ● | ● |
| `VisionTools/src/AlignTool.cpp` | 원점 설정 | ● (Phase 3) | |
| `VisionTools/src/PlaneFitTool.cpp` | 평면 생산 | ● | |
| `VisionTools/src/ThresholdTool.cpp`, `CreateRoiTool.cpp` | Region 생산 | ● | ● |
| `VisionTools/src/ReduceDomainTool.cpp`, `RegionMeasureTool.cpp` | Region 소비 | ● | ● |
| `ui/src/renderer/src/types/tools.ts` | PortType·노드 정의 | | ● |
| `ui/src/renderer/src/components/NodeCanvas.tsx` | 연결 검증 L31~52 | | ● |

**호출처 규모 (조사 결과, 변경 부담 판단용)**

| 슬롯 | 사용 횟수 | 파일 |
|---|---|---|
| `->region` | 6 | CreateRoiTool, ReduceDomainTool, RegionMeasureTool, ThresholdTool, main.cpp |
| `->plane` | 9 | HeightFromPlaneTool, PlaneFitTool, main.cpp, vision_sdk.cpp, sdk_test.cpp |
| `->heights` | 15 | CsvWriterTool, HeightFromPlaneTool, RegionMeasureTool, main.cpp, vision_sdk.cpp |
| `->points` | 27 | LineCenterTool, NoiseFilter, main.cpp, ToolFactory.cpp, vision_sdk.cpp, ThicknessMeasureTest.cpp |
| `->origin` | 3 | AlignTool, LineCenterTool, main.cpp |

전부 두 자리 수 이하다. **필드를 메서드로 바꿔 컴파일 에러로 호출처를 전수 발견하는 방식이 안전하다** — 조용한 동작 변화보다 컴파일 실패가 낫다.

---

## 1. 현재 코드 사실

추측이 아니라 실제 코드다. 설계 판단의 근거이므로 변경 전 재확인할 것.

### 1.1 `HeightMap` — px↔mm 변환을 이미 보유

`Core/include/HeightMap.h`
```cpp
struct HeightMap {
    int   width, height, channels;
    float xResMm, yResMm, zResMm;   // 분해능
    float zZeroCount;               // 높이 0에 해당하는 raw count
    float originCol = 0.f;          // 좌표계 원점 X (px) — Align이 설정
    float originRow = 0.f;          // 좌표계 원점 Y (px) — Align이 설정
    std::vector<std::string> channelRoles;
    std::vector<float> data;        // planar, NaN = 무효

    float zMm(int col, int row) const { return (rawAt(col,row) - zZeroCount) * zResMm; }
    float xMm(int col) const { return (col - originCol) * xResMm; }
    float yMm(int row) const { return (row - originRow) * yResMm; }
};
using Image = HeightMap;   // B1 별칭
```

### 1.2 `VisionData` — 타입별 고정 슬롯 1개씩

`Core/include/VisionData.h`
```cpp
struct PointCloud3D { std::vector<Point3f> points; std::string frameId; };  // ← frameId 있으나 미사용
struct PlaneModel   { double a, b, c; bool valid; };                        // ← 프레임 정보 없음
struct RefPoint     { double cx, cy;  double cxMm, cyMm; double angleDeg; };// ← px+mm 이중
struct OriginCoord  { double xPx, yPx; double xMm, yMm; };                  // ← px+mm 이중

struct VisionData {
    std::shared_ptr<PointCloud3D> cloud;
    std::shared_ptr<HeightMap>    heightmap;
    std::shared_ptr<Region>       region;      // ← 1개만
    std::shared_ptr<PlaneModel>   plane;       // ← 1개만
    std::shared_ptr<std::vector<double>>   heights;  // 배열이지만 특수 슬롯
    std::shared_ptr<std::vector<RefPoint>> points;   // 배열이지만 특수 슬롯
    std::shared_ptr<OriginCoord>  origin;
    std::shared_ptr<std::vector<std::pair<std::string, HeightMapPtr>>> stages;
    std::string sourceId;
    int64_t     timestampUs;
};
```

### 1.3 `Region` — 좌표 정보 전무

`Core/include/Region.h`
```cpp
struct Region {
    int width = 0, height = 0;
    std::vector<uint8_t> mask;   // [row*width+col], 1=내부
};
```

### 1.4 실행 모델 — `runPipeline`

`VisionEngine/src/main.cpp` L94~

```cpp
struct Edge { std::string source, target; };   // L56 — sourceHandle/targetHandle 없음(!)

static std::vector<std::string> topoSort(...);  // L58, Kahn

// L142
auto order = topoSort(ids, edges);
for (const auto& nodeId : order) {
    // 캐시: paramHash + 상류 dirty
    if (useCache && !upstreamDirty && nodeId != forceNode) {
        auto cit = g_nodeCache.find(nodeId);
        if (cit != ... && cit->second.paramHash == ph && cit->second.output) {
            outputs[nodeId] = cit->second.output;   // ← 노드 실행 자체를 건너뜀
            dirty[nodeId] = false;
            continue;
        }
    }
    auto tool = ToolFactory::create(ns.type, ns.params, noPreview);

    // 여러 입력 엣지를 슬롯별로 병합 — "먼저 온 것이 이김"
    for (const auto& src : inputsFrom.at(nodeId)) {
        if (o->heightmap && !merged->heightmap) merged->heightmap = o->heightmap;
        if (o->region    && !merged->region)    merged->region    = o->region;
        if (o->plane     && !merged->plane)     merged->plane     = o->plane;
        if (o->heights) { /* 이어붙이기(concat) */ }
        if (o->points)  { /* 이어붙이기(concat) */ }
    }
    auto result = tool->execute(inputData);   // L227
    outputs[nodeId] = result.output;
    g_nodeCache[nodeId] = { result.output, ph };
}
```

**여기서 도출되는 제약 4개 — 설계에 직접 영향**

| # | 사실 | 영향 |
|---|---|---|
| C-1 | `struct Edge`가 **포트 핸들을 버린다.** UI(`NodeCanvas.tsx` L48-52)는 핸들 인덱스로 타입 검증하지만 엔진에 전달되지 않음 | 입력 병합이 **타입 슬롯 기반**. 같은 타입 입력 2개를 구분 못 함. `Region[]`을 특정 포트로 라우팅하려면 **Edge에 핸들 추가가 선행 필수** → T0-2의 blocker |
| C-2 | 병합이 "먼저 온 것이 이김" | 다중 입력 순서가 결과를 좌우. `inputsFrom`의 순서 = 엣지 파싱 순서 |
| C-3 | `heights`/`points`는 concat | **임시방편 컬렉션 의미가 이미 존재.** T0-2가 이걸 일반화하는 것 |
| C-4 | 캐시 적중 시 **노드 실행을 건너뛴다** | 노드가 부수효과로 프레임을 정의하면 캐시 적중 시 정의가 누락됨 → §3.4에서 처리 |

### 1.5 검증 도구

`main.cpp` L429 `repeatAnalyze(recipePath, folder, outCsv)`, L694 `--repeat-analyze <recipe> <folder> <out.csv>`.
`runPipeline(msg, nullptr)`로 헤드리스 실행(conn 널). **회귀검증의 근간이므로 이 경로를 깨지 말 것.**

---

## 2. T0-1 — 해결할 결함

각 항목은 코드 근거가 있다. 추상적 개선이 아니다.

### 결함 1 — 프레임 정체성 미표현
`PointCloud3D::frameId` 문자열이 존재하지만 **아무도 쓰지 않는다.** 레지스트리가 없어 변환 조합이 불가능하다.

### 결함 2 — `Region`에 프레임·분해능 없음
`Region`은 `width/height/mask`만 든다. HeightMap A에서 만든 Region을 분해능이 다른 B에 적용해도 검증 수단이 없다. `ReduceDomainTool`이 현재 그 상태다 — width/height만 같으면 통과한다.

### 결함 3 — `PlaneModel`이 프레임에 안 묶임 (무성 오류)
`PlaneModel{a,b,c}`는 `z = a·x + b·y + c`인데 **어느 프레임의 x,y인지 기록이 없다.** PlaneFit 후 Align이 `originCol/originRow`를 바꾸면 평면식이 틀어진다:

```
원점이 (Δx, Δy) px 이동하면, 올바른 c는
c_new = c_old + a·(Δx · xResMm) + b·(Δy · yResMm)
```

에러 없이 측정값만 조용히 틀린다. **커밋 `3575c7d` "좌표 원점 일관성"이 이 부류다.** 한 번 고쳐도 노드를 추가할 때마다 재발한다 — 규약이 없어서.

### 결함 4 — px/mm 이중 보유
`RefPoint`가 `cx,cy`(px)와 `cxMm,cyMm`(mm)을 둘 다 든다. `OriginCoord`도 같다. 단일 진실 원천이 없어 한쪽만 갱신하면 desync한다.

### 결함 5 — 회전 없음
`originCol/originRow`는 **평행이동 전용**이다. `RefPoint::angleDeg`가 계산되지만 좌표계에 반영되지 않는다. T3-1 Fixture는 회전이 필수다.

---

## 3. T0-1 설계

### 3.1 핵심 결정 — 2계층 분리

**px↔mm은 HeightMap에 남기고, mm↔mm rigid 변환만 프레임이 담당한다.**

```
픽셀 (col,row)
  │  HeightMap::xMm()/yMm()/zMm()  — 분해능·원점·zZeroCount. 기존 코드 그대로.
  ▼
로컬 mm  (그 HeightMap의 프레임)
  │  FrameRegistry::transform(from, to)  — rigid만
  ▼
다른 프레임의 mm  (world / fixture / object)
```

이렇게 나누면 **Phase 1에서 기존 코드를 한 줄도 안 고치고** 프레임 인프라를 도입할 수 있다. 변경 위험이 가장 낮은 경로다.

### 3.2 신규 파일 — `Core/include/Frame.h`

```cpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>

namespace vision {

// ─────────────────────────────────────────────────────────────────
//  Transform2D — rigid 변환 (회전 + 평행이동). scale/shear 없음.
//    p_to = R(angleDeg) * p_from + (tx, ty)
//  Z는 오프셋만 (레이저 삼각측량 높이맵은 Z축 회전만 의미가 있다).
//  rigid로 제한하는 이유: 거리가 보존되어 mm 측정값이 프레임을 옮겨도 유효.
//  (Open eVision이 world→ZMap 변환에 문서로 못박은 것과 같은 제약)
// ─────────────────────────────────────────────────────────────────
struct Transform2D {
    double angleDeg = 0.0;
    double tx = 0.0, ty = 0.0, tz = 0.0;

    static Transform2D identity() { return {}; }
    bool isIdentity() const {
        return angleDeg == 0.0 && tx == 0.0 && ty == 0.0 && tz == 0.0;
    }

    void apply(double& x, double& y) const {
        if (angleDeg == 0.0) { x += tx; y += ty; return; }   // 정확성 위해 특수화
        const double r = angleDeg * 3.14159265358979323846 / 180.0;
        const double c = std::cos(r), s = std::sin(r);
        const double nx = c * x - s * y + tx;
        const double ny = s * x + c * y + ty;
        x = nx; y = ny;
    }
    double applyZ(double z) const { return z + tz; }

    Transform2D inverse() const;
    // this 적용 후 next 적용에 해당하는 합성 변환
    Transform2D then(const Transform2D& next) const;
};

// ─────────────────────────────────────────────────────────────────
//  Frame — 부모 프레임으로의 rigid 변환 + 부모 링크
//  parentId 빈 문자열 = 루트.  루트 관례 id = "world".
// ─────────────────────────────────────────────────────────────────
struct Frame {
    std::string id;
    std::string parentId;      // "" = root
    Transform2D toParent;      // 이 프레임 좌표 → 부모 좌표
};

// ─────────────────────────────────────────────────────────────────
//  FrameRegistry — 실행 1회분의 프레임 트리.
//  데이터에는 라벨(frameId)만 두고 트리는 여기 한 곳에만 둔다.
// ─────────────────────────────────────────────────────────────────
class FrameRegistry {
public:
    void define(const Frame& f);                  // 같은 id = 덮어쓰기
    bool exists(const std::string& id) const;
    const Frame* get(const std::string& id) const;

    // from → to 변환. 공통 조상까지 올라가 합성. 실패 시 false (연결 없음/미정의).
    bool transform(const std::string& from,
                   const std::string& to,
                   Transform2D& out) const;

    // 두 프레임이 같은 트리에 있고 변환 가능한가 (경계 검사용).
    bool compatible(const std::string& a, const std::string& b) const;

    void clear();
    std::vector<std::string> ids() const;   // 디버그·이벤트 출력용

private:
    std::unordered_map<std::string, Frame> m_frames;
};

// 관례 상수 — 문자열 리터럴 산포 방지
namespace frames {
    inline constexpr const char* kWorld = "world";
    inline constexpr const char* kUnset = "";     // 미지정 = 검사 생략(Phase 1~3 호환)
}

} // namespace vision
```

**`transform()` 구현 요건**

1. `from == to` → identity, true.
2. 각각 루트까지 조상 경로를 구한다.
3. 공통 조상을 찾는다. 없으면 false.
4. `from`→공통조상 = `toParent` 순차 합성. `to`→공통조상의 역변환을 뒤에 붙인다.
5. **사이클 방어**: 조상 추적 시 방문 집합으로 검사, 사이클이면 false. `define()`에서도 자기 조상을 부모로 지정하면 거부.

### 3.3 타입별 프레임 필드 추가

```cpp
// HeightMap.h
struct HeightMap {
    // ... 기존 필드 유지 ...
    std::string frameId;      // 신규. 빈 문자열 = 미지정(검사 생략)
};

// Region.h
struct Region {
    int width = 0, height = 0;
    std::vector<uint8_t> mask;
    std::string frameId;      // 신규 — 결함 2
};

// VisionData.h
struct PlaneModel {
    double a = 0, b = 0, c = 0;
    bool valid = false;
    std::string frameId;      // 신규 — 결함 3. a,b,c가 유효한 프레임
    // signedDistance/tiltDeg 기존 그대로
};

struct PointCloud3D {
    std::vector<Point3f> points;
    std::string frameId;      // 기존 필드 — 이제 실제로 사용
};
```

**`RefPoint`/`OriginCoord`의 px/mm 이중 보유(결함 4)는 Phase 3에서 처리한다.** Phase 1~2에 섞으면 `->points` 27개 호출처가 동시에 흔들려 회귀 원인 추적이 어려워진다.

### 3.4 레지스트리 배치 — 캐시와의 상호작용 (중요)

`IAlgorithmTool::execute(VisionDataPtr)`는 컨텍스트 인자가 없다. 인터페이스를 바꾸면 모든 툴이 영향을 받으므로 **`VisionData`에 공유 레지스트리 포인터를 싣는다.**

```cpp
struct VisionData {
    // ... 기존 ...
    std::shared_ptr<FrameRegistry> frames;   // 실행 1회분, 모든 노드가 공유
    std::vector<Frame> definedFrames;        // 이 노드가 정의한 프레임 (캐시 복원용)
};
```

**`definedFrames`가 왜 필요한가 (제약 C-4).**
캐시 적중 시 `runPipeline`은 노드 실행을 건너뛰고 `outputs[nodeId] = cached.output`만 한다. 프레임 정의가 노드 실행의 부수효과라면 **캐시 적중 시 프레임이 정의되지 않아** 하류 노드가 변환을 못 찾는다.

그래서 규칙:
- 프레임을 만드는 노드는 `output->definedFrames`에 그 프레임을 **반드시 기록**한다.
- `runPipeline`은 캐시 적중 경로에서도 `cached.output->definedFrames`를 현재 실행의 레지스트리에 `define()` 한다.

```cpp
// main.cpp 캐시 적중 분기에 추가
if (cit != g_nodeCache.end() && cit->second.paramHash == ph && cit->second.output) {
    outputs[nodeId] = cit->second.output;
    for (const auto& f : cit->second.output->definedFrames)   // ← 추가
        runFrames->define(f);
    dirty[nodeId] = false;
    continue;
}
```

**입력 병합에도 전파한다** (`merged->frames = runFrames;`). 병합 루프에서 `frames`는 슬롯 병합 대상이 아니라 항상 실행 레지스트리를 가리킨다.

### 3.5 규약 6개

| # | 규약 | 근거 |
|---|---|---|
| 1 | **rigid만** (회전+평행이동, Z오프셋). scale·shear 금지 | 거리 보존 → mm 측정값이 프레임 이동에도 유효. eVision이 문서로 규정한 제약 |
| 2 | 모든 iconic·geometry 타입이 `frameId` 보유 | 결함 2·3 차단 |
| 3 | **레지스트리는 실행 컨텍스트에, 데이터엔 라벨만** | 데이터가 트리를 복사하지 않음. 변환 합성이 한 곳 |
| 4 | `transform(from,to)`는 공통 조상 합성 | N×N 변환 테이블 방지 |
| 5 | **단일 표현.** px 또는 mm 하나만 저장, 변환은 요청 시 | 결함 4 |
| 6 | **노드 경계에서 프레임 계약 검사.** 불일치 = 명시적 실패 | 무성 오류를 관측 가능한 실패로 전환 |
| 7 | `frameId == ""`은 "미지정 = 검사 생략" | Phase 1~3 무중단 이행 |

### 3.6 Phase 계획

#### Phase 1 — 인프라 도입 (동작 변화 0)

**변경**
1. `Core/include/Frame.h` 신규 작성 (§3.2).
2. `Core/CMakeLists.txt`에 필요 시 헤더 등록 (헤더 온리면 불필요).
3. `HeightMap`/`Region`/`PlaneModel`에 `std::string frameId` 추가. **기본값 빈 문자열.**
4. `VisionData`에 `frames`, `definedFrames` 추가.
5. `runPipeline`에서 실행 시작 시 `auto runFrames = std::make_shared<FrameRegistry>();` 생성, `world` 루트 정의, 병합 시 전파, 캐시 적중 시 `definedFrames` 복원(§3.4).
6. `Tests/`에 `FrameRegistry` 단위 테스트 추가 — identity, 부모-자식 1단, 2단 합성, 형제 간 변환, 사이클 거부, 미정의 id 실패, `inverse()`·`then()` 왕복.

**금지**
- 어떤 툴도 `frameId`를 읽거나 쓰지 않는다.
- `originCol/originRow` 의미를 건드리지 않는다.
- 경계 검사를 켜지 않는다.

**수용 기준**
- 빌드 성공, 신규 단위 테스트 통과.
- §6 회귀검증 **CSV diff 0** (bit-identical).

#### Phase 2 — 프레임 부여 + 경고 수준 검사

**변경**
1. `HeightMapLoader`가 출력 HeightMap에 프레임을 정의·부여: id는 `"hm:" + nodeId` 같은 결정론적 규칙. `parentId = "world"`, `toParent = identity`. `definedFrames`에 기록.
2. Region 생산자(`ThresholdTool`, `CreateRoiTool`)가 **입력 HeightMap의 `frameId`를 그대로 복사**한다.
3. `PlaneFitTool`이 출력 `PlaneModel::frameId`에 입력 HeightMap의 프레임을 기록한다.
4. `HeightMapToCloud`/`ExposureMergeCloud`가 `PointCloud3D::frameId`를 채운다.
5. 소비자(`ReduceDomainTool`, `RegionMeasureTool`, `HeightFromPlaneTool`)가 프레임 불일치를 검사한다. **단 Phase 2에서는 경고 로그만** — `ToolStatus`는 바꾸지 않는다.
   - 양쪽 중 하나가 `""`이면 검사 생략.
6. HeightMap을 변형하는 필터(`NoiseFilter`, `GapFill`, `RowStretch`, `ExposureMerge*`)는 **입력 `frameId`를 출력에 그대로 전달**한다. 격자가 안 바뀌므로 새 프레임이 아니다.
   - 예외: `halfRes` 등으로 분해능이 바뀌는 경로가 있으면 **새 프레임을 정의**하고 부모를 입력 프레임으로 둔다. `ExposureMerge2/3`의 `halfRes: true`가 이 경우인지 코드로 확인할 것.

**수용 기준**
- 회귀검증 CSV diff 0.
- 기존 레시피 실행 시 프레임 경고가 **0건**. 경고가 나오면 그 자체가 실제 버그 발견이므로 원인을 기록하고 개별 판단한다.

#### Phase 3 — 회전 도입 + Align 전환 + px/mm 단일화

**여기서 처음으로 동작이 바뀐다. 가장 위험한 Phase다.**

**변경**
1. `AlignTool`이 `originCol/originRow`를 쓰는 대신 **자식 프레임을 정의**한다. 검출 기준점과 `angleDeg`로 `toParent`를 구성하고, 출력 HeightMap의 `frameId`를 그 자식 프레임으로 바꾼다. 데이터(`data`)는 건드리지 않는다.
2. `RefPoint`/`OriginCoord`에서 **mm 필드를 단일 진실 원천으로** 삼고 px 필드는 파생 접근자로 바꾼다(또는 반대 — HeightMap을 알아야 변환되므로 **px 저장 + mm은 접근자**가 더 자연스럽다. 구현 시 호출처를 보고 결정하고 결정 이유를 이 문서에 追記).
3. `PlaneModel` 소비 시 프레임이 다르면 **변환해서 쓴다.** 평면 변환식:
   ```
   프레임 F1의 평면 z = a·x + b·y + c 를 F2로 옮길 때
   (x,y)_F1 = T_{F2→F1}(x,y)_F2 를 대입해 전개하고 tz를 반영한다.
   회전이 있으면 (a,b)가 회전하고 c가 평행이동분만큼 이동한다.
   → 단위 테스트로 왕복 검증 필수 (F1→F2→F1 = 원본).
   ```

**수용 기준**
- `Align`을 쓰지 않는 레시피: CSV diff 0.
- `Align`을 쓰는 레시피: **값이 바뀌는 것이 정상이다** (기존이 회전을 무시했으므로). 이 경우 diff 0을 요구하지 말고, 대신
  - 회전각 0인 케이스에서 기존과 일치함을 확인,
  - 회전각이 있는 케이스는 합성 데이터로 정답을 계산해 대조,
  - 반복성(σ)이 기존보다 나빠지지 않았음을 `--repeat-analyze`로 확인.
- 평면 프레임 변환 왕복 테스트 통과.

#### Phase 4 — 경계 검사 강제

**변경**
1. Phase 2에서 경고였던 프레임 불일치를 `ToolStatus::Fail` + 메시지로 승격.
2. `frameId == ""`(미지정) 허용을 유지할지 결정. 권장: 유지하되 경고 1회 출력.

**수용 기준**
- 기존 레시피 전부 정상 실행. 실패가 나면 그건 잡아낸 실제 버그다.

---

## 4. T0-2 설계

### 4.1 해결할 결함

1. **객체 수가 런타임 결정.** ConnectedComponents(T2-1)가 Region N개를 낸다. 현재 표현 수단이 없다.
2. **fan-out/fan-in 불가.** 필요 형태: 입력 1 → 원소 N → 각각 측정 → 결과 N 수집.
3. **실행 모델 문제.** topoSort가 노드당 1회만 실행한다. 서브그래프 N회 실행은 `runPipeline` 변경이다.

### 4.2 선택지와 결정

| | A. 암묵적 벡터화 (Aurora 방식) | B. 명시적 ForEach + 서브그래프 (Gocator Array tools / VisionPro 방식) |
|---|---|---|
| 방식 | 배열을 스칼라 자리에 넣으면 엔진이 원소별로 노드 반복 실행 | ForEach 노드가 서브그래프를 N회 실행 |
| 장점 | 그래프 구조 변경 0. 서브그래프 실행 엔진 불필요 | 그래프에 명시적. **T0-6 중첩 ToolBlock과 동일한 기계장치** |
| 단점 | 타입 시스템이 배열 깊이를 추적해야 함. 중첩 배열 어색 | `runPipeline`에 서브그래프 실행 필요. UI에 중첩 필요 |

**결정: A를 먼저 구현하고, B는 T0-6과 함께 한 번에.**

근거: A만으로 `ConnectedComponents → 원소별 측정 → 수집`이 실행 엔진 변경 없이 동작한다. B의 서브그래프 실행은 T0-6과 같은 기계장치이므로 **따로 두 번 만들지 않는다.**

### 4.3 선행 blocker — `Edge`에 포트 핸들 추가

제약 C-1 때문에 **T0-2 어떤 작업보다 이것이 먼저다.**

```cpp
// main.cpp L56
struct Edge {
    std::string source, target;
    int sourcePort = 0;      // 신규 — sourceHandle "output-N"의 N
    int targetPort = 0;      // 신규 — targetHandle "input-N"의 N
};
```

- L132 엣지 파싱에서 `sourceHandle`/`targetHandle` 문자열을 파싱해 채운다. UI가 이미 `output-N`/`input-N` 형식을 쓴다(`NodeCanvas.tsx` L48-49).
- 없거나 파싱 실패 시 0으로 폴백 → **기존 동작 보존**.
- `inputsFrom`을 `(src, srcPort, dstPort)` 튜플로 확장.
- 입력 병합을 **포트 인덱스 기준**으로 바꾼다. 단 **Phase 1에서는 기존 슬롯 병합 결과와 동일해야 한다** — 즉 포트 정보를 채우기만 하고 병합 로직 전환은 검증 후.

이 변경만으로도 얻는 것: `ReduceDomain(HeightMap, Region)`처럼 타입이 다른 경우에 우연히 동작하던 것이 **의도적으로 동작**하게 되고, 같은 타입 입력 2개(예: 두 표면 차분 B3)가 가능해진다.

### 4.4 타입 설계

**포트 다중성 플래그**
```ts
// ui/src/renderer/src/types/tools.ts
export type PortSpec = { type: PortType; isArray?: boolean }
// 기존 inputs: PortType[] 을 PortSpec[] 로. 문자열은 { type, isArray:false }로 정규화하는
// 헬퍼를 두어 기존 노드 정의를 그대로 유지한다.
```

**연결 검증** (`NodeCanvas.tsx` `isCompatible`)
```
스칼라 → 스칼라   허용
배열   → 배열     허용
스칼라 → 배열     허용 (원소 1개로 승격)
배열   → 스칼라   허용 (브로드캐스트: 엔진이 원소별 반복 실행)
```
`Any`는 기존 규칙 유지.

**브로드캐스트 규칙 (엔진)**
```
노드의 배열 입력 중 최대 길이 N
  N == 1 또는 배열 입력 없음  →  1회 실행 (현재와 동일)
  N >  1                      →  N회 실행. 스칼라 입력은 매 반복에 동일값 사용.
                                 길이가 1인 배열도 동일값으로 확장.
                                 길이가 서로 다르고 1도 아니면  →  Fail (명시적 에러)
출력 다중성 = N
```

**`VisionData` 슬롯 다중화**

`region`을 벡터로 바꾸고 편의 접근자를 둔다. 필드→메서드 전환으로 컴파일 에러가 호출처를 전수 알려준다 (`->region` 6곳).

```cpp
struct VisionData {
    std::vector<std::shared_ptr<Region>> regions;    // 스칼라 = size 1

    // 호환 접근자 — 첫 원소 또는 널
    const std::shared_ptr<Region>& region0() const {
        static const std::shared_ptr<Region> kNull;
        return regions.empty() ? kNull : regions.front();
    }
    bool hasRegion() const { return !regions.empty() && regions.front() && !regions.front()->empty(); }
};
```

같은 방식을 `plane`에도 적용한다(`->plane` 9곳). `heights`/`points`는 이미 벡터이므로 **같은 다중성 메커니즘으로 통합**한다 — 이게 부수 이득이다: `heights`/`points` 특수 슬롯이 사라지면서 **B3(iconic/control 분리)의 절반이 함께 해결된다.**

### 4.5 결정론적 순서 (필수)

**`Region[]`의 원소 순서가 실행마다 바뀌면 안 된다.** CsvWriter 출력 열 순서가 흔들려 회귀검증이 무의미해진다.

- ConnectedComponents 등 컬렉션 생산자는 **명시적 정렬 규칙**을 갖는다. 권장: 첫 픽셀의 raster 순서(row 우선, 그 다음 col).
- 병렬화하더라도 **인덱스로 기록해 직렬과 동일 순서**를 보장한다. `perf_findings.md`의 HeightMeasure ROI 병렬화가 이미 이 방식으로 bit-identical을 달성했다 — 같은 패턴을 따른다.
- 브로드캐스트 반복도 인덱스 순서를 보존한다.

### 4.6 Phase 계획

#### Phase 1 — Edge 포트 핸들 (동작 변화 0)
1. `struct Edge`에 `sourcePort`/`targetPort` 추가, L132 파싱에서 채움. 폴백 0.
2. `inputsFrom`을 포트 정보 포함 구조로 확장.
3. **병합 로직은 그대로 둔다.** 포트 정보를 채우기만 한다.
4. 디버그: 실행 로그에 포트 정보 출력해 UI 핸들과 일치하는지 눈으로 확인.

**수용 기준**: 회귀검증 CSV diff 0.

#### Phase 2 — 포트 기반 병합 전환
1. 병합을 포트 인덱스 기준으로 바꾼다.
2. 기존 특수 동작 2개를 **명시적으로 보존**한다 (제약 C-3):
   - 같은 `targetPort`에 여러 엣지가 오면 `heights`/`points`는 concat.
   - 그 외 타입은 첫 번째 우선(기존 동작).
3. `ReduceDomain`·`HeightFromPlane`·`RegionMeasure`처럼 다중 입력 노드의 포트 순서가 `tools.ts`의 `inputs` 순서와 일치하는지 확인. `RegionMeasure`는 `inputs: ['Region','HeightMap']`, `ReduceDomain`은 `['HeightMap','Region']`로 **순서가 서로 반대**다 — 여기가 깨지기 쉬운 지점이다.

**수용 기준**: 회귀검증 CSV diff 0. 다중 입력 노드가 포함된 레시피를 반드시 포함할 것.

#### Phase 3 — 슬롯 다중화 + 브로드캐스트
1. `regions`/`planes` 벡터화 + 호환 접근자. 호출처 전수 수정(컴파일러 안내).
2. `runPipeline`에 브로드캐스트 실행 루프 추가 (§4.4 규칙).
3. `tools.ts`에 `PortSpec` 도입, `NodeCanvas.tsx` 검증 확장.
4. 브로드캐스트 단위 테스트: 배열 없음(1회), 길이 N, 길이 1 확장, 길이 불일치 실패.

**수용 기준**
- 기존 레시피는 전부 배열 없음 경로 → **CSV diff 0**.
- 신규 합성 테스트: 인위적으로 Region 3개를 만들어 원소별 측정이 3개 결과를 내고 순서가 결정론적임을 확인.

#### Phase 4 — 축약 노드 (이후 작업의 진입점)
`Collect`(배열→단일 배열), `Filter`(조건), `Select`(인덱스), 집계. **T2-1/T2-2의 전제**이므로 이 문서 범위에서는 인터페이스만 정의하고 구현은 후속으로 넘긴다.

---

## 5. 상호작용과 순서

### 5.1 T0-1 × T0-2

**ForEach/브로드캐스트가 원소별 로컬 프레임을 만들면 프레임 트리에 반복당 N개 자식이 붙는다** (E3 로컬 base plane 케이스). 그러면 레지스트리에 스코프·생명주기 개념이 필요하다.

이 문서 범위에서의 처리: **원소별 프레임 생성은 하지 않는다.** 배열 원소는 부모 프레임을 공유한다. 원소별 프레임이 필요해지는 시점(T2-3)에 다시 설계한다. 그때 필요한 것:
- 프레임 id에 원소 인덱스 포함 (`obj:<nodeId>:<i>`)
- 실행 종료 시 정리 또는 레지스트리 스코프

### 5.2 왜 T0-1이 먼저인가

후속 항목 전부가 T0-1을 소비한다:

| 항목 | T0-1 의존 |
|---|---|
| T1-1 Profile | 경로가 **어느 프레임의** 경로인가 |
| T2-1 Region[] | 원소마다 프레임 필요 |
| T2-2 메트릭 필터 | mm 단위 필터링 = px↔mm 변환이 전제 |
| T2-3 로컬 base plane | 객체별 로컬 프레임 |
| T3-1 Fixture | **Fixture는 곧 프레임 생성기.** T0-1 Phase 3이 그 진입점 |

나중에 넣으면 이 다섯을 전부 다시 쓴다.

### 5.3 T0-2가 Tier 0인데 §5 권장순서에서 5번째인 이유

모순이 아니다.

- T0-2가 **막는 것**: T2-1, T2-2, E4
- T0-2가 **안 막는 것**: T1 캘리퍼 전체 — 단일 프로파일·단일 측정은 컬렉션이 필요 없다

그래서 T1(측정 본체)을 먼저 세우고 T0-2를 뒤에 둔다. T0-1은 반대로 **전부를 막는다.**

**단 §4.3 Edge 포트 핸들은 예외다.** 이건 T0-2 본체와 분리해 **지금 바로** 해도 이득이고(다중 입력이 의도적으로 동작), 위험이 거의 없다.

---

## 6. 회귀검증 절차

**모든 Phase 완료 시 실행. 통과 못 하면 되돌린다.**

```bash
# 1) 빌드 (WORKTREE-SETUP.md 절차 준수)
cmake --build build --config Release --target VisionEngine

# 2) 단위 테스트
ctest --test-dir build -C Release --output-on-failure

# 3) 기준선 생성 — 변경 전 커밋에서 1회
build/bin/Release/VisionEngine.exe --repeat-analyze <recipe.json> <folder> baseline.csv

# 4) 변경 후
build/bin/Release/VisionEngine.exe --repeat-analyze <recipe.json> <folder> after.csv

# 5) 대조 — 반드시 완전 동일
diff baseline.csv after.csv
```

**검증 레시피 (findings 문서 기준)**

| 레시피 | 데이터 | 확인 대상 |
|---|---|---|
| `recipe_top_merged.json` | `D:\Feasibility Study\260511_SDC\0710` merged 100장 | 주 경로. `repeatability_findings.md`의 avg σ 0.00147 / worst range 0.00788 재현 |
| `recipe_top` (저노출 분리) | `top_100_zmap/test` 원본 100장 | raw 경로. avg σ 0.00149 |
| `merge.json` | 동일 | ExposureMerge2 strict (tolX5/tolY30/gapK0) 경로 |

**다중 입력 노드가 포함된 레시피를 반드시 하나 포함할 것** (T0-2 Phase 2 검증용). `ReduceDomain`·`RegionMeasure`·`HeightFromPlane` 중 하나 이상.

**σ 지표 확인**: CSV diff가 0이 아닌 정당한 경우(T0-1 Phase 3의 Align 경로)에는 avg σ와 worst range가 **악화되지 않았음**을 확인한다.

---

## 7. 금지 사항

작업 중 건드리지 말 것. 건드려야 한다고 판단되면 먼저 보고할 것.

1. **`IAlgorithmTool::execute(VisionDataPtr)` 시그니처 변경 금지.** 컨텍스트는 `VisionData`에 실어 전달한다(§3.4).
2. **`--repeat-analyze` 경로 파괴 금지.** `runPipeline(msg, nullptr)` 헤드리스 실행이 회귀검증의 근간이다.
3. **`zZeroCount`·`xResMm`·`yResMm`·`zResMm` 의미 변경 금지.** px↔mm은 HeightMap에 남긴다(§3.1).
4. **`ExposureMerge*` 알고리즘 파라미터 기본값 변경 금지.** `repeatability_findings.md`가 코드 기본값(tolX10/tolY100/gapK2)은 다른 용도로 유지하기로 결정했다.
5. **`NoiseFilter` 경계 처리 변경 금지.** 커밋 `63a8641`·`71b8320`에서 조정된 부분이다.
6. **성능 회귀 금지.** `perf_findings.md` 기준: HeightMeasure 78ms, 전체 대표 레시피. 프레임 변환을 픽셀 루프 안에서 호출하지 말 것 — **루프 밖에서 변환을 1회 계산해 상수로 쓴다.**
7. **한글/비-ASCII 경로 처리 파괴 금지.** 커밋 `dad5b7a`에서 고친 부분이다.
8. **자동 커밋 금지.** 각 Phase 완료 후 diff를 보고하고 승인받는다.

---

## 8. 작업 체크리스트

순서대로. 각 항목 완료 시 §6 회귀검증.

### T0-1
- [ ] **1-1** `Core/include/Frame.h` 신규 — `Transform2D`, `Frame`, `FrameRegistry`, `frames::kWorld`
- [ ] **1-2** `FrameRegistry` 단위 테스트 — identity / 1단 / 2단 합성 / 형제 / 사이클 거부 / 미정의 실패 / `inverse()`·`then()` 왕복
- [ ] **1-3** `HeightMap`·`Region`·`PlaneModel`에 `frameId` 추가 (기본 `""`)
- [ ] **1-4** `VisionData`에 `frames`·`definedFrames` 추가
- [ ] **1-5** `runPipeline`: 레지스트리 생성 + `world` 정의 + 병합 전파 + **캐시 적중 시 `definedFrames` 복원**
- [ ] **[검증]** CSV diff 0
- [ ] **2-1** `HeightMapLoader`가 프레임 정의·부여 (`hm:<nodeId>`, parent=`world`, identity)
- [ ] **2-2** Region 생산자(`Threshold`, `CreateROI`)가 입력 `frameId` 복사
- [ ] **2-3** `PlaneFitTool`이 `PlaneModel::frameId` 기록
- [ ] **2-4** `HeightMapToCloud`·`ExposureMergeCloud`가 `PointCloud3D::frameId` 채움
- [ ] **2-5** HeightMap 필터들이 `frameId` 전달. **`halfRes`로 분해능이 바뀌는 경로는 새 프레임** — 코드로 확인
- [ ] **2-6** 소비자 3개(`ReduceDomain`, `RegionMeasure`, `HeightFromPlane`)에 불일치 **경고** 추가. `""`은 생략
- [ ] **[검증]** CSV diff 0 + 기존 레시피에서 프레임 경고 0건
- [ ] **3-1** `AlignTool`을 프레임 생성 방식으로 전환 (회전 포함, 데이터 무변형)
- [ ] **3-2** `RefPoint`·`OriginCoord` px/mm 단일화 (결정 이유를 이 문서에 追記)
- [ ] **3-3** 평면 프레임 변환 구현 + **왕복 단위 테스트**
- [ ] **[검증]** Align 미사용 레시피 diff 0 / Align 사용 레시피는 §3.6 Phase 3 기준
- [ ] **4-1** 프레임 불일치를 `ToolStatus::Fail`로 승격
- [ ] **[검증]** 기존 레시피 전부 정상 실행

### T0-2
- [ ] **B-1** `struct Edge`에 `sourcePort`/`targetPort` + L132 파싱 (폴백 0). `inputsFrom` 확장
- [ ] **[검증]** CSV diff 0
- [ ] **B-2** 병합을 포트 기준으로 전환. `heights`/`points` concat 동작 명시 보존. **`RegionMeasure`(Region,HeightMap) vs `ReduceDomain`(HeightMap,Region) 순서 반대 주의**
- [ ] **[검증]** CSV diff 0 (다중 입력 노드 레시피 포함 필수)
- [ ] **B-3** `regions`·`planes` 벡터화 + 호환 접근자. 호출처 전수 수정
- [ ] **B-4** `runPipeline` 브로드캐스트 루프 (§4.4 규칙, 길이 불일치 = Fail)
- [ ] **B-5** `tools.ts` `PortSpec` + `NodeCanvas.tsx` 검증 확장
- [ ] **B-6** 브로드캐스트 단위 테스트 4종 + **결정론적 순서** 테스트
- [ ] **[검증]** 기존 레시피 diff 0 + 신규 합성 테스트(Region 3개 → 결과 3개, 순서 고정)
- [ ] **B-7** 축약 노드 인터페이스 정의만 (`Collect`/`Filter`/`Select`) — 구현은 후속

---

## 9. 미결정 사항

구현 중 결정하고 이 문서에 追記할 것.

1. **`RefPoint` 단일 표현을 px로 할지 mm로 할지.** px 저장 + mm 접근자가 자연스러워 보이나(변환에 HeightMap이 필요), 호출처 27곳을 보고 판단.
2. **`ExposureMerge2/3`의 `halfRes: true`가 실제로 분해능을 바꾸는가.** 바꾸면 새 프레임이 필요하다. 코드 확인 필요.
3. **`frameId == ""` 허용을 영구 유지할지.** Phase 4에서 결정.
4. **프레임 id 명명 규칙 확정.** 초안: `world` / `hm:<nodeId>` / `fx:<nodeId>` / `obj:<nodeId>:<i>`.
5. **`Image2D`의 프레임 취급.** B1 통합(T0-3)과 함께 결정하는 게 맞을 수 있다.

---

## 10. 구현 결정 로그 (2026-07-30)

구현 중 내린 결정. 이후 Phase가 근거로 삼을 것.

### 완료 Phase (전부 CSV diff 0 검증 — `_reg_merged.json` × `top_100_zmap/merge_test` 100장)

- **T0-2 P1 (Edge 포트 핸들).** `struct Edge`에 `sourcePort`/`targetPort`, `parsePortHandle()`가 `"output-N"`/`"input-N"`의 N을 파싱(폴백 0). `inputsFrom`을 `InputRef{source,srcPort,dstPort}`로 확장. 병합 로직 불변. 디버그 로그로 엣지 포트 출력.
- **T0-1 P2 (프레임 부여+경고).** **중앙집중 방식 채택** — 설계는 툴별 부여를 제안했으나, `execute(VisionDataPtr)` 시그니처 불변 원칙(§7-1) 하에 10개 툴 파일을 건드리는 대신 `runPipeline` 한 곳에서 처리. HeightMapLoader 출력에 `"hm:"+nodeId` 프레임 정의·`definedFrames` 기록, 그 외 노드는 입력 HeightMap의 `frameId`를 출력 iconic/geometry에 전파(비어있을 때만). 소비자 불일치는 `VISION_LOG_WARN`(헤드리스에서도 보임). frameId는 순수 메타데이터라 측정값 bit-identical. **근거:** 단일 지점 = 회귀 추적 쉽고 캐시 상호작용(§3.4)을 한 곳에서 보장. AlignTool이 P3에서 자기 프레임을 명시 지정하면 "비어있을 때만" 규칙이 이를 덮어쓰지 않음 — P3와 호환.
- **T0-2 P2 (포트 기반 병합).** 입력을 `(dstPort 오름차순, 엣지순)` stable-sort 후 병합. heights/points concat·그 외 첫우선 명시 보존. 타입 슬롯 라우팅이라 RegionMeasure(Region,HeightMap) vs ReduceDomain(HeightMap,Region) 순서 반대여도 무관.
- **T0-2 P3 (슬롯 다중화+브로드캐스트).** `VisionData::region`/`plane` → `regions`/`planes` 벡터 + `region0()`/`setRegion()`/`plane0()`/`setPlane()` 호환 접근자. 호출처 전수 수정(VisionTools 6, main.cpp, vision_sdk.cpp). 브로드캐스트 규칙(§4.4)은 `Core/include/Broadcast.h`의 순수 함수 `computeBroadcast()` + 단위 테스트 7종으로 고정. UI: `PortSpec`/`PortDecl` + `portType()`/`portIsArray()`, NodeCanvas `isCompatible`가 스칼라↔배열 4조합 허용, ToolNode가 배열 포트에 `[]` 표시.
- **T0-2 P4 (축약 노드).** `PLANNED_REDUCTION_NODES`(Collect/Filter/Select) 인터페이스만 정의. TOOL_DEFS 미등록(실행 불가). 구현은 T2-1/T2-2.

### 미결선(P3 보류로 이월)

- **브로드캐스트 실행 루프 미배선.** 배열을 내는 노드(T2-1 ConnectedComponents)가 없어 현재 N=1 경로만 존재. 실행 루프는 규칙 함수·부착 지점 주석(`main.cpp` tool->execute 직전)을 남겨 T2-1과 함께 연결. 합성 3-Region end-to-end 테스트도 그때.
- **T0-1 P3/P4 보류(사용자 결정).** Align 회전+프레임 전환은 `originCol/originRow`를 소비하는 5개 툴의 ROI 배치 의미를 바꾸는 정밀-경로 변경 → 합성 회전 검증셋 갖춘 전용 세션에서. §9-1(RefPoint px/mm), §9-4(회전각 입력원) 그때 확정.
