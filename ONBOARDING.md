# VisionSW — 개발자 온보딩 가이드

산업용 3D 비전 검사 소프트웨어. 노드 기반 파이프라인으로 ZMap(높이맵)·이미지·포인트클라우드를 처리하여 평면 피팅, 단차/높이 측정 등을 수행한다.

---

## 1. 한눈에 보기

| 항목 | 내용 |
|------|------|
| 목적 | 노드 그래프로 검사 레시피를 구성 → 3D 센서 데이터에서 측정값 추출 (반복성 테스트 등) |
| 아키텍처 | **C++ 알고리즘 엔진** + **Electron/React UI**, 둘은 **WebSocket**(`ws://localhost:9000`)으로 통신 |
| 핵심 워크플로우 | `ZMapLoader → PlaneFit(평면 피팅) → HeightFromPlane(평면 대비 수직거리 측정)` |

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
- **stb_image / stb_image_write** — PNG 입출력 (ZMap 로딩, 미리보기 인코딩)
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
│   │   ├── VisionData.h     # Image2D / PointCloud3D / ZMap / PlaneModel 컨테이너
│   │   ├── ZMap.h           # 높이맵 (mm 좌표 변환 포함)
│   │   ├── IAlgorithmTool.h # 모든 툴의 인터페이스 (execute)
│   │   └── Pipeline.h
│   └── src/
├── VisionTools/            # 알고리즘 구현 (정적 라이브러리)
│   ├── include/ · src/
│   │   ├── NoiseFilter         # 2D 가우시안 / 3D 이웃수 필터
│   │   ├── EdgeDetector        # Sobel / Canny
│   │   ├── LineFitHeightMeasure# 직선/평면 기준 높이 측정
│   │   ├── PlaneFitTool        # 평면 피팅 (LS / RANSAC / SVD)
│   │   ├── HeightFromPlaneTool # 평면 대비 수직거리 측정
│   │   └── ThicknessMeasure    # 두께 측정
├── VisionEngine/           # WebSocket 서버 (실행 파일)
│   └── src/
│       ├── main.cpp         # Crow 서버, 파이프라인 실행, 결과 직렬화
│       ├── ToolFactory.cpp  # JSON 파라미터 → 툴 인스턴스 생성
│       └── ImageEncoder.h   # ZMap/이미지 → base64 PNG 미리보기
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

| 노드 | 입력 → 출력 | 기능 |
|------|------------|------|
| **ZMapLoader** | – → ZMap | PNG(16/8bit)를 ZMap으로 로드. `xResMm/yResMm/zResMm` 분해능 지정 |
| **ImageLoader** | – → Image2D | 일반 이미지 로드 |
| **NoiseFilter** | Any → Any | 2D 가우시안 / 3D 포인트 노이즈 제거 |
| **EdgeDetector** | Image2D → Image2D | Sobel / Canny 엣지 검출 |
| **LineFitHeight** | ZMap → ZMap | 2개 ref 영역으로 직선/평면 기준 잡고 높이 측정 |
| **PlaneFit** | ZMap → **PlaneZMap** | 여러 ref ROI로 평면 `z=ax+by+c` 피팅. 결과: 평면식·RMSE·기울기각·인라이어수. 평면을 출력에 첨부 |
| **HeightFromPlane** | **PlaneZMap** → PlaneZMap | 여러 measure ROI에서 Z 추출(Mean/Max/HighTail) → 평면까지 수직거리 측정 + tolerance 합부판정 |
| **ThicknessMeasure** | PointCloud3D → PointCloud3D | 두께 측정 + 공차 판정 |

**포트 타입**(`tools.ts`의 `PortType`): `ZMap` · `PlaneZMap` · `Image2D` · `PointCloud3D` · `Any`
연결 규칙: `Any`이거나 타입이 같으면 연결 허용. `PlaneFit→HeightFromPlane`은 `PlaneZMap`으로 강제 매칭.

**평면 전달 방식**: `PlaneFit`이 `VisionData::plane`(`PlaneModel`)에 피팅 결과를 실어 출력 → `HeightFromPlane`이 읽어 수직거리 계산.
수직거리 = `(z − (a·x + b·y + c)) / √(1 + a² + b²)` (부호 있음, mm)

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
