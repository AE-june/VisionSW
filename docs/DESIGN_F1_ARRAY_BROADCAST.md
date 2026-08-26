# 상세 설계 — F1 배열모드 브로드캐스트 (첫 수직 슬라이스)

> 상위 방향: [ARCHITECTURE_DIRECTION.md](./ARCHITECTURE_DIRECTION.md) §7 F1 (제어흐름).
> 기반 설계: [1.DESIGN_T0_FRAME_AND_COLLECTION.md](./1.DESIGN_T0_FRAME_AND_COLLECTION.md) §4 (컬렉션 & 브로드캐스트).
> 최초 작성: 2026-08-26.

## 0. 배경과 결정

F1(제어흐름)의 반복 모델은 **암묵적 벡터화(Aurora 방식, 설계 §4.2 방식 A)** 로 확정됐다.
ForEach 노드를 두지 않고, 스칼라 선언 툴에 배열 입력이 오면 **엔진이 원소별로 N회 실행**한다.

이미 존재하는 것:
- `Core/include/Broadcast.h` — `computeBroadcast(arrayLengths) → {count, ok}` 순수함수 + 단위테스트 8종(`Tests/Core/BroadcastTest.cpp`).
- `tools.ts` — `PortSpec { type, isArray }`, `portIsArray()` 헬퍼. 배열 포트 선언 다수(CreateROI→Region[], CloudToProfiles→Profile[] 등).
- `VisionData` — 타입별 벡터 슬롯 + `inRegions()`/`inProfiles()` 배열 헬퍼. 슬롯 다중화 완료(T0-2 P3, `fa24716`).

**미배선(이 문서가 착지시킬 것):** `computeBroadcast`가 `BroadcastTest.cpp`에서만 쓰이고 **`runPipeline`에 실행 루프가 배선되지 않았다.** 배열 생산 노드가 없어(당시) N=1 경로만 존재했다. 지금은 CreateROI가 Region[]을 내므로 배선 가능.

**전략(사용자 확정):** 전부 선구현 아니라 **얇은 수직 슬라이스** — 최소 브로드캐스트 베이스 + 실제 툴 하나를 함께 착지시키고, 다음 툴을 추가하며 베이스를 확장한다. 투기적 완전 일반화는 하지 않는다(YAGNI).

**첫 슬라이스 경로(확정):** `CreateROI(Region[]) → RegionMeasure(스칼라, 엔진이 원소별 자동 반복) → measurements N벌`. fan-out 브로드캐스트를 실제 검사 워크플로우로 증명한다.

## 1. 목표 / 비목표

**목표**
- `runPipeline`에 브로드캐스트 실행 루프 배선(§4.4 규칙).
- 스칼라 선언 툴이 배열 입력을 받으면 엔진이 원소별 N회 실행, 출력을 인덱스 순서로 수집.
- 엔진이 포트 arity(배열/스칼라)를 알 수 있도록 레시피에 포트 메타 동봉.
- RegionMeasure를 스칼라 툴로 전환(내부 배열 루프 제거) — 브로드캐스트 베이스의 첫 소비자.

**비목표(이번 범위 밖)**
- Filter/Select/Collect 축약 노드 등록·재구현(`PLANNED_REDUCTION_NODES` 그대로 둠).
- Branch/Condition(조건부 실행 스킵 — executor 변경 필요).
- 중첩 배열(배열의 배열), 원소별 로컬 프레임(T2-3).

## 2. 아키텍처

`runPipeline`의 노드 async 태스크는 현재: 상류 입력 대기 → 포트별 `merged` VisionData 구성 → `tool->execute(inputData)` **1회** → promise set. 브로드캐스트는 **`tool->execute` 직전**에 분기로 삽입한다(설계 §4.6 P3가 지정한 부착점).

```
inputData 구성 완료
  │
  ├─ 포트 메타로 declaredArray[p], portType[p] 판정
  ├─ 스칼라선언 포트가 받은 (portType[p]) 벡터 길이 L>1 → 브로드캐스트 축
  ├─ arrayLengths 수집 → computeBroadcast → {N, ok}
  │
  ├─ ok=false (길이 불일치) → ToolStatus::Fail (명시적 에러)
  ├─ N==1 → tool->execute(inputData) 1회   (기존 경로, 회귀 0)
  └─ N>1  → for i in [0,N): tool->execute(sliceInput(inputData, i))
              출력 원소를 집계 VisionData에 인덱스 순서로 concat
```

## 3. 컴포넌트별 변경

### 3.1 레시피 포트 메타 (UI → 엔진)
- **결정:** tools.ts를 단일 진실(SSOT)로 두고 UI가 노드 msg에 포트 메타를 동봉한다. 엔진이 tools.ts를 미러링하는 이중 관리를 피한다.
- 각 노드 직렬화에 입력 포트 메타 추가:
  ```ts
  inputPorts: TOOL_DEF_MAP[type].inputs.map(p => ({
    type: portType(p), isArray: portIsArray(p)
  }))
  ```
- 위치: 레시피/실행 payload를 만드는 곳(엔진 전송 직전). 기존 `nodes[].params` 옆에 `nodes[].inputPorts`.
- **폴백:** 노드에 `inputPorts`가 없으면 엔진은 전 포트 스칼라로 간주 → 기존 동작 보존(구 레시피/헤드리스 호환).

### 3.2 엔진 브로드캐스트 루프 (`VisionEngine/src/main.cpp`)
- 노드 파싱 시 `inputPorts` 읽어 `std::vector<PortMeta>{type, isArray}` per node.
- inputData 구성 후, `tool->execute` 직전:
  1. 각 입력 포트 p에 대해: `isArray[p]==false`이고 그 포트로 들어온 `portType[p]` 벡터 길이 L>1이면 브로드캐스트 축에 L 추가.
  2. `computeBroadcast(lengths)` → `{N, ok}`. `!ok` → Fail 이벤트 + pipelinePass=false.
  3. `N<=1`: 기존 단일 실행.
  4. `N>1`: `for i in [0,N)`: `sliceInput(inputData, i, portMetas)` → `tool->execute` → 반환 VisionData의 생산 벡터(measurements/regions/geometries/…)를 **집계 out에 인덱스 순서로 append**.
- **결정론(§4.5 필수):** 기본 순차 실행. 병렬화 시 인덱스로 기록해 직렬과 동일 순서 보장(기존 HeightMeasure ROI 병렬 패턴). 첫 슬라이스는 순차로 시작(단순).

### 3.3 슬라이싱 헬퍼 `sliceInput`
- 입력: `inputData`(포트별 inputs), 원소 인덱스 i, 포트 메타.
- 동작: 새 VisionData를 만들어 각 입력 포트 복제하되 —
  - **스칼라선언 브로드캐스트 포트:** `portType[p]` 벡터에서 원소 i만 남긴 단일원소로 축소.
  - **배열선언 포트:** 통째 유지(툴이 배열 소비).
  - **길이 1 벡터/스칼라:** 그대로(동일값 확장).
- `portType → VisionData 벡터` 매핑 필요(Region→regions, Geometry→geometries, Profile→profiles, HeightMap→heightmaps, …). 작은 switch 헬퍼.
- 순수 함수로 분리해 단위테스트 가능하게.

### 3.4 RegionMeasure 스칼라화 (`VisionTools/src/RegionMeasureTool.cpp`, `tools.ts`)
- tools.ts: `inputs: ['Region', 'HeightMap']` — Region 포트에서 `isArray` 제거.
- `execute`: `inRegion(0)` 단일 Region 측정. 내부 `for (regions)` 루프와 prefix(인덱스/label) 로직 제거. `measureOne(*rg, map, params, "")` 한 벌만 out->measurements에 넣음.
- 원소별 fan-out은 이제 엔진 브로드캐스트가 담당. label 구분이 필요하면 후속(측정값에 원소 인덱스 메타를 다는 별도 작업).

## 4. 데이터 흐름 (첫 슬라이스)

```
CreateROI  --Region[](N)-->  RegionMeasure(스칼라 선언)
                               엔진: Region 포트 스칼라선언 + 길이 N>1
                               → computeBroadcast → N회
                               → 각 회 measureOne → measurements 11종
                               → out.measurements = N×11 (인덱스 순서)
```

## 5. 에러 처리
- 배열 길이 불일치(둘 다 1 아님) → `computeBroadcast.ok=false` → Fail 이벤트, 파이프라인 실패.
- 길이 0 배열 → N=0 → 0회 실행, 빈 출력(정상, Fail 아님).
- 원소 실행 중 하나가 Fail → 파이프라인 Fail(첫 슬라이스는 fail-fast). 부분 성공 정책은 후속.

## 6. 테스트
- **통합:** 합성 레시피 — CreateROI 3 ROI → RegionMeasure → measurements 33개(3×11), 순서 결정론(반복 실행 bit-identical).
- **회귀:** 기존 레시피 전부 스칼라 포트에 배열 없음 → N=1 → CSV diff 0. 다중 입력 노드(ReduceDomain·RegionMeasure 포함) 레시피 반드시 포함.
- **영향 레시피:** 다중 ROI를 RegionMeasure로 측정하던 기존 레시피는 출력 이름구조가 바뀜(prefix 제거 → 원소별 반복). 해당 레시피만 식별해 재검증/골든 갱신.
- **단위:** `sliceInput` 신규(원소별 슬라이스, 배열포트 통째 유지, 길이1 확장). `computeBroadcast`는 기존 8종 유지.

## 7. 리스크
- **CsvWriter 열 순서·이름:** RegionMeasure prefix 제거로 기존 다중ROI CSV 컬럼명·순서 변화. §4.5 결정론 준수 + 영향 레시피 골든 갱신으로 대응.
- **포트 메타 누락 경로:** 헤드리스/구 레시피가 `inputPorts` 없이 오면 전 포트 스칼라 폴백 — 배열 생산자가 낀 신 레시피는 반드시 메타를 실어야 정상 fan-out.
- **type→벡터 매핑 누락:** 새 타입 추가 시 `sliceInput` switch 갱신 필요(컴파일 경고/테스트로 방어).

## 8. 다음 구현 (이 슬라이스 이후 순서)
1. **Filter/Select 축약노드 착지** — `PLANNED_REDUCTION_NODES` → 실제 등록·엔진 구현. Filter(metric·min/max로 배열 부분집합), Select(인덱스로 원소 1개). fan-in, 브로드캐스트 루프 불필요.
2. **Collect 의미 정합** — 현재 포트 fan-in. 배열 수집 의미와 정합/문서화.
3. **ConnectedComponents (T2-1)** — 진짜 런타임 결정 Region[] 생산자. 브로드캐스트 fan-out을 임의 개수로 확장 검증. 결정론 정렬(raster 순서).
4. **메트릭 필터 (T2-2)** — mm 단위 필터링(프레임/단위 변환 전제).
5. **Branch/Condition** — Decision으로 하류 조건부 스킵. executor 변경(미실행 표시) 필요 — 별도 설계.
