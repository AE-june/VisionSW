# VisionSW — 아키텍처

## 핵심 원칙: 좁은 허리 (Narrow Waist)

모든 노드 간 데이터는 `VisionDataPtr` 단 하나로 전달한다.  
노드는 전 단계 노드의 타입을 몰라도 된다. `in(port)` 헬퍼로 원하는 슬롯만 꺼낸다.

```
[Node A] ──port0──▶ [Node B] ──port0──▶ [Node C]
                        └──port1──▶
```

---

## 핵심 데이터 타입

### HeightMap (`Core/include/HeightMap.h`)

2D 격자 높이 이미지. N채널 float 배열.

```cpp
struct HeightMap {
    int    width, height;
    float  xResMm, yResMm, zResMm;    // 픽셀당 실제 해상도 (mm)
    double originCol, originRow;       // 물리 원점 오프셋
    std::vector<float> data;           // row-major, NaN = 무효 픽셀
    std::string frameId;

    bool  valid(int c, int r) const;   // !isnan
    float zMm(int c, int r) const;     // data[r*width+c]
    bool  inBounds(int c, int r) const;
    bool  empty() const;
};
```

NaN이 유효·무효 구분자다. 필터/측정 툴은 반드시 NaN 픽셀을 건너뛴다.

### Region (`Core/include/Region.h`)

픽셀 마스크. HeightMap과 동일 해상도 기준.

```cpp
struct Region {
    int   width, height;
    std::vector<uint8_t> mask;    // 1 = 포함, 0 = 제외
    std::string label;

    bool  empty() const;
    int   pixelCount() const;
};
```

### VisionData (`Core/include/VisionData.h`)

파이프라인 데이터 컨테이너. 모든 노드 간 유일한 통신 단위.

```cpp
struct VisionData {
    // 포트별 상류 입력 — inputs[n] = n번 포트에 연결된 상류 노드 출력
    std::vector<std::shared_ptr<VisionData>> inputs;

    // 이 노드의 출력 슬롯들
    std::vector<std::shared_ptr<HeightMap>>    heightmaps;
    std::vector<std::shared_ptr<PointCloud3D>> clouds;
    std::vector<std::shared_ptr<Region>>       regions;
    std::vector<std::shared_ptr<PlaneModel>>   planes;
    std::vector<std::shared_ptr<LineModel>>    lines;
    std::vector<std::shared_ptr<Geometry>>     geometries;
    std::vector<std::shared_ptr<Profile>>      profiles;
    std::vector<Measurement>                   measurements;
    std::vector<Decision>                      decisions;
    // ...
};
```

### Measurement & Decision (`Core/include/Measurement.h`)

```cpp
struct Measurement {
    std::string name;      // 이름 있는 스칼라 측정값
    double      value;
    std::string unit;
    bool        valid;
};

struct Decision {
    std::string name;
    bool        pass;
    double      value;
    std::string criterion;
};
```

### Profile (`Core/include/Profile.h`)

1D 높이 단면. 행/열 방향 또는 임의 선분.

### Geometry (`Core/include/VisionData.h`)

Point / Line / Circle / Plane을 `GeoKind` 태그로 통합한 1급 타입.  
기존 `PlaneModel`, `LineModel`, `RefPoint`에서 `fromXxx()` 헬퍼로 변환 가능.

---

## 포트 기반 입력 규약 (Phase 2)

툴 내부에서 직접 `inputs` 벡터 접근 금지. `in(port)` 계열 헬퍼만 사용.

```cpp
// 올바른 패턴
auto hm     = input->inHeightMap(0);       // 포트 0의 HeightMap
auto region = input->inRegion(1);          // 포트 1의 Region
auto plane  = input->inPlane(2);           // 포트 2의 PlaneModel

// 배열 전체 필요할 때
const auto& regions = input->inRegions(1); // 포트 1의 Region[]
const auto& profiles = input->inProfiles(0);

// 금지
// input->inputs[0]->heightmaps[0]   ← 직접 접근 금지
```

포트 범위 밖이거나 미연결이면 자동으로 `nullptr` 반환 — 툴에서 nullptr 체크만 하면 된다.

---

## 결과 반환 규약

`IAlgorithmTool::execute()` 는 `ToolResult`를 반환한다.

```cpp
struct ToolResult {
    ToolStatus    status;    // Ok / Fail / Skip
    std::string   message;
    VisionDataPtr output;    // 이 노드의 출력 VisionData
};
```

출력 VisionData는 새로 생성해서 반환. 상류 `inputs` 포인터를 복사하거나 수정하지 않는다.  
단일 출력도 벡터에 1개 원소로 넣는다.

```cpp
auto out = std::make_shared<VisionData>();
out->sourceId = input->sourceId;
out->heightmaps.push_back(resultHm);
return { ToolStatus::Ok, "", out };
```

---

## 금지 패턴

| 금지 | 대체 |
|------|------|
| `lastResult()` 호출 | 포트 입력으로 이전 노드 출력 받기 |
| `dynamic_cast<XxxTool*>` | 타입별 분기 없이 VisionData 슬롯 사용 |
| params에 `rois` 벡터 | Region을 포트 입력으로 받기 |
| 충돌 포트에 데이터 병합 | Collect 노드 사용, 충돌 시 경고+덮어쓰기 |
| `inputs[port]` 직접 접근 | `in(port)` / `inHeightMap(port)` 등 헬퍼 |
| 상류 VisionData 수정 | 새 VisionData 생성해서 반환 |

---

## 파이프라인 실행 (`VisionEngine/src/main.cpp`)

1. WebSocket 클라이언트에서 JSON 레시피 수신 (포트 9000)
2. `schemaVersion >= 2` 검증 — 미달 시 즉시 오류 반환
3. `ToolFactory`로 노드 인스턴스 생성
4. 위상 정렬(BFS) 순서로 노드 실행
5. 엣지 라우팅: `dst.inputs[dstPort] = src.output` — 충돌 시 경고 + 마지막 연결 우선
6. 결과 JSON 직렬화 → WebSocket 응답

---

## 프레임 시스템 (`Core/include/Frame.h`)

물리 좌표 변환 등록소. 각 HeightMap/Geometry에 `frameId` 문자열로 연결.

```cpp
struct Frame {
    std::string id;
    // 변환 행렬 등
};

struct FrameRegistry {
    std::map<std::string, Frame> frames;
};
```

측정값에 단위 있는 mm 좌표 필요 시 `VisionData::frames`에서 찾는다.

---

## 테스트 구조 (`Tests/`)

```
Tests/
  Core/            CoreTests — HeightMap, Region, VisionData 단위 테스트
  Tools/           ToolsTests — 개별 툴 단위 테스트
    SyntheticFixtures.h   합성 데이터 픽스처 (tiltedPlane, step, hole, bump)
    InvariantTests.cpp    V2 불변량 테스트 (왕복·보존 성질)
    XxxTest.cpp           툴별 단위 테스트
```

테스트 프레임워크: GoogleTest. 합성 픽스처 난수는 시드 고정 — 재현 가능해야 한다.

---

## 레시피 스키마

JSON 레시피 필수 필드:

```json
{
  "schemaVersion": 2,
  "nodes": [
    { "id": "n1", "type": "HeightMapLoader", "params": { ... } },
    ...
  ],
  "edges": [
    { "src": "n1", "dst": "n2", "srcPort": 0, "dstPort": 0 },
    ...
  ]
}
```

`schemaVersion < 2`는 엔진이 거부한다.
