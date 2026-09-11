# VisionSW — 개요

## 목적

3D 높이맵(HeightMap) 기반 산업 검사용 노드 그래프 툴.  
SmartRay ECCO 스캐너 출력을 받아 측정·판정·저장 파이프라인을 시각적으로 구성한다.

## 기술 스택

| 계층 | 기술 |
|------|------|
| UI | Electron + Vite + React + TypeScript |
| 엔진 | C++17, WebSocket(Crow), OpenCV, Eigen |
| 통신 | WebSocket JSON (포트 9000) |
| 빌드 | CMake + vcpkg (x64-windows) |

## 폴더 구조

```
arch_develop/
  Core/           HeightMap, VisionData, Frame, Region, Profile 타입
  VisionEngine/   main.cpp — WebSocket 서버 + 파이프라인 실행
  VisionTools/    개별 알고리즘 툴 (IAlgorithmTool 구현체)
  Tests/          GoogleTest — CoreTests / ToolsTests
  ui/             Electron-Vite 프론트엔드
  docs/           설계 문서
```

## 빌드 & 실행

```powershell
# C++ 엔진 빌드 (첫 번째 또는 변경 후)
cmake --build D:\GitHub\VisionSWTool\arch_develop\build --config Release --target VisionEngine

# UI 개발 모드 (엔진 자동 실행)
cd D:\GitHub\VisionSWTool\arch_develop\ui
npm run dev
```

## 현재 노드 카탈로그 (47개)

### 입력
| 타입 | 설명 |
|------|------|
| HeightMapLoader | 파일/폴더에서 HeightMap 로드 |
| CloudLoader | 포인트클라우드(ply/xyz/pcd 등) 로드 |

### SDC 전용
| 타입 | 설명 |
|------|------|
| ExposureMerge | 저/장노출 HeightMap 머지 + 리플렉션 제거 |
| ExposureMerge3 | 저/중/장노출 3단 캐스케이드 머지 |
| ExposureFilter | 인터리브 3노출 대칭 일관성 필터 |
| ExposureSplit | 인터리브 HeightMap → 노출별 분리 |
| RowStretch | Region 행을 보간 업샘플 |
| LineCenter | 스캔 방향 엣지 검출 → Point |
| Align | Point 기준 HeightMap X/Y 정렬 |
| PointCloudSplit | 인터리브 PointCloud → 노출별 분리 |

### 분할
| 타입 | 설명 |
|------|------|
| Threshold | 임계값으로 HeightMap → Region |
| ValidRegion | 유효(non-NaN) 픽셀 → Region |
| CreateROI | 사각/원/폴리곤 ROI → Region[] |

### 변환
| 타입 | 설명 |
|------|------|
| Level | Plane 기준 평탄화 또는 수직거리 |
| SurfaceCrop | 사각형/Region으로 HeightMap 크롭 |
| SurfaceResample | HeightMap 다운샘플 |
| SurfaceSubtract | HeightMap A − B (mm) |
| ReduceDomain | Region union으로 처리 범위 제한 |
| HeightMapToCloud | HeightMap → PointCloud3D |
| CloudToHeightMap | PointCloud3D → HeightMap (top/bottom/mean) |
| CloudSelect | PointCloud 배열에서 인덱스 선택 |
| CloudZReduce | 같은 (x,y) 다중 Z → reduce 1개 |
| CloudToProfiles | PointCloud → Profile[] (bin별) |
| ProfileToCloud | Profile[] → PointCloud3D |
| ExtractProfile | HeightMap 단면 → Profile (axisX/Y/line) |

### 필터
| 타입 | 설명 |
|------|------|
| NoiseFilter | median/bilateral/gaussian/sor 필터 |
| GapFill | NaN 픽셀 이웃값/IDW 보간 |
| PointCloudSOR | 통계적 이상점 제거 |

### 측정
| 타입 | 설명 |
|------|------|
| PlaneFit | Region 영역 평면 피팅 → Plane |
| LineFit | 엣지/능선 검출 → Line |
| RegionMeasure | Region 면적·무게중심·Z집계·체적·평탄도 |
| ProfileFeature | Profile 집계/엣지/능선/골/코너 측정 |
| NotchMeasure | 배터리 캔캡 노치 깊이 (V1) |
| NotchMeasureV2 | 배터리 캔캡 노치 깊이 (V2) |
| GeometryMeasure | 점/선/원/평면 간 기하 측정 |

### 영역 분석
| 타입 | 설명 |
|------|------|
| ConnectedComponents | 연결 성분 라벨링 → Region[] |
| RegionFilter | Region[] 면적/기하 기준 필터 |
| RegionSelect | Region[] 인덱스 선택 |
| RegionBoolean | Region AND/OR/NOT 연산 |
| RegionMorphology | Region 팽창/침식/열기/닫기 |
| ScalarMath | 측정값 간 산술 연산 |

### 판정
| 타입 | 설명 |
|------|------|
| Compare | 측정값 → 판정 (tolerance/range/max/min) |
| CombineDecision | 판정 결합 (all/any/count) |

### 축약
| 타입 | 설명 |
|------|------|
| Collect | 복수 노드 측정값·판정 수집 |

### 출력
| 타입 | 설명 |
|------|------|
| CsvWriter | Measurements/Profile[] → CSV |
| HeightMapSaver | HeightMap 파일 저장 + 메타 사이드카 |
| CloudSaver | PointCloud3D → PLY 등 저장 |

## 새 노드 추가 절차

1. `VisionTools/include/XxxTool.h` — params struct + `IAlgorithmTool` 상속 선언
2. `VisionTools/src/XxxTool.cpp` — `execute()` 구현 (`in(port)` 패턴, `VisionData` 결과 반환)
3. `VisionTools/CMakeLists.txt` — 소스 파일 추가
4. `VisionEngine/src/ToolFactory.cpp` — `if (type == "Xxx")` 블록 추가
5. `ui/src/renderer/src/types/tools.ts` — `TOOL_DEFS`에 타입·포트·파라미터 추가
6. `Tests/Tools/XxxTest.cpp` + `Tests/CMakeLists.txt` — 단위 테스트 추가
