# VisionSW — 개발자 온보딩 가이드

산업용 3D 비전 검사 소프트웨어. 노드 기반 파이프라인으로 HeightMap(높이맵)·이미지·포인트클라우드를 처리하여 평면 피팅, 단차/높이 측정 등을 수행한다.

---

## 1. 한눈에 보기

| 항목 | 내용 |
|------|------|
| 목적 | 노드 그래프로 검사 레시피를 구성 → 3D 센서 데이터에서 측정값 추출 (반복성 테스트 등) |
| 아키텍처 | **C++ 알고리즘 엔진** + **Electron/React UI**, 둘은 **WebSocket**(`ws://localhost:9000`)으로 통신 |
| 핵심 워크플로우 | `HeightMapLoader → PlaneFit(평면 피팅) → HeightFromPlane(평면 대비 수직거리 측정)` |

데이터 흐름:
```
[UI] 노드 그래프(JSON 레시피) ──run──▶ [VisionEngine]
                                          │ 위상정렬(Kahn) 후 노드 순차 실행
[UI] 결과/로그/미리보기 ◀──event 스트림── │ (start/log/result/done/error)
```

---

## 2. 기술 스택

**백엔드 (C++17)**
- **Crow** — WebSocket 서버
- **nlohmann/json** — JSON 직렬화
- **stb_image / stb_image_write** — PNG 입출력 (HeightMap 로딩, 미리보기 인코딩)
- **spdlog** — 로깅
- **GoogleTest** — 단위 테스트
- 의존성은 **vcpkg**(manifest 모드, `vcpkg.json`)로 관리

**프론트엔드 (Electron + React + TypeScript)**
- **Electron 42** + **electron-vite 5** — 데스크톱 앱 / 빌드
- **React 19** + **@xyflow/react 12** — 노드 그래프 에디터
- **ws** — WebSocket 클라이언트 (Electron main 프로세스)

---

## 3. 폴더 구조

```
visionsw/
├── CMakeLists.txt          # 루트 빌드 (MSVC / VS2022)
├── vcpkg.json              # C++ 의존성 매니페스트
├── Core/                   # 공용 데이터 타입 · 파이프라인
│   ├── include/
│   │   ├── VisionData.h     # Image2D / PointCloud3D / HeightMap / PlaneModel 컨테이너
│   │   ├── HeightMap.h           # 높이맵 (mm 좌표 변환 포함)
│   │   ├── IAlgorithmTool.h # 모든 툴의 인터페이스 (execute)
│   │   └── Pipeline.h
│   └── src/
├── VisionTools/            # 알고리즘 구현 (정적 라이브러리)
│   ├── include/ · src/
│   │   ├── NoiseFilter         # 2D 가우시안·미디언 / 3D 이웃수 필터
│   │   ├── EdgeDetector        # Sobel / Canny
│   │   ├── PlaneFitTool        # 평면 피팅 (LS / RANSAC / SVD)
│   │   ├── RefHeightTool       # 기준 평면 + 평균높이 산출
│   │   ├── HeightFromPlaneTool # 평면 대비 수직거리 측정
│   │   ├── LineCenterTool      # 라인 중심·각도 검출 (좌표정렬 기준점)
│   │   ├── AlignTool           # 기준점을 좌표계 원점으로 설정
│   │   ├── CsvWriterTool       # 측정값 CSV 저장
│   │   └── ThicknessMeasure    # 두께 측정
├── VisionEngine/           # WebSocket 서버 (실행 파일)
│   └── src/
│       ├── main.cpp           # Crow 서버, 파이프라인 실행, 결과 직렬화
│       ├── ToolFactory.cpp    # JSON 파라미터 → 툴 인스턴스 생성 (+ 다수 I/O·머지 툴 인라인 정의)
│       ├── ExposureMergeCore.h# 이중/삼중 노출 머지 공유 코어
│       ├── HeightMapCache.h        # HeightMap 파일 로드·LRU 캐시·폴더 프리로드
│       ├── JsonBridge.*       # JSON ↔ 레시피 변환
│       └── ImageEncoder.h     # HeightMap/이미지 → base64 PNG 미리보기
├── Tests/                  # GTest 단위 테스트
└── ui/                     # Electron + React 앱
    └── src/
        ├── main/            # Electron 메인 (창 생성, engine.ts = WS 클라이언트)
        ├── preload/         # IPC 브릿지
        └── renderer/src/
            ├── App.tsx              # 상태 관리, 엔진 이벤트 처리
            ├── types/tools.ts       # 노드 정의(포트 타입·기본 파라미터) ★ 노드 추가 시 여기부터
            └── components/
                ├── NodeCanvas.tsx       # @xyflow 그래프 + 포트 타입 연결 검증
                ├── ToolNode.tsx         # 노드 비주얼 + 인라인 결과
                ├── ToolboxPanel.tsx     # 툴 팔레트 (카테고리 자동 생성)
                ├── NodePanel.tsx        # 선택 노드 파라미터/결과 패널
                ├── ImageViewer.tsx      # ★ 공용 2D 디스플레이 (줌/팬/원본비율/오버레이)
                ├── RoiCanvas.tsx        # ImageViewer + ROI 그리기 (공용)
                ├── PlaneFitEditor.tsx   # ref ROI + 알고리즘 선택
                ├── HeightFromPlaneEditor.tsx # measure ROI + 집계/합부판정
                ├── ParamPanel.tsx       # 범용 파라미터 폼
                └── ResultPanel.tsx      # 로그/결과 스트림
```

---

## 4. 노드 카탈로그

노드는 UI 라벨(`tools.ts`) 기준으로 표기하고, 괄호 안은 내부 `type`(레시피 JSON·`ToolFactory`에서 쓰는 식별자)이다.

| 카테고리 | 노드 (type) | 입력 → 출력 | 기능 |
|------|------|------------|------|
| 입력 | **HeightMap Loader** (`HeightMapLoader`) | – → HeightMap | PNG(16/8bit)를 HeightMap으로 로드. `xResMm/yResMm/zResMm` 분해능 지정. file/folder 모드, 파일 캐시 |
| 입력 | **Image Loader** (`ImageLoader`) | – → Image2D | 일반 이미지 로드 |
| 필터 | **Exposure Split** (`ExposureMerge`) | HeightMap → HeightMap | 인터리브 노출 분리(`outputStage`로 저/장 선택) |
| 필터 | **Exposure Merge** (`ExposureMerge2`) | HeightMap → HeightMap | 저/장 2노출 인터리브 Z-map 머지(리플렉션 제거·홀채움). matchTol/reflTol/tolX/tolY/gapK, chunkMode |
| 필터 | **Exposure Merge (3)** (`ExposureMerge3`) | HeightMap → HeightMap | 저>중>장 3노출 캐스케이드 머지 |
| 필터 | **Row Stretch** (`RowStretch`) | HeightMap → HeightMap | ROI 밴드별 행 스케일 보정 |
| 필터 | **Noise Filter** (`NoiseFilter`) | HeightMap → HeightMap | 2D 가우시안/미디언 / 3D 포인트 노이즈 제거 |
| 필터 | **Gap Fill** (`GapFill`) | HeightMap → HeightMap | 무효(NaN) 구멍 채움(neighbor/IDW 등) |
| 필터 | **Edge Detector** (`EdgeDetector`) | Image2D → Image2D | Sobel / Canny 엣지 검출 |
| 정렬 | **Line Finder** (`LineCenter`) | HeightMap → **Point** | HeightMap 이진화 후 ROI 내 라인 중심(x,y)·각도 검출(회전 ROI 지원). 좌표정렬 기준점 생산 |
| 정렬 | **Align** (`Align`) | HeightMap + **Point** → HeightMap | 검출 기준점을 좌표계 원점으로 설정(하류 ROI가 타겟 추종) |
| 측정 | **Plane Fit** (`PlaneFit`) | HeightMap → **Plane** | 여러 ref ROI로 평면 `z=ax+by+c` 피팅(LS/RANSAC/SVD). 평면을 출력에 첨부 |
| 측정 | **Ref Height** (`RefHeight`) | HeightMap → **Plane, Heights** | ref ROI로 기준 평면+평균높이 산출(SOR/tail 옵션) |
| 측정 | **Height Measure** (`HeightMeasure`) | HeightMap + **Plane** → Heights | measure ROI에서 Z 추출(Mean/Max/HighTail) → 평면까지 수직거리 + tolerance 합부판정 |
| 측정 | **Thickness Measure** (`ThicknessMeasure`) | PointCloud3D → PointCloud3D | 두께 측정 + 공차 판정 |
| 변환 | **HeightMap to Cloud** (`HeightMapToCloud`) | HeightMap → PointCloud3D | HeightMap을 포인트클라우드로 변환(`step` 서브샘플) |
| 변환 | **Exposure Merge (Cloud)** (`ExposureMergeCloud`) | HeightMap → PointCloud3D | 이중노출 머지 결과를 포인트클라우드로 출력(strict 기본값) |
| 출력 | **CSV Writer** (`CsvWriter`) | Heights → – | 측정 높이값을 CSV로 저장 |
| 출력 | **Image Saver** (`ImageSaver`) | Any → – | HeightMap/이미지를 PNG/TIFF 등으로 저장(타임스탬프 경로) |
| 출력 | **Cloud Saver** (`CloudSaver`) | PointCloud3D → – | 포인트클라우드를 PLY/ASC 등으로 저장 |

**포트 타입**(`tools.ts`의 `PortType`): `HeightMap` · `Plane` · `Heights` · `Image2D` · `PointCloud3D` · `Point` · `Any`
연결 규칙: `Any`이거나 타입이 같으면 연결 허용.

**평면 전달 방식**: `PlaneFit`/`RefHeight`가 `VisionData::plane`(`PlaneModel`)에 피팅 결과를 실어 출력(`Plane` 포트) → `HeightMeasure`가 읽어 수직거리 계산.
수직거리 = `(z − (a·x + b·y + c)) / √(1 + a² + b²)` (부호 있음, mm)

**기준점 전달**: `LineCenter`가 검출한 기준점을 `Point` 포트로 출력 → `Align`이 받아 HeightMap 좌표계 원점을 재설정.

> ⚠️ 참고: `LineFitHeight`(구 노드)는 현재 코드에 없다. `RefHeight`/`HeightMeasure`로 대체됨.

---

## 5. 빌드 & 실행

### 사전 요구사항
- Windows + **Visual Studio 2022** (MSVC v143)
- **CMake** (3.x), **vcpkg** (`C:\vcpkg` 가정)
- **Node.js** + npm

### C++ 엔진 빌드
```bash
# 최초 구성 (vcpkg 의존성 자동 설치)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-windows -G "Visual Studio 17 2022" -A x64

# 빌드
cmake --build build --config Release --target VisionEngine
# 산출물: build/bin/Release/VisionEngine.exe
```
> ⚠️ 폴더를 옮기면 `build/CMakeCache.txt`에 박힌 절대경로 때문에 재구성이 필요하다.
> `build/vcpkg_installed`만 보존하고 나머지를 지운 뒤 위 구성 명령을 다시 실행하면 의존성 재설치 없이 빠르게 재구성된다.

### UI 실행 (개발)
```bash
cd ui
npm install        # 최초 1회
npm run dev        # electron-vite 개발 서버 + Electron 창
```
`engine.ts`가 `ws://localhost:9000`으로 자동 연결한다. 엔진을 먼저 실행해 두면 UI 상단 표시등이 연결됨으로 바뀐다.

### 실행 순서
```bash
# 1) 엔진 실행
build/bin/Release/VisionEngine.exe
# 2) UI 실행
cd ui && npm run dev
# (재빌드 전 기존 프로세스 종료)
taskkill /F /IM VisionEngine.exe /T
taskkill /F /IM electron.exe /T
```

---

## 6. 새 노드 추가 절차 (요약)

1. `VisionTools/include|src/`에 `IAlgorithmTool` 파생 클래스 구현 → `VisionTools/CMakeLists.txt`에 `.cpp` 추가
2. `VisionEngine/src/ToolFactory.cpp`에 JSON 파라미터 파싱 + 생성 케이스 추가
3. `VisionEngine/src/main.cpp`에 결과 직렬화 추가 (`dynamic_cast`로 결과 추출)
4. `ui/.../types/tools.ts`에 노드 정의(포트·기본 파라미터) 추가 → 툴박스에 자동 노출
5. 필요 시 `NodePanel.tsx`에 전용 에디터 연결, `ToolNode.tsx`에 인라인 결과 표시

---

## 7. 작업 현황 메모

- **이미지 미리보기**: 백엔드에서 최대 1024px로 인코딩, UI는 `ImageViewer`로 원본 비율 + nearest-neighbor 확대(`image-rendering: pixelated`).
- **3D 뷰(예정)**: PlaneFit 결과(포인트 + 피팅 평면)를 three.js로 시각화하여 피팅 품질을 직관적으로 확인하는 기능이 다음 단계로 계획됨.
