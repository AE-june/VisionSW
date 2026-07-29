# 3D 검사 툴 벤치마크 & 기본기능 Gap 분석

> 목적: "기존 검사툴을 벤치마킹해서 기본 기능부터" 만들기 위한 기준선(baseline) 정의.
> 조사일: 2026-07-29. 대상: Zebra Aurora Vision Studio 5.7.1, MVTec HALCON 24.05,
> Euresys Open eVision 26.6, LMI Gocator (firmware 4.4 / 6.x / GoPxL 1.5),
> iNSNEX InsWorks VDE (2025.03 출시).
> 조사는 벤더 공식 문서 기준. 미확인 항목은 §6에 별도 표기.
> 방향 문서는 [ARCHITECTURE_DIRECTION.md](./ARCHITECTURE_DIRECTION.md), 이 문서는 그 §7·§9를 실제 제품 카탈로그로 검증·교정한 것.

---

## 0. 요약 (4줄)

1. **높이맵을 1급 타입으로 삼은 설계 판단은 옳았다.** 조사한 5개 제품 중 4개가 높이맵을 1급으로 다룬다. Aurora는 `Surface`(격자 높이맵)를 point cloud보다 **권장 타입으로 명시**하고, eVision은 `ZMap`, Gocator는 Surface, InsWorks는 "Range 이미지"를 중심에 둔다. 전용 타입이 없는 건 HALCON뿐이다.
2. **가장 큰 공백은 "높이맵 위에서 도는 캘리퍼/게이지 툴"이다.** Aurora는 이 목적의 필터가 19개, Gocator는 Feature Point 14종 + Profile 툴 14개, InsWorks는 "3D 횡단면 툴"로 같은 일을 한다. VisionSW는 0개. 측정 툴로서의 기본기가 여기 있다.
3. **사용성의 핵심은 툴 개수가 아니라 3가지 구조적 개념이다** (§2-H) — **Fixture**(찾은 특징으로 좌표계를 만들고 하위 툴이 그 안에서 동작), **중첩 ToolBlock**(서브그래프를 하나의 툴처럼 재사용), **좌표계 트리**. Cognex VisionPro 계열 개념이고 InsWorks가 그대로 채택했다. VisionSW엔 셋 다 없다.
4. **2D를 따로 만들 필요는 거의 없다.** eVision은 3D 측정 툴을 **아예 만들지 않고**, 높이맵을 metric 보정된 그레이스케일 이미지로 만들어 기존 2D 툴셋(게이지·블롭·매칭)을 그대로 재사용한다. → VisionSW도 `Image2D`/`HeightMap` 통합(B1의 남은 범위)이 "증분 사항"이 아니라 **2D 커버리지 전략의 핵심**이다.

---

## 1. 경쟁 제품의 3D 데이터 모델 (구조적 차이)

| 제품 | 1급 3D 표현 | 높이맵 지위 | 메시 |
|---|---|---|---|
| **Aurora Vision** | `Surface`(격자 높이맵, Image와 동일 메모리 레이아웃 + XOffset/XScale/YOffset/YScale/ZScale/ZOffset), `Point3DGrid`(조직화 점군), `Point3D[]` | **1급 + 권장.** 문서가 명시: *"For most purposes the Surface objects should be preferred than Point3DGrid… Surface requires much less space"* | 없음 (필터 레퍼런스 전체에 메시 타입 부재) |
| **HALCON** | `object_model_3d` 핸들 하나로 점군·메시·프리미티브 다형 처리 | **전용 타입 없음.** X/Y/Z 3장 이미지 triple로 브리지(`xyz_to_object_model_3d`). Z만 있는 높이맵은 의미 없는 평범한 grayscale | 있음 (triangulate/simplify/fuse/convex_hull) |
| **Open eVision** | `EPointCloud`, `EMesh`, `EZMap8/16` | **1급.** *"projection of a point cloud or a mesh on a reference plane, with the distance coded as grayscale values"* + *"distortion free, with affine transformation from/to metric coordinate system"* | 있음 (`EMesh`) |
| **Gocator** | Profile(2D 단면) / Surface(높이맵) / Mesh(GoPxL) | **1급.** Surface 모드 + Height Map Color Scale | GoPxL에만 |
| **InsWorks VDE** | "Range 이미지"(높이맵) + 점군. 이미지 속성은 1~4채널 8/16bit로 문서화 | **1급.** 3D 툴셋 설명이 전부 *"Range 图像"* 기준 (필터·결손픽셀·횡단면) | 언급 없음 |
| **VisionSW (현재)** | `HeightMap`(N채널 float + 분해능), `PointCloud3D` | **1급** | 없음 |

**함의**
- VisionSW의 포지션은 **Aurora·eVision·InsWorks 계열**이고 HALCON 계열이 아니다. 벤치마크 대상을 이쪽으로 좁히는 게 맞다.
- 메시는 Aurora·InsWorks가 아예 없다 → **메시 없음은 결격이 아니다.** ARCHITECTURE_DIRECTION §3의 "메시 보류" 판단 유지.
- 좌표계 규약은 **3사가 각기 다른 방식으로 1급 관심사임을 드러낸다.** eVision은 **world / ZMap / pixel 3계**를 문서에 못박고 world→ZMap 변환을 rigid(거리 보존)로 규정. Aurora는 `SurfacePointToImageCoordinates`·`ImagePointToSurfaceCoordinates`·`SurfaceBoxToRegion`·`AlignRegionToSurfaceFormat` 등 2D↔3D 브리지 필터를 대량 제공. InsWorks는 **"공간 좌표 트리(空间坐标树)"**를 소프트웨어 장점으로 전면에 내세운다 — 평면 3계가 아니라 **프레임 트리** 구조. → "정리하면 좋은 것"이 아니라 설계 단계에서 못박아야 하는 항목. (커밋 `3575c7d`의 "좌표 원점 일관성" 이슈가 이 문제의 예고편)

---

## 2. Table-stakes 목록 (2개 이상 제품이 공통으로 가진 것)

교차 검증된 공통 기능. **이게 "기본 기능"의 정의다.**

### A. 데이터 변환·정규화
| 기능 | Aurora | eVision | Gocator |
|---|---|---|---|
| 점군 ⇄ 높이맵 변환 (참조평면·분해능·원점 명시 제어) | `MakeSurfaceFromImage`, `CreatePoint3DGridFromImage`, `ArrangePoint3DGrid` | `EPointCloudToZMapConverter` (`SetReferencePlane`/`SetOrigin`/`SetMapXYResolution`/`SetMapZResolution`) | Surface 생성 (uniform spacing) |
| 무효 픽셀 채우기 (hole/gap fill) | `ReplaceInvalidSurfacePoints` | `EnableFillMode`/`SetFillMode` | Gap Filling, Max Void Width |
| 유효 영역 마스크 | `SurfaceValidPointsRegion`, `Point3DGridValidPointsRegion` | — | Preserve Invalid |
| 리샘플 / 다운샘플 | `ResampleSurface`, `ReduceSurface`, `VoxelizePoint3DGrid` | `EGridDecimator`, `ERandomDecimator` | Decimation |
| 노이즈 제거 (이웃 통계 기준) | `SmoothSurface_Gauss/Mean`, `RemoveOutliersFromPoint3DGrid` | `EPointCloudFilter`, `EFilters::RemoveNoise` (mean+k·σ) | Surface Filter, Smoothing, Vibration Correction |

### B. 표면 공간 변환 (높이맵 자체를 조작)
| 기능 | Aurora | eVision | Gocator |
|---|---|---|---|
| **레벨링/평탄화** (피팅 평면 기준으로 높이맵 펴기) | `FlattenSurface`, `FlattenSurface_WithScalePreserving`, `SurfaceToPlaneDistanceImage` | ZMap Leveling | Tilt Correction (툴별 X/Y Angle) |
| 크롭 (박스 / Region / 평면 근접 / 이웃 근접) | `CropSurface`, `CropSurfaceToBox3D`, `CropSurfaceToRegion`, `CropSurfaceByPlaneProximity`, `CropSurfaceByNeighborsProximity` | `ESimpleCropper`, `ERectangularCropper`, `ESphericalCropper`, `EPlaneCropper` | Regions |
| 두 표면 차분 / 결합 | `SubtractSurfaces`, `JoinSurfaces`, `SplitSurfaceByPlane` | — | Surface Stitch |
| 강체 변환 (회전·이동) | `TransformPoint3DGrid`, `RotatePoint3DGrid`, `AlignPoint3DGridToPlane` | `E3DTransformMatrix`, `EAffineTransformer` | Surface Transform |
| 표면 법선 | `SurfaceNormalsImage` | Normals and Curvatures | — |
| 높이맵 3D 모폴로지 | `DilateSurfacePoints`/`Erode`/`Open`/`Close` | (2D 모폴로지 재사용) | — |

### C. 기하 피팅 & 메트롤로지
| 기능 | Aurora | eVision | HALCON | Gocator |
|---|---|---|---|---|
| 평면 피팅 (LS + robust/RANSAC) | `FitPlaneToSurface`, `_M` | `EPlaneFitter`(LS), `EPlaneFinder`(RANSAC) | `fit_primitives_object_model_3d` | Surface Plane |
| 선 / 세그먼트 피팅 (3D) | `FitLineToPoints3D`(`_M`/`_RANSAC`/`_LTE`), `FitSegmentToPoints3D` | — | — | Profile Line, Feature Create Line |
| 원 피팅 (3D) | `FitCircleToPoints3D`, `FitCircleToSurfaceHole` | — | — | Profile Circle, Surface Hole |
| 구 / 실린더 | — (Aurora 부재) | `ESphereFitter` | cylinder·sphere·plane | Surface Sphere, Ball Bar |
| 거리 / 각도 / 교점 연산 | `Geometry3D` 4개 그룹 (Distance/Angle/Intersection/Construction) | `E3DPlane.DistanceTo` | `intersect_plane_object_model_3d` | Feature Dimension, Feature Intersect |
| 전역 특징 (면적/체적/평탄도/BBox/무게중심) | `SurfaceArea`, `SurfaceVolume_Single/Double`, `SurfaceFlatness`, `SurfaceBoundingBox`, `SurfaceMassCenter` | Easy3DObject Area/Volume | `area_`/`volume_`/`smallest_bounding_box_object_model_3d` | Surface Volume, Flatness, Bounding Box |

### D. 높이맵 네이티브 캘리퍼 (**최대 공백 영역**)
| 기능 | Aurora | Gocator | eVision |
|---|---|---|---|
| 경로 따라 프로파일 추출 (단면) | `SurfaceProfileAlongPath`, `SurfaceSingleProfileAlongAxis`, `SurfaceMultipleProfilesAlongAxis`, `GetSurfacePath` | Surface Section (*"Profile measurement tools can be used on sections extracted from Surface data"*) | ZMap 자체가 정사영 → 2D 툴 |
| 1D 엣지 / 리지 / 스트라이프 검출 | `ScanSingleEdge3D`, `ScanMultipleEdges3D`, `ScanExactlyNEdges3D` × {Edges, Ridges, Stripes} = **9개** | Feature Points 14종: Max Z, Min Z, Min X, Max X, Average, Corner, Top/Bottom/Left/Right Corner, Rising Edge, Falling Edge, Any Edge, Median | EasyGauge (2D) |
| 검출점에 기하 피팅 | `FitSegmentToEdges3D/Ridges3D/Stripe3D`, `FitCircleToEdges3D/…`, `FitPathToEdges3D/…`, `MeasureObjectWidth3D` = **10개** | Fit Lines (1~2 fit area, 불연속 우회 가능) | — |
| 로컬 극값 | `SurfaceLocalMaxima`, `SurfaceLocalMinima`, `SurfaceMaximalPoint`, `SurfaceMinimalPoint`, `SurfaceMedian` | Feature Points에 포함 | — |

### E. 객체 단위 분할·검사
| 기능 | Aurora | eVision | Gocator |
|---|---|---|---|
| 높이맵 → 개별 객체 분할 | `SegmentSurface_Planes`, `SegmentSurface_PlanarCells` | `E3DObjectExtractor` → `E3DObject[]` | Part Detection, Surface Segmentation |
| **메트릭 기준 객체 필터링** | (부분적) | Length/Width/LocalHeight/ReferenceHeight/AspectRatio/Orientation/LocalTilt/ReferenceTilt/Area/Volume **범위 필터** | Decisions (Min/Max) |
| **로컬 base plane** (객체 주변 픽셀로 배경 추정) | — | `E3DObject.BasePlane` (주변 픽셀 피팅), local height vs reference height **분리 출력** | Reference Regions (*"the surface around the hole is not flat"* 케이스용) |
| 배열/컬렉션 순회 | 배열 네이티브 | 객체 리스트 | GoPxL **Array tools** |

### F. 정렬 / 포즈 / 골든 비교
| 기능 | Aurora | eVision | HALCON | Gocator |
|---|---|---|---|---|
| 강체 정렬 (rough → refine, registration) | `AdjustPointGrids3D`, `AdjustPointGrids3DGlobal` | `EFeaturesAligner`, `EPrincipalAxisExtractor`(PCA) | `register_object_model_3d_pair`/`_global` | Part Matching, Surface Align Wide |
| CAD/골든 기준 포즈 | — | `E3DAligner` (참조 점군 **또는 CAD 메시**) | `find_surface_model` 등 5종 | Mesh Template Matching |
| **앵커링** (로케이터 결과로 하위 툴 위치 이동) | (수동 변환) | 변환행렬 선적용 | **Anchor X/Y/Z 입력이 거의 모든 툴에 존재** | — |
| 골든 샘플 편차 비교 | `GoldenTemplate3D`, `Point3DGridDistance`, `Point3DGridRMSE` | `E3DComparer` | `distance_object_model_3d` | Profile Master Comparison |

### G. 공통 인프라
| 기능 | 근거 |
|---|---|
| 합/불 판정을 툴에 내장 (Min/Max threshold → pass/fail) | Gocator Decisions (디지털 출력까지 라우팅), Aurora 판정은 별도 |
| 측정값 필터 (Scale/Offset, Hold Last Valid, Smoothing) | Gocator Filters |
| 3D 뷰어 | Aurora, eVision `E3DViewer`, Gocator, HALCON `render_object_model_3d`, InsWorks 자체 2D/3D 표시 엔진 |
| 스크립트 탈출구 | Gocator Script 툴 + GDK, HALCON 전체, Aurora 사용자 필터, InsWorks C#/Python 스크립트 + VS 디버깅 |

### H. InsWorks VDE — 툴 그룹 단위 대조 + VisionPro 계열 개념

InsWorks는 개별 필터 레퍼런스를 공개하지 않으므로 §2 A~G의 필터 단위 표에 넣을 수 없다. 대신 **툴 그룹 단위**로 대조한다.

**공개된 3D 툴셋 = 7개 그룹** (영문 데이터시트와 중문 제품페이지가 일치)

| InsWorks 3D 툴 그룹 | 설명 (벤더 문서) | §2 / §4 대응 |
|---|---|---|
| 3D 횡단면(Cross-sectional) 툴 | *"Range 이미지 X-Y 평면의 지정 영역 수직 컨투어를 획득하고 컨투어 특징 추출·측정"* — 예시로 corner extraction / length / area 제시 | **D 전체** (T1-1·T1-2·T1-3) |
| 3D 필터 툴 | Range 이미지 노이즈 제거 | A (VisionSW `NoiseFilter` 보유) |
| 3D 결손 픽셀 채우기 툴 | Range 이미지 무효 픽셀 채우기 | A (VisionSW `GapFill` 보유) |
| 3D 점군 매칭 툴 | 3D 공간 위치결정 | F (T3-2) |
| 3D 표면 결함 검출 툴 | *"3D 물체의 평면 또는 곡면상 돌출·함몰 검출"*, Train/Runtime 구조 + `BasePlane` 기준 | E·F (T2-3 + T3-3) |
| 3D 멀티카메라 캘리브레이션 툴 | 2대 이상 카메라 정합 | 보류 (멀티센서) |
| 3D 점군 스티칭 툴 | Range 이미지/점군 병합 | 보류 (멀티센서) |

**데이터시트에 이름이 노출된 3D 툴**: `3DMutlCameraCalibration`, `3D ImageFile`, `3DPointcloudStitching`, `Fixture`, `CreateCylinder`, `CreateSphere`, `DiffReferBasePlane`, `LineSphereIntersect`, `Ins3DBeadTrackerTool`
→ `DiffReferBasePlane`은 **T0-4(레벨링/평면거리맵)** 그 자체, `Ins3DBeadTrackerTool`은 Gocator Surface Track과 같은 비드(도포) 검사 툴.

**2D 툴 중 이름 노출분**: `Blob`, `Caliper`, `FindCircle`, `ColorSegment`, `ID`, `HDR`, `Affine Transform Tool`, `SearchMax`, `IPTwoImageMinMax`, `2DCalibCheckerBoard`, `2DNPointCalibration`, `CreateCircle`/`CreateEllipse`, `CreateLineParallel`/`CreateLineBisectPoints`, `AngleLineLine`/`AnglePointPoint`, `DistanceSegmentLine`/`DistanceSegmentSegment`/`DistanceCirleCirle`/`DistanceSegmentCircle`, `IntersectCircleCirlce`
→ **거리·각도·교점·기하 구성 툴이 2D 쪽에 대량으로 있다.** VisionSW의 T1-5(기하 연산)가 왜 기본기인지 보여주는 또 하나의 근거.

#### VisionPro 계열 3가지 개념 (← 이 벤치마크의 가장 실용적인 수확)

`ToolBlock` · `Fixture` · `Blob` · `Caliper` 는 Cognex VisionPro의 용어다. InsWorks VDE는 **VisionPro 아키텍처를 그대로 채택하고 3D를 얹은 형태**이고, 그 과정에서 Aurora/eVision/Gocator 조사만으로는 안 보였던 세 개념이 드러난다. 셋 다 툴이 아니라 **구조**이고, 셋 다 VisionSW에 없다.

| 개념 | InsWorks 근거 | 왜 중요한가 | VisionSW |
|---|---|---|---|
| **Fixture** | 데이터시트 3D 툴 목록에 `Fixture` 존재 | VisionPro 방식: 로케이터가 찾은 특징으로 **좌표계를 만들고** 하위 툴이 그 좌표계 안에서 동작. 이미지/데이터를 변환하지 않아 보간 손실·비용이 없음. Gocator의 Anchor X/Y/Z와 같은 목적, 더 일반적인 형태 | 없음. `Align`이 **데이터 자체를 변환**하는 방식 → T3-1 |
| **중첩 ToolBlock** | *"자유롭게 조합·중첩 가능한 툴 블록으로 복잡한 비전 작업을 빠르게 구성"*, *"nested tool blocks"* | 서브그래프를 하나의 툴처럼 캡슐화·재사용. 레시피가 커질 때 유일한 확장 수단이고, 툴 개수를 늘리는 것보다 조합력에 기여 | 없음 → **신규 T0-6** |
| **공간 좌표 트리** | 소프트웨어 장점 항목: *"자체 개발 2D/3D 그래픽 표시 기술, 공간 좌표 트리 지원"* | 좌표계를 평면 3계가 아니라 **부모-자식 트리**로 관리. Fixture가 프레임을 새로 만들면 그 아래에 붙는 구조 → Fixture의 전제 | 없음 → T0-1을 "3계"에서 "트리"로 확장 |

### I. 계산 이미징(Computational Imaging) — VisionSW가 이미 하고 있는 카테고리

InsWorks는 **"先进成像/계산 이미징 툴셋"을 4개 툴셋 중 하나로 전면에 배치**한다 (2D / 3D / AI / 계산 이미징).

| InsWorks 계산 이미징 툴 | 타 제품 |
|---|---|
| **HDR 고다이나믹 융합** | eVision·Aurora·Gocator 모두 이 카테고리를 별도 툴셋으로 두지 않음. Aurora 2D에 `HDR` 툴명만 확인 |
| **위상 편향(Phase Deflectometry) 2.5D** | 없음. 반사·투명 표면 전용 기법 |
| **광도 스테레오(Photometric Stereo) 2.5D** | eVision Easy3D, HALCON 보유 |
| **대심도 융합 (focus stacking)** | HALCON Depth From Focus |

**함의 두 가지**

1. **VisionSW의 `ExposureMerge`/`ExposureMerge2`/`ExposureMerge3`/`ExposureMergeCloud` + 리플렉션 제거는 "필터"가 아니라 계산 이미징 툴셋이다.** 현재 UI 카테고리가 `필터`로 묶여 있는데, 업계 분류상 독립 카테고리다. 툴박스 개편 시 분리 권장.
2. **반사·투명 표면 니치는 이미 경쟁 중이다.** InsWorks의 타깃 산업 목록 첫 번째가 **光伏&玻璃(태양광·유리)**이고, 위상 편향은 정확히 그 용도다. "어려운 표면"을 차별점으로 삼으려면 이 사실을 전제로 해야 한다.

---

## 3. VisionSW 현재 상태 (23개 노드)

`ui/src/renderer/src/types/tools.ts` 기준. PortType: `HeightMap` · `Region` · `Plane` · `Heights` · `Image2D` · `PointCloud3D` · `Point` · `Any`

| 카테고리 | 노드 |
|---|---|
| 입력 | HeightMapLoader, ImageLoader |
| 필터 | ExposureMerge(Split), ExposureMerge2, ExposureMerge3, RowStretch, NoiseFilter, GapFill, EdgeDetector |
| 분할 | Threshold(→Region), CreateROI(→Region) |
| 변환 | ReduceDomain, HeightMapToCloud, ExposureMergeCloud |
| 측정 | RegionMeasure, PlaneFit, RefHeight, HeightMeasure, ThicknessMeasure |
| 정렬 | LineCenter(→Point), Align |
| 출력 | CsvWriter, ImageSaver, CloudSaver |

**ARCHITECTURE_DIRECTION.md 대비 실제 진척 (문서가 뒤처져 있음)**
- B2(Region 1급화): 문서상 "신규" → **실제로는 상당 부분 구현됨.** `Region` 포트 + `Threshold` + `CreateROI` + `ReduceDomain` + `RegionMeasure` 존재.
- B4(Geometry 1급화): 문서상 "신규" → **부분 구현.** `Plane`·`Point` 포트가 이미 분리돼 있음 (kind 태그 통합형은 아니고 개별 포트 방식).
- B3(iconic/control 분리): `Heights` 포트가 control 역할로 분리 시작됨.
- → 문서 §7 상태 표기를 갱신할 필요가 있다.

---

## 4. Gap 분석 — 무엇이 없는가

### 🔴 Tier 0 — 기반 (없으면 나머지가 쌓이지 않음)

| # | 항목 | 근거 | 현재 |
|---|---|---|---|
| T0-1 | **좌표계 규약 명문화 — 프레임 트리** (world / heightmap / pixel + Fixture가 만드는 파생 프레임) + 변환 API. rigid 보장, 원점·분해능·단위 일원화 | eVision 3계 문서 규정, Aurora 브리지 필터군, **InsWorks "공간 좌표 트리"** | 암묵적. `3575c7d`에서 원점 불일치 버그 발생 |
| T0-2 | **컬렉션 타입 + ForEach** (`Region[]`, `Geometry[]`, `Array`) | Gocator Array tools, eVision 객체 리스트, Aurora 배열 네이티브 | 없음 (문서 F1) |
| T0-3 | **Image2D / HeightMap 통합 완결** — 높이맵을 2D 툴이 그대로 먹게 | eVision 전략의 근간(*"All Open eVision 2D processing are available on ZMaps"*) | B1 절반. `EdgeDetector`만 Image2D 전용으로 고립 |
| T0-4 | **레벨링 노드** (피팅 평면 기준으로 높이맵 평탄화 / 평면거리맵 출력) | Aurora `FlattenSurface`·`SurfaceToPlaneDistanceImage`, eVision ZMap Leveling, Gocator Tilt Correction, **InsWorks `DiffReferBasePlane`** | 없음. `PlaneFit`이 평면을 내지만 그걸로 맵을 펴는 노드가 없음 |
| T0-5 | 표면 기본 변환: Crop / Resample / Subtract / ValidPointsRegion | 4사 전부 | 없음 |
| T0-6 | **중첩 서브그래프 (ToolBlock)** — 노드 묶음을 하나의 툴로 캡슐화·재사용·중첩 | InsWorks 중첩 ToolBlock (VisionPro 계열), 레시피 규모 확장의 유일한 수단 | 없음 |

### 🟠 Tier 1 — 측정 코어 (가장 큰 차이, 여기가 "검사 툴"의 본체)

| # | 항목 | 근거 | 현재 |
|---|---|---|---|
| T1-1 | **프로파일 추출** (경로/축 따라 단면 → `Profile`) | Aurora 4필터, Gocator Surface Section, **InsWorks 3D 횡단면 툴** | 없음. 문서 B5는 "수요 시점"으로 미뤄져 있으나 **table-stakes로 격상 필요** |
| T1-2 | **1D 특징점 검출** — Edge(rising/falling/any) · Ridge · Stripe · Corner · Max/Min Z · Median | Aurora 9필터, Gocator Feature Points 14종, InsWorks 횡단면 corner extraction | 없음 |
| T1-3 | **검출점 → 기하 피팅** — FitLine / FitCircle / FitSegment / MeasureWidth | Aurora 10필터, Gocator Fit Lines, InsWorks 횡단면 length/area 측정 | 없음 |
| T1-4 | **Line3D / Circle3D 피팅** (Plane은 있음) | Aurora, Gocator, HALCON, InsWorks | `PlaneFit`만 존재 |
| T1-5 | **기하 연산** — 거리 / 각도 / 교점 / 구성 | Aurora `Geometry3D` 4그룹, **InsWorks 2D 툴 이름 노출분의 절반 이상이 이것** (Distance*/Angle*/Intersect*/Create*) | 없음 |
| T1-6 | **전역 측정** — Area / Volume / Flatness | 3사 전부 | 없음 (`HeightMeasure`는 ROI 높이만) |
| T1-7 | **Compare → Decision** (공칭±공차 → 합/불), 판정 결합 | Gocator Decisions | `HeightMeasure`·`ThicknessMeasure`에 tolerance가 하드코딩. 독립 노드 없음 |

### 🟡 Tier 2 — 객체 단위 검사

| # | 항목 | 근거 | 현재 |
|---|---|---|---|
| T2-1 | **ConnectedComponents** → `Region[]` | eVision `E3DObjectExtractor`, Gocator Segmentation | 없음 |
| T2-2 | **메트릭 객체 필터** — Length/Width/Height/Area/Volume/AspectRatio/Orientation/Tilt 범위 | eVision 10개 range 필터가 사실상 표준 스펙 | 없음 |
| T2-3 | **로컬 base plane** — 객체 주변 픽셀로 배경 평면 추정 → local height / reference height 분리 | eVision `E3DObject.BasePlane`, Gocator Reference Regions, **InsWorks 표면결함검출 툴의 `BasePlane`** | 없음. `PlaneFit`/`RefHeight`는 전역 ROI 기반 |
| T2-4 | 3D 모폴로지 (높이맵 dilate/erode/open/close), Region Boolean(∪∩−) | Aurora `DilateSurfacePoints` 등 | 없음 |

### 🟢 Tier 3 — 정렬 / 앵커링 / 골든 비교

| # | 항목 | 근거 | 현재 |
|---|---|---|---|
| T3-1 | **Fixture / 앵커링** — 로케이터 결과로 좌표계를 만들고 하위 툴이 그 안에서 동작 (또는 Anchor X/Y/Z 주입) | **InsWorks `Fixture`** + Gocator Anchor X/Y/Z. 데이터를 변환하지 않아 보간 손실·비용 없음. 3사 중 2사가 이 패턴 | `Align`이 데이터를 변환하는 방식. Fixture/앵커 개념 없음 |
| T3-2 | 강체 정렬 / registration (PCA · 특징점 대응 · 반복 정밀화) | Aurora `AdjustPointGrids3D`, eVision `EFeaturesAligner`/PCA, HALCON `register_*`, InsWorks 점군 매칭 툴 | `Align`(Point 1개 기반)만 |
| T3-3 | 골든 샘플 편차 비교 (거리맵 / RMSE, Train/Runtime 구조) | Aurora `GoldenTemplate3D`, eVision `E3DComparer`, Gocator Master Comparison, **InsWorks 3D 표면결함검출(Train/Runtime)** | 없음 |
| T3-4 | 노이즈 제거 이웃통계 기준 통일 (mean+k·σ) | eVision 명문화 | `NoiseFilter`에 `stdRatio` 존재 — 부분적 |

### ⬜ 보류 / 스킵 (근거: 3사 중 1사만 보유하거나 애플리케이션 특화)

- **애플리케이션 특화 툴** — Hole, Stud, Countersunk Hole, Opening, Groove, Panel Gap&Flush, Ball Bar, Track, Ellipse (Gocator), Bead Tracker (InsWorks). 기본기 아님. 필요한 응용이 생기면 개별 추가.
- **표면 거칠기** Sa/Sq/Sz 등 (Gocator만, 문서상 confocal 센서 전제)
- **메시** (Aurora·InsWorks 부재 → 결격 아님)
- **실린더 / 구 피팅** — HALCON·eVision·InsWorks(`CreateCylinder`/`CreateSphere`/`LineSphereIntersect`)가 보유하고 Aurora만 부재. **다수가 가진 기능이지만 후순위**: 단방향 레이저 삼각측량 높이맵은 원통·구의 뒷면이 없어 피팅 조건이 나쁘다. 평면·선·원이 기본선.
- **곡률 기반 분할** (HALCON만)
- **3D 재구성** — 스테레오 / sheet-of-light 캘리브레이션 / structured light (HALCON·eVision 보유, Aurora 부재). 센서 독립 툴에서는 센서가 이미 높이맵을 주므로 불필요.
- **계산 이미징 확장** — 위상 편향(phase deflectometry), 광도 스테레오, 대심도 융합. InsWorks가 4개 툴셋 중 하나로 배치하고 HALCON·eVision도 일부 보유하지만, VisionSW는 이미 **다중노출 머지 계열을 보유**하고 있으므로 이 카테고리는 "없음"이 아니라 **분류를 바로잡을 것**(§2-I). 추가 기법은 수요 발생 시.
- **멀티센서 스티칭 / 캘리브레이션 / 융합** (Gocator·eVision·InsWorks 보유. 수요 발생 시)
- **6DoF 포즈 / bin picking / 그리핑 포인트** (HALCON·Gocator, 검사 아닌 로보틱스 용도)
- **AI / 딥러닝 툴셋** — InsWorks ESAI(8종 태스크), Aurora, HALCON 전부 보유. 1인 프로젝트 범위 밖이고 기본기 아님.

---

## 5. 권장 구현 순서

기존 동작 보존이 원칙(회귀검증). ARCHITECTURE_DIRECTION §7 순서를 이 벤치마크로 교정한 결과:

```
1) T0-1 좌표계 프레임 트리   ← 나중에 고치면 전부 다시 손봐야 함. 최우선. T3-1의 전제.
2) T0-3 Image2D/HeightMap 통합 완결 + T0-4 레벨링 + T0-5 표면 기본변환
3) T1-1 Profile 추출 → T1-2 특징점 검출 → T1-3 검출점 피팅   ← 측정 툴의 본체
4) T1-4 Line/Circle 피팅 + T1-5 기하연산 + T1-6 Area/Volume/Flatness
5) T0-2 컬렉션+ForEach → T2-1 ConnectedComponents → T2-2 메트릭 필터
6) T1-7 Compare/Decision (판정을 툴에서 분리)
7) T2-3 로컬 base plane
8) T3-1 Fixture → T3-2 정렬 → T3-3 골든 비교
9) T0-6 중첩 ToolBlock       ← 툴이 늘어나 레시피가 커진 뒤가 적기. 단, T0-1과 설계 정합 필요.
```

**§7 원안과 달라진 점**
- `B5`(Profile)가 "수요 시점"에서 **3순위로 격상** — 캘리퍼 툴 전체가 Profile 위에 서기 때문. 4개 제품이 전부 보유.
- `B1` 잔여분(Image2D 흡수)이 "증분"에서 **2순위로 격상** — eVision 전략상 2D 커버리지의 전제.
- 좌표계 정리가 신규 최우선 항목으로 추가. InsWorks의 "공간 좌표 트리"를 참고해 **평면 3계가 아니라 프레임 트리**로 설계.
- `A1`~`A3`(로더 narrow-waist, ToolFactory 분해)는 위 항목들과 병행. 툴이 늘어나기 전에 하면 이득이 크다.
- **T3-1(Fixture)** 신규 — 원안에 없던 항목. InsWorks·Gocator 2사가 이 패턴이고, 데이터를 변환하지 않아 보간 손실이 없다. 정렬 파트 재설계 시 반영.
- **T0-6(중첩 ToolBlock)** 신규 — 조합력을 툴 개수 대신 구조로 확보하는 수단.
- **툴박스 카테고리 개편**: `ExposureMerge*` 계열을 `필터`에서 **`계산 이미징`으로 분리** (§2-I). 코드 변경 없이 분류만 바꾸는 일이라 저비용.

---

## 6. 미확인 / 주의 사항

정확성을 위해 확인 못 한 것을 명시한다.

- **Gocator firmware 6.x 통합 툴 목록**은 LMI 공식 PDF의 제3자 미러(manualslib)에서 추출. 내용은 LMI 것이지만 호스트가 비공식. 개별 툴(Surface Roughness, Surface Track, Surface Align Wide, Mesh Template Matching)은 `am.lmi3d.com` 1차 출처로 교차 확인함.
- **Gocator Surface 툴 중 미확인**: Surface Arithmetic, Blob, Circular Edge, Cylinder, Mask, Merge, Pattern Matching — 검색 요약에는 반복 등장하나 LMI 1차 출처에서 확인 실패. 존재한다고 단정하지 않음.
- **Aurora의 부재 주장**(메시 타입 없음, 모델 기반 3D 위치인식 없음, 실린더/구 피팅 없음, 재구성 카테고리 없음)은 필터 레퍼런스 최상위 카테고리 목록 + 6개 3D 그룹 전수를 근거로 함. `Camera Calibration`·`Deep Learning`·`OpenCV` 그룹은 전수 확인하지 않았으므로 그 안에 숨어 있을 가능성은 배제 못 함.
- **HALCON에 높이맵 전용 타입이 없다**는 서술은 오퍼레이터 집합에서 추론한 것. MVTec이 한 문장으로 그렇게 명시한 문서는 찾지 못함(오퍼레이터 근거는 직접적).
- **HALCON 3D Transformations 장 TOC**·Multi-View/Photometric Stereo/Structured Light/Depth-From-Focus 하위 오퍼레이터 목록은 미확인.
- Gocator는 센서 패밀리(G2 line profile / G3 snapshot / G5 line confocal)별로 툴 가용성이 다름. 본 문서는 line profile 기준.

**InsWorks 관련 (주의 필요)**

- **개별 툴 레퍼런스가 공개되지 않음.** 확보한 자료는 영문 제품 데이터시트(2024.10.24판, 유통사 Soda Vision 호스팅)와 중문 제품 페이지뿐이다. 그래서 §2 A~G 필터 단위 표에 넣지 못하고 §2-H에서 그룹 단위로만 대조했다. 위 InsWorks 관련 서술은 **전부 벤더 마케팅 자료 기준**이며 기술 문서로 검증한 것이 아니다.
- **툴 개수가 자료마다 불일치.** 영문 데이터시트 "60+ 2D / 35+ 3D", 중문 홈페이지 "70+ 2D / 35+ 3D / 7 AI", VDE 제품페이지 "116+ (2D+3D+AI+계산이미징)". 마케팅 수치로 취급할 것.
- **VisionPro 계열이라는 해석은 추론이다.** `ToolBlock`·`Fixture`·`Blob`·`Caliper` 용어 일치와 중첩 툴블록 구조에 근거하지만, iNSNEX가 VisionPro를 참조했다고 밝힌 문서는 없다. `Fixture` 툴의 실제 동작(좌표계 생성 방식인지 데이터 변환 방식인지)도 **확인 못 했다** — VisionPro 관례를 따랐다고 가정한 것이다.
- **시장 채택 데이터 없음.** 점유율·설치 대수·독립 리뷰를 중/영/한국어로 검색했으나 벤더 자체 자료 외에는 확인 실패. InsWorks VDE/ESAI는 **2025년 3월 정식 출시**로 조사 시점 기준 약 1년 4개월 된 신제품이다. 회사는 2017년 설립(쑤저우), 원래 3D+AI 비전 시스템·카메라 사업. 즉 **성숙도·채택도로는 Aurora/HALCON/eVision/Gocator와 같은 급으로 취급할 근거가 없고**, 이 문서에서는 "설계 참조"로만 쓴다.
- **중국 내수 시장의 주력은 InsWorks가 아닐 가능성이 높다.** Hikrobot **VisionMaster**가 배포 규모로는 훨씬 크다고 보이나(알고리즘 툴 1,000개 이상 주장 — 출처가 벤더 홍보·Baidu Baike라 신뢰도 낮음), 3D 툴 카탈로그를 공식 문서로 공개하지 않아 이 벤치마크에 포함하지 못했다. **후속 조사 대상.**

---

## 7. 출처

**Aurora Vision Studio** — [Surface 데이터타입](https://docs.adaptive-vision.com/current/studio/datatypes/Surface.html) · [Point3DGrid](https://docs.adaptive-vision.com/current/studio/datatypes/Point3DGrid.html) · [Filter Reference](https://docs.adaptive-vision.com/current/studio/filters/FilterReference.html) · [Surface 필터군](https://docs.adaptive-vision.com/current/studio/filters/Surface/index.html) · [Point3DGrid 필터군](https://docs.adaptive-vision.com/current/studio/filters/Point3DGrid/index.html) · [Geometry3D](https://docs.adaptive-vision.com/current/studio/filters/Geometry3D/index.html) · [Segmentation3D](https://docs.adaptive-vision.com/current/studio/filters/Segmentation3D/index.html) · [1D Edge Detection 3D](https://docs.adaptive-vision.com/current/studio/filters/1DEdgeDetection3D/index.html) · [Shape Fitting 3D](https://docs.adaptive-vision.com/current/studio/filters/ShapeFitting3D/index.html) · [3D 데이터 다루기](https://docs.adaptive-vision.com/current/studio/user_interface/WorkingWith3DData.html)

**HALCON 24.05** — [3D Object Model](https://www.mvtec.com/doc/halcon/2405/en/toc_3dobjectmodel.html) · [Features](https://www.mvtec.com/doc/halcon/2405/en/toc_3dobjectmodel_features.html) · [Segmentation](https://www.mvtec.com/doc/halcon/2405/en/toc_3dobjectmodel_segmentation.html) · [Transformations](https://www.mvtec.com/doc/halcon/2405/en/toc_3dobjectmodel_transformations.html) · [3D Matching](https://www.mvtec.com/doc/halcon/2405/en/toc_3dmatching.html) · [Surface-Based Matching](https://www.mvtec.com/doc/halcon/2405/en/toc_3dmatching_surfacebased.html) · [3D Reconstruction](https://www.mvtec.com/doc/halcon/2405/en/toc_3dreconstruction.html) · [Sheet of Light](https://www.mvtec.com/doc/halcon/2405/en/toc_3dreconstruction_sheetoflight.html)

**Open eVision 26.6** — [ZMap 생성](https://documentation.euresys.com/Products/OPEN_EVISION/OPEN_EVISION/en-us/Content/03_Using/6_3D_Processing/1_Easy3D/3_ZMap/Generating_a_ZMap.htm) · [좌표 관리](https://documentation.euresys.com/Products/Open_eVision/Open_eVision_2_5/en-us/Content/03_Using_Open_eVision/F2_Using_3D_Toolset__Easy3D_/3_ZMap/Managing_the_Coordinates.htm) · [3D Viewer](https://documentation.euresys.com/Products/OPEN_EVISION/OPEN_EVISION/en-us/Content/03_Using/6_3D_Processing/1_Easy3D/3D_Viewer.htm) · [Easy3DObject Object Features](https://documentation.euresys.com/Products/OPEN_EVISION/OPEN_EVISION/en-us/Content/03_Using/6_3D_Processing/4_Easy3DObject/Object_Features.htm) · [Easy3DObject 제품페이지](https://www.euresys.com/en/products/software/easy3dobject/) · [Easy3DMatch 제품페이지](https://www.euresys.com/en/products/software/easy3dmatch/) · [3D Processing Tools 사용자 가이드 PDF](https://documentation.euresys.com/Products/OPEN_EVISION/OPEN_EVISION_23_08/en-us/Content/11_Pdf/D134ET-Using_3D_Processing_Tools_.NET-Open_eVision-23.08.1.1187.pdf)

**iNSNEX InsWorks** — [InsWorks 공식 사이트](https://insworks.insnex.com/) · [VDE 제품 페이지 (중문, 툴셋 목록)](https://insworks.insnex.com/InsWorks/5n04ipQ83NaF.html) · [ESAI 제품 페이지](https://insworks.insnex.com/InsWorks/pJaiflHfjf8s.html) · [VDE-3D 영문 데이터시트 PDF (2024.10.24판)](https://www.sodavision.com/wp-content/uploads/2025/03/2024.10.24-insworks-vde-en-v2-web.pdf) · [InsWorks VDE (Soda Vision)](https://www.sodavision.com/product/insnex-insworks-vde/) · [InsWorks ESAI (Soda Vision)](https://www.sodavision.com/product/insnex-insworks-esai/) · [바이렉스-INSNEX 국내 대리점 계약 (헬로티)](https://www.hellot.net/news/article.html?no=99704)

**후속 조사 대상** — [Hikrobot VisionMaster](https://www.hikrobotics.com/en/machinevision/visionmaster/)

**Gocator** — [Measure 펌웨어 페이지](https://lmi3d.com/firmware/measure/) · [4.4 사용자 매뉴얼 PDF](https://lmi3d.com/wp-content/uploads/2016-08/15159-4.4.4.40_MANUAL_User_Gocator-2100-2300-2880-Series.pdf) · [측정툴 알고리즘 기술 매뉴얼 PDF](https://lmi3d.com/wp-content/uploads/2020-02/15201-5.2.29.3_MANUAL_Technical_Gocator_Measurement_Tool.pdf) · [GoPxL 툴 목록](https://am.lmi3d.com/manuals/gopxl/gopxl-1.5/en-US/LMILaserLineProfiler/Content/Inspect_toolRelated/Inspect_Tools.htm) · [Surface Roughness](https://am.lmi3d.com/manuals/gopxl/gopxl-1.5/en-US/LMILaserLineProfiler/Content/Inspect_toolTopics/SurfaceRoughness.htm) · [Surface Track](https://am.lmi3d.com/manuals/gopxl/gopxl-1.0/G5/Content/Inspect_toolTopics/SurfaceTrack.htm) · [Surface Align Wide](https://am.lmi3d.com/manuals/gopxl/gopxl-1.0/G2/Content/Inspect_toolTopics/SurfaceAlignWide.htm) · [Mesh Template Matching](https://am.lmi3d.com/manuals/gopxl/gopxl-1.0/G2/Content/Inspect_toolTopics/MeshTemplateMatching.htm)
