# F1 배열모드 브로드캐스트 (첫 슬라이스) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 스칼라 선언 툴이 배열 입력을 받으면 엔진이 원소별 N회 실행·수집하도록 `runPipeline`에 브로드캐스트 루프를 배선하고, `CreateROI(Region[]) → RegionMeasure(스칼라)` fan-out으로 증명한다.

**Architecture:** 브로드캐스트 축 길이 수집·원소 슬라이싱을 Core 순수함수(`BroadcastRun.h`)로 두어 GTest로 단위검증한다. `main.cpp`의 `tool->execute` 단일 호출을 이 순수함수를 쓰는 루프로 감싼다. UI는 `TOOL_DEF_MAP`에서 포트 arity 메타를 뽑아 레시피 노드에 동봉한다. RegionMeasure는 내부 배열 루프를 제거해 스칼라 툴이 된다.

**Tech Stack:** C++17 (Core/VisionTools/VisionEngine, MSVC, vcpkg), GoogleTest, React+TypeScript(electron-vite), nlohmann::json.

## Global Constraints

- 빌드: `cmake --build D:/GitHub/VisionSWTool/wt-refactor/build --config Release` (필요시 `--target CoreTests`/`ToolsTests`/`VisionEngine`).
- 테스트 실행: `ctest --test-dir D:/GitHub/VisionSWTool/wt-refactor/build -C Release` 또는 `build/bin/Release/CoreTests.exe`.
- 회귀 원칙: 기존 레시피는 스칼라 포트에 배열이 없어 N=1 경로 → **CSV diff 0** 유지.
- 결정론(설계 §4.5): 브로드캐스트 반복은 인덱스 순서 보존. 첫 슬라이스는 **순차 실행**.
- 포트 메타 폴백: 노드에 `inputPorts` 없으면 엔진은 전 포트 스칼라로 간주(구 레시피/헤드리스 호환).
- 브로드캐스트 규칙: `computeBroadcast`(기존, `Core/include/Broadcast.h`) — 축 없음/전부1 → N=1, 길이 N>1 → N회, 서로 다른 비-1 길이 → `ok=false`(Fail), 길이 0 → N=0.
- 커밋 메시지 말미: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

## File Structure

- `Core/include/BroadcastRun.h` (신규) — `PortMeta`, `broadcastAxisLengths()`, `sliceBroadcastInput()` 선언.
- `Core/src/BroadcastRun.cpp` (신규) — 위 구현 + 내부 `typedLen`/`sliceTypedTo`.
- `Core/CMakeLists.txt` (수정) — `BroadcastRun.cpp` 소스 추가.
- `Tests/Core/BroadcastRunTest.cpp` (신규) — 축 길이·슬라이싱 단위테스트.
- `Tests/CMakeLists.txt` (수정) — `CoreTests`에 `BroadcastRunTest.cpp` 추가.
- `VisionEngine/src/main.cpp` (수정) — `inputPorts` 파싱 + `tool->execute` 브로드캐스트 루프.
- `VisionTools/src/RegionMeasureTool.cpp` (수정) — 내부 배열 루프 제거, `inRegion(0)` 단일 측정.
- `Tests/Tools/RegionMeasureTest.cpp` (수정) — 다중 Region 시 첫 원소만 측정 확인 테스트 추가.
- `ui/src/renderer/src/recipe.ts` (신규) — `nodeToEnginePayload(n)` 공유 헬퍼(포트 메타 동봉).
- `ui/src/renderer/src/App.tsx` (수정) — 엔진 전송 노드 payload를 헬퍼로 교체(2곳).

---

## Task 1: Core 브로드캐스트 축 길이 + 슬라이싱 순수함수

**Files:**
- Create: `Core/include/BroadcastRun.h`
- Create: `Core/src/BroadcastRun.cpp`
- Modify: `Core/CMakeLists.txt`
- Create: `Tests/Core/BroadcastRunTest.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `vision::VisionData`(`Core/include/VisionData.h` — `inputs`, `regions`, `geometries`, `profiles`, `heightmaps`, `clouds`, `lines`, `in(port)`), `vision::computeBroadcast`(`Core/include/Broadcast.h`).
- Produces:
  - `struct vision::PortMeta { std::string type; bool isArray = false; };`
  - `std::vector<std::size_t> vision::broadcastAxisLengths(const VisionData& in, const std::vector<PortMeta>& metas);`
  - `vision::VisionDataPtr vision::sliceBroadcastInput(const VisionData& in, std::size_t i, const std::vector<PortMeta>& metas);`

- [ ] **Step 1: Write the failing test**

Create `Tests/Core/BroadcastRunTest.cpp`:

```cpp
#include <gtest/gtest.h>
#include "BroadcastRun.h"
#include "VisionData.h"

using namespace vision;

// 포트0에 Region N개를 담은 상류 입력을 만든다.
static VisionDataPtr makeRegionPortInput(int nRegions) {
    auto up = std::make_shared<VisionData>();
    for (int k = 0; k < nRegions; ++k) {
        auto rg = std::make_shared<Region>();
        rg->width = 1; rg->height = 1; rg->mask.assign(1, 1);
        rg->label = std::to_string(k);   // 원소 구분용
        up->regions.push_back(rg);
    }
    auto merged = std::make_shared<VisionData>();
    merged->inputs.push_back(up);        // 포트0 = up
    return merged;
}

TEST(BroadcastRunTest, ScalarPortWithArrayIsAxis) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", false } };  // 스칼라 선언
    auto lens = broadcastAxisLengths(*in, metas);
    ASSERT_EQ(lens.size(), 1u);
    EXPECT_EQ(lens[0], 3u);
}

TEST(BroadcastRunTest, ArrayDeclaredPortIsNotAxis) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", true } };   // 배열 선언 → 통째 소비
    auto lens = broadcastAxisLengths(*in, metas);
    EXPECT_TRUE(lens.empty());
}

TEST(BroadcastRunTest, ScalarSingleElementIsNotAxis) {
    auto in = makeRegionPortInput(1);
    std::vector<PortMeta> metas{ { "Region", false } };
    auto lens = broadcastAxisLengths(*in, metas);
    EXPECT_TRUE(lens.empty());
}

TEST(BroadcastRunTest, SliceKeepsOnlyElementI) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", false } };
    auto s1 = sliceBroadcastInput(*in, 1, metas);
    auto port0 = s1->in(0);
    ASSERT_TRUE(port0);
    ASSERT_EQ(port0->regions.size(), 1u);
    EXPECT_EQ(port0->regions[0]->label, "1");   // 원소 1만 남음
}

TEST(BroadcastRunTest, SliceLeavesArrayPortWhole) {
    auto in = makeRegionPortInput(3);
    std::vector<PortMeta> metas{ { "Region", true } };  // 배열 선언
    auto s0 = sliceBroadcastInput(*in, 0, metas);
    ASSERT_TRUE(s0->in(0));
    EXPECT_EQ(s0->in(0)->regions.size(), 3u);           // 통째 유지
}
```

- [ ] **Step 2: Create the header**

Create `Core/include/BroadcastRun.h`:

```cpp
#pragma once
#include "VisionData.h"
#include <string>
#include <vector>
#include <cstddef>

namespace vision {

// 입력 포트의 선언 타입 + 배열여부. UI 레시피 메타에서 온다.
struct PortMeta {
    std::string type;        // "Region"/"HeightMap"/"Profile"/"Geometry"/"PointCloud3D"/"Line"/그 외
    bool        isArray = false;
};

// 브로드캐스트 축 길이 수집.
//  스칼라 선언(isArray=false) 포트가 받은 (type) 벡터 길이가 >1이면 축으로 채택.
//  배열 선언 포트·길이<=1·매핑 없는 타입은 축 아님.
std::vector<std::size_t> broadcastAxisLengths(
    const VisionData& in, const std::vector<PortMeta>& metas);

// i번째 원소로 슬라이스한 입력을 새로 만든다.
//  축이 된 포트(스칼라선언 + 해당 type 길이>1)만 원소 i 단일로 축소, 나머지는 통째 유지.
VisionDataPtr sliceBroadcastInput(
    const VisionData& in, std::size_t i, const std::vector<PortMeta>& metas);

} // namespace vision
```

- [ ] **Step 3: Create the implementation**

Create `Core/src/BroadcastRun.cpp`:

```cpp
#include "BroadcastRun.h"

namespace vision {

// 포트 데이터에서 type에 해당하는 iconic 벡터의 원소 수. 매핑 없는 타입은 0.
static std::size_t typedLen(const VisionData& d, const std::string& t) {
    if (t == "Region")       return d.regions.size();
    if (t == "Geometry")     return d.geometries.size();
    if (t == "Profile")      return d.profiles.size();
    if (t == "HeightMap")    return d.heightmaps.size();
    if (t == "PointCloud3D") return d.clouds.size();
    if (t == "Line")         return d.lines.size();
    return 0;   // Any/Measurements/Decisions 등은 첫 슬라이스에서 축 아님
}

// 포트 사본에서 type 벡터를 원소 i 하나만 남긴다.
static void sliceTypedTo(VisionData& d, const std::string& t, std::size_t i) {
    if (t == "Region"       && i < d.regions.size())    { auto e = d.regions[i];    d.regions    = { e }; }
    else if (t == "Geometry"     && i < d.geometries.size()) { auto e = d.geometries[i]; d.geometries = { e }; }
    else if (t == "Profile"      && i < d.profiles.size())   { auto e = d.profiles[i];   d.profiles   = { e }; }
    else if (t == "HeightMap"    && i < d.heightmaps.size()) { auto e = d.heightmaps[i]; d.heightmaps = { e }; }
    else if (t == "PointCloud3D" && i < d.clouds.size())     { auto e = d.clouds[i];     d.clouds     = { e }; }
    else if (t == "Line"         && i < d.lines.size())      { auto e = d.lines[i];      d.lines      = { e }; }
}

std::vector<std::size_t> broadcastAxisLengths(
    const VisionData& in, const std::vector<PortMeta>& metas) {
    std::vector<std::size_t> lens;
    for (std::size_t p = 0; p < metas.size(); ++p) {
        if (metas[p].isArray) continue;
        auto port = in.in(p);
        if (!port) continue;
        std::size_t n = typedLen(*port, metas[p].type);
        if (n > 1) lens.push_back(n);
    }
    return lens;
}

VisionDataPtr sliceBroadcastInput(
    const VisionData& in, std::size_t i, const std::vector<PortMeta>& metas) {
    // inputs 벡터(shared_ptr들)는 얕은 복사; 축 포트만 깊은 복사 후 슬라이스.
    auto sliced = std::make_shared<VisionData>(in);
    for (std::size_t p = 0; p < metas.size(); ++p) {
        if (metas[p].isArray) continue;
        auto port = in.in(p);
        if (!port) continue;
        if (typedLen(*port, metas[p].type) <= 1) continue;
        auto pc = std::make_shared<VisionData>(*port);   // 포트 깊은 복사
        sliceTypedTo(*pc, metas[p].type, i);
        if (p < sliced->inputs.size()) sliced->inputs[p] = pc;
    }
    return sliced;
}

} // namespace vision
```

- [ ] **Step 4: Register the source in Core**

`Core/CMakeLists.txt` uses an explicit source list (`add_library(Core STATIC src/VisionData.cpp src/Pipeline.cpp ...)`). Add a new line `    src/BroadcastRun.cpp` inside that list.

- [ ] **Step 5: Register the test**

In `Tests/CMakeLists.txt`, add `Core/BroadcastRunTest.cpp` to the `CoreTests` `add_executable(...)` source list (after `Core/BroadcastTest.cpp`).

- [ ] **Step 6: Build and run the test — verify pass**

Run:
```
cmake --build D:/GitHub/VisionSWTool/wt-refactor/build --config Release --target CoreTests
D:/GitHub/VisionSWTool/wt-refactor/build/bin/Release/CoreTests.exe --gtest_filter=BroadcastRunTest.*
```
Expected: 5 tests PASS.

- [ ] **Step 7: Commit**

```
git add Core/include/BroadcastRun.h Core/src/BroadcastRun.cpp Core/CMakeLists.txt Tests/Core/BroadcastRunTest.cpp Tests/CMakeLists.txt
git commit -m "feat(core): 브로드캐스트 축 길이·슬라이싱 순수함수 + 단위테스트"
```

---

## Task 2: RegionMeasure 스칼라화

**Files:**
- Modify: `VisionTools/src/RegionMeasureTool.cpp:109-143`
- Modify: `ui/src/renderer/src/types/tools.ts:126`
- Modify: `Tests/Tools/RegionMeasureTest.cpp`

**Interfaces:**
- Consumes: `VisionData::inRegion(0)`, `VisionData::inHeightMap(1)`, 기존 `measureOne(rg, map, params, prefix)`(같은 파일 static).
- Produces: 단일 Region 측정 결과(measurements 11종, prefix 없음). 다중 Region 입력 시 **첫 원소만** 측정.

- [ ] **Step 1: Write the failing test**

Add to `Tests/Tools/RegionMeasureTest.cpp` (파일 끝, `makeRectRegion` 헬퍼 재사용):

```cpp
// 스칼라화: Region 2개를 주면 첫 원소(inRegion(0))만 측정한다.
TEST(RegionMeasureTest, MultipleRegionsMeasuresFirstOnly) {
    auto hm = makeUniformHM(4, 1, 0.0, 1.f, 1.f, 0.001f);
    auto input = std::make_shared<VisionData>();
    auto port  = std::make_shared<VisionData>();
    port->regions.push_back(makeRectRegion(4, 1, 0, 0, 2, 1)); // 첫 원소: areaPx=2
    port->regions.push_back(makeRectRegion(4, 1, 0, 0, 4, 1)); // 둘째: areaPx=4
    input->inputs.push_back(port);                              // 포트0 = Region[]
    input->inputs.push_back(nullptr);                           // 포트1 HeightMap 없음
    // 포트1에 HeightMap 넣기
    auto hmPort = std::make_shared<VisionData>();
    hmPort->heightmaps.push_back(hm);
    input->inputs[1] = hmPort;

    auto res = RegionMeasureTool().execute(input);
    ASSERT_EQ(res.status, ToolStatus::Ok);
    EXPECT_NEAR(findMeas(res, "areaPx"), 2.0, 1e-9);   // 첫 원소만
    // 원소별 prefix 이름이 더는 생기지 않음 (스칼라 이름만)
    EXPECT_TRUE(std::isnan(findMeas(res, "0.areaPx")));
}
```

- [ ] **Step 2: Build and run — verify it fails**

Run:
```
cmake --build D:/GitHub/VisionSWTool/wt-refactor/build --config Release --target ToolsTests
D:/GitHub/VisionSWTool/wt-refactor/build/bin/Release/ToolsTests.exe --gtest_filter=RegionMeasureTest.MultipleRegionsMeasuresFirstOnly
```
Expected: FAIL — 현재 구현은 두 원소 모두 측정하고 `0.areaPx`/`1.areaPx` prefix 이름을 만든다.

- [ ] **Step 3: Scalarize the tool**

Replace `RegionMeasureTool::execute` (`VisionTools/src/RegionMeasureTool.cpp:109-143`) with:

```cpp
ToolResult RegionMeasureTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "RegionMeasure: 입력이 없습니다." };

    auto rg = input->inRegion(0);
    if (!rg || rg->empty())
        return { ToolStatus::Fail, "RegionMeasure: Region이 없습니다." };

    const HeightMap* map = input->inHeightMap(1) ? input->inHeightMap(1).get() : nullptr;

    auto out = std::make_shared<VisionData>();
    out->sourceId = input->sourceId;

    auto ms = measureOne(*rg, map, m_params, "");   // 단일 측정, prefix 없음
    if (ms.empty())
        return { ToolStatus::Fail, "RegionMeasure: 빈 Region입니다." };
    out->measurements = std::move(ms);

    return { ToolStatus::Ok, "", out };
}
```

- [ ] **Step 4: Update the port declaration**

In `ui/src/renderer/src/types/tools.ts:126`, change RegionMeasure inputs from
`inputs: [{ type: 'Region', isArray: true }, 'HeightMap'],`
to
`inputs: ['Region', 'HeightMap'],`

- [ ] **Step 5: Build and run — verify pass**

Run:
```
cmake --build D:/GitHub/VisionSWTool/wt-refactor/build --config Release --target ToolsTests
D:/GitHub/VisionSWTool/wt-refactor/build/bin/Release/ToolsTests.exe --gtest_filter=RegionMeasureTest.*
```
Expected: 모든 RegionMeasureTest PASS (기존 단일 Region 테스트 + 신규). 기존 테스트는 단일 Region이라 prefix 없어 그대로 통과.

- [ ] **Step 6: Commit**

```
git add VisionTools/src/RegionMeasureTool.cpp ui/src/renderer/src/types/tools.ts Tests/Tools/RegionMeasureTest.cpp
git commit -m "feat(region-measure): 스칼라화 — 내부 배열 루프 제거, inRegion(0) 단일 측정"
```

---

## Task 3: 엔진 브로드캐스트 루프 배선

**Files:**
- Modify: `VisionEngine/src/main.cpp` (노드 파싱 ~134-153, 실행 부 318-323)

**Interfaces:**
- Consumes: `vision::PortMeta`, `broadcastAxisLengths`, `sliceBroadcastInput`(Task 1), `computeBroadcast`(`Broadcast.h`), `tool->execute(VisionDataPtr)`.
- Produces: N개 원소 출력을 인덱스 순서로 concat 한 단일 `result.output`. (하류 노드가 기존과 동일하게 소비.)

- [ ] **Step 1: Include the new headers**

At top of `VisionEngine/src/main.cpp`, add near existing Core includes:
```cpp
#include "BroadcastRun.h"
#include "Broadcast.h"
```

- [ ] **Step 2: Parse `inputPorts` into NodeSpec**

In the `NodeSpec` struct (`main.cpp:134-137`), add a field:
```cpp
    std::vector<vision::PortMeta> inputPorts;
```
In the node parse loop (`main.cpp:142-152`), after reading `ns.params`, add:
```cpp
        if (n.contains("inputPorts") && n.at("inputPorts").is_array()) {
            for (const auto& pm : n.at("inputPorts")) {
                vision::PortMeta m;
                m.type    = pm.value("type", std::string());
                m.isArray = pm.value("isArray", false);
                ns.inputPorts.push_back(m);
            }
        }
```
(폴백: 없으면 빈 벡터 → 전 포트 스칼라 아님으로 취급되지만 축 판정은 벡터 길이도 봐야 하므로 아래 Step 3에서 빈 메타면 N=1로 처리.)

- [ ] **Step 3: Wrap `tool->execute` with the broadcast loop**

Replace the execution block (`main.cpp:318-323`) — currently:
```cpp
            // 6. 실행
            const auto t0 = std::chrono::steady_clock::now();
            auto result   = tool->execute(inputData);
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                                         std::chrono::steady_clock::now() - t0).count();
            VISION_LOG_INFO("[pipeline] {} [{:.1f} ms]", ns.type, elapsedMs);
```
with:
```cpp
            // 6. 실행 — 브로드캐스트: 스칼라선언 포트가 배열 받으면 원소별 N회 (설계 §4.4)
            const auto t0 = std::chrono::steady_clock::now();
            ToolResult result;
            std::vector<std::size_t> axisLens =
                inputData ? vision::broadcastAxisLengths(*inputData, ns.inputPorts)
                          : std::vector<std::size_t>{};
            vision::BroadcastPlan plan = vision::computeBroadcast(axisLens);
            if (!plan.ok) {
                result = { ToolStatus::Fail,
                           "브로드캐스트 배열 길이 불일치 (" + ns.type + ")" };
            } else if (plan.count <= 1) {
                result = tool->execute(inputData);            // 기존 경로 (회귀 0)
            } else {
                // N>1: 원소별 실행 후 생산 벡터를 인덱스 순서로 concat
                auto agg = std::make_shared<VisionData>();
                bool anyOk = false;
                std::string firstErr;
                for (std::size_t i = 0; i < plan.count; ++i) {
                    auto slice = vision::sliceBroadcastInput(*inputData, i, ns.inputPorts);
                    auto r = tool->execute(slice);
                    if (r.status != ToolStatus::Ok) {
                        result = { ToolStatus::Fail,
                                   r.message.empty() ? ("브로드캐스트 원소 실패 idx="
                                       + std::to_string(i)) : r.message };
                        firstErr = result.message;
                        agg.reset();
                        break;
                    }
                    if (r.output) {
                        anyOk = true;
                        if (agg->sourceId.empty()) agg->sourceId = r.output->sourceId;
                        if (!agg->frames) agg->frames = r.output->frames;
                        auto& o = *r.output;
                        for (auto& e : o.heightmaps)   agg->heightmaps.push_back(e);
                        for (auto& e : o.clouds)       agg->clouds.push_back(e);
                        for (auto& e : o.regions)      agg->regions.push_back(e);
                        for (auto& e : o.planes)       agg->planes.push_back(e);
                        for (auto& e : o.lines)        agg->lines.push_back(e);
                        for (auto& e : o.geometries)   agg->geometries.push_back(e);
                        for (auto& e : o.profiles)     agg->profiles.push_back(e);
                        for (auto& e : o.points)       agg->points.push_back(e);
                        for (auto& e : o.measurements) agg->measurements.push_back(e);
                        for (auto& e : o.decisions)    agg->decisions.push_back(e);
                        for (auto& e : o.overlays)     agg->overlays.push_back(e);
                        for (auto& f : o.definedFrames) agg->definedFrames.push_back(f);
                    }
                }
                if (agg && anyOk) result = { ToolStatus::Ok, "", agg };
                else if (result.status != ToolStatus::Fail)
                    result = { ToolStatus::Ok, "", agg };   // N>1 이지만 전부 빈 출력
            }
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                                         std::chrono::steady_clock::now() - t0).count();
            VISION_LOG_INFO("[pipeline] {} [{:.1f} ms]{}", ns.type, elapsedMs,
                            plan.count > 1 ? (" x" + std::to_string(plan.count)) : "");
```

- [ ] **Step 4: Build the engine**

Run:
```
cmake --build D:/GitHub/VisionSWTool/wt-refactor/build --config Release --target VisionEngine
```
Expected: `VisionEngine.exe` 링크 성공. (`ToolResult`는 집계+기본멤버초기화라 `ToolResult result;`가 status=Ok/빈 output으로 기본구성됨 — 확인됨.)

- [ ] **Step 5: Commit**

```
git add VisionEngine/src/main.cpp
git commit -m "feat(pipeline): 브로드캐스트 실행 루프 배선 — 스칼라툴 배열 fan-out"
```

---

## Task 4: UI 포트 메타 동봉 (DRY 헬퍼)

**Files:**
- Create: `ui/src/renderer/src/recipe.ts`
- Modify: `ui/src/renderer/src/App.tsx:191-195` (handleNodeRun), `App.tsx:435-440` (folder-inspect buildRecipe)

**Interfaces:**
- Consumes: `TOOL_DEF_MAP`, `portType`, `portIsArray`(`ui/src/renderer/src/types/tools.ts`).
- Produces: `nodeToEnginePayload(id, toolType, params) => { id, type, params, inputPorts: {type,isArray}[] }`.

- [ ] **Step 1: Create the shared helper**

Create `ui/src/renderer/src/recipe.ts`:

```ts
import { TOOL_DEF_MAP, portType, portIsArray } from './types/tools'

// 엔진 전송용 노드 payload. 엔진 브로드캐스트가 포트 arity를 알도록 inputPorts 동봉.
export function nodeToEnginePayload(
  id: string,
  toolType: string,
  params: Record<string, unknown>
) {
  const def = TOOL_DEF_MAP[toolType]
  const inputPorts = (def?.inputs ?? []).map(p => ({
    type: portType(p),
    isArray: portIsArray(p),
  }))
  return { id, type: toolType, params: params ?? {}, inputPorts }
}
```

- [ ] **Step 2: Confirm `portType` is exported**

Check `ui/src/renderer/src/types/tools.ts` exports both `portType` and `portIsArray`. `portIsArray` exists (line 9). If `portType` is missing, add next to it:
```ts
export function portType(p: PortDecl): PortType { return typeof p === 'string' ? p : p.type }
```

- [ ] **Step 3: Wire into handleNodeRun**

In `App.tsx:191-195`, replace the `nodes:` mapping:
```ts
      nodes: allNodes.filter(n => needed.has(n.id)).map(n =>
        nodeToEnginePayload(
          n.id,
          (n.data as { toolType: string }).toolType,
          (n.data as { params: Record<string, unknown> }).params ?? {}
        )
      ),
```
Add the import at top of `App.tsx`:
```ts
import { nodeToEnginePayload } from './recipe'
```

- [ ] **Step 4: Wire into folder-inspect buildRecipe**

In `App.tsx:435-440`, replace the `nodes:` mapping so it merges the per-file param overrides then delegates to the helper:
```ts
        nodes: allNodes.map(n => {
          const params = { ...((n.data as { params?: Record<string, unknown> }).params ?? {}) }
          if (paths.has(n.id)) params.path = paths.get(n.id)
          if (csv && n.id === csv.id) params.label = setLabel
          return nodeToEnginePayload(n.id, (n.data as { toolType: string }).toolType, params)
        }),
```

- [ ] **Step 5: Typecheck / build UI**

Run:
```
cd D:/GitHub/VisionSWTool/wt-refactor/ui && npm run build
```
Expected: 타입체크·번들 성공 (electron-vite build 는 tsc 통과 요구).

- [ ] **Step 6: Commit**

```
git add ui/src/renderer/src/recipe.ts ui/src/renderer/src/App.tsx ui/src/renderer/src/types/tools.ts
git commit -m "feat(ui): 엔진 레시피에 포트 메타(inputPorts) 동봉 — 공유 헬퍼"
```

---

## Task 5: 통합 검증 + 회귀

**Files:** (없음 — 검증 전용)

**Interfaces:**
- Consumes: 빌드된 `VisionEngine.exe`, dev 앱, 기존 골든 레시피.

- [ ] **Step 1: 전체 빌드 + 전체 유닛테스트**

Run:
```
cmake --build D:/GitHub/VisionSWTool/wt-refactor/build --config Release
ctest --test-dir D:/GitHub/VisionSWTool/wt-refactor/build -C Release --output-on-failure
```
Expected: CoreTests(신규 BroadcastRunTest 포함) + ToolsTests 전부 PASS.

- [ ] **Step 2: fan-out 통합 — 앱에서 수동 확인**

앱 실행(`run-visionsw-app` 스킬 또는 `cd ui && npm run dev`). 그래프 구성:
`HeightMapLoader → CreateROI(ROI 3개) → RegionMeasure`.
RegionMeasure 결과에 measurements가 **3벌(원소별)** 나오는지 결과창에서 확인. 반복 실행 시 순서 동일(결정론).

- [ ] **Step 3: 회귀 — 기존 레시피 CSV diff 0**

브로드캐스트를 쓰지 않는 기존 골든 레시피를 `VisionEngine --repeat-analyze <recipe> <folder> <csv>`로 실행, 이전 골든 CSV와 diff 0 확인. (스칼라 포트에 배열 없음 → N=1 경로.)

- [ ] **Step 4: 영향 레시피 식별**

다중 ROI를 RegionMeasure로 측정하던 기존 레시피가 있으면 출력 이름구조가 prefix→원소별로 바뀐다. 해당 레시피를 찾아 골든 CSV를 갱신(의도된 변화). 없으면 스킵.

- [ ] **Step 5: 최종 커밋 (필요 시 골든 갱신)**

골든 갱신이 있었다면:
```
git add <갱신된 골든 파일>
git commit -m "test(f1): 브로드캐스트 통합 검증 + 다중ROI 골든 갱신"
```
없으면 이 태스크는 커밋 없이 검증만.

---

## Self-Review 결과

- **Spec 커버리지:** §3.1 포트메타→Task4, §3.2 엔진루프→Task3, §3.3 sliceInput→Task1, §3.4 RegionMeasure→Task2, §6 테스트→Task1/2/5, §7 리스크(CsvWriter)→Task5 Step4. 전 항목 매핑됨.
- **플레이스홀더:** 없음 — 모든 코드/명령/기대출력 명시.
- **타입 일관성:** `PortMeta{type,isArray}`, `broadcastAxisLengths`, `sliceBroadcastInput`, `nodeToEnginePayload`, `computeBroadcast/BroadcastPlan{count,ok}` 전 태스크 동일 시그니처.
- **검증된 전제:** `ToolResult`는 기본생성자 있음(집계+기본초기화) → Task3 `ToolResult result;` OK. `Core/CMakeLists.txt`는 명시 소스목록 → Task1 Step4 명시 추가.
