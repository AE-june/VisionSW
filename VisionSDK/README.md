# VisionSDK — VisionSW 노드 SDK (C ABI DLL)

VisionSW의 **모든 노드를 외부 C++/C# 검사 프로그램에서 함수로 호출**하는 공유 라이브러리.
`ToolFactory`를 그대로 래핑하므로 팩토리가 아는 모든 노드가 자동 노출된다.

## 구성
- `include/vision_sdk.h` — 순수 C 인터페이스 (extern "C", `__declspec(dllexport)`).
- `src/vision_sdk.cpp` — 구현. `ToolFactory::create(type, paramsJson) → execute` 래핑 + 평탄 구조체 마샬링.
- `test/sdk_test.cpp` — C++ 스모크 테스트(`VisionSDKTest`).
- `examples/VisionSdk.cs` — C# P/Invoke 바인딩 + 예시.
- 빌드 산출물: `VisionSDK.dll`(+`VisionSDK.lib` import), 옆에 `opencv_world*.dll` 자동 복사.

## 노드 접근
1. **노드별 전용 함수** — 각 노드를 개별 함수로:
   `vsdk_zmap_load`, `vsdk_exposure_split`(ExposureMerge), `vsdk_exposure_merge`(ExposureMerge2),
   `vsdk_noise_filter`, `vsdk_gap_fill`, `vsdk_edge_detector`, `vsdk_align`, `vsdk_plane_fit`,
   `vsdk_zmap_to_cloud`, `vsdk_thickness`, `vsdk_height_measure`.
2. **제네릭** — 임의 노드: `vsdk_run(type, paramsJson, inZmap, inPlane, out)`.
   `type`은 UI 레시피의 노드 타입 문자열, `paramsJson`은 그 노드 params와 동일 스키마.

## 데이터 / 메모리 규약
- ZMap/Cloud/Plane/Heights는 평탄 구조체(`float*`/`double*`)로 주고받음. NaN=무효 픽셀.
- 입력 버퍼는 **호출자 소유**(SDK는 복사만). C#은 `GCHandle.Alloc(..., Pinned)`로 고정.
- 출력 `VsdkResult`의 버퍼는 **SDK가 malloc** → 사용 후 반드시 `vsdk_free_result()` 호출.
- 반환값/`status`: 0=OK, 1=FAIL, 2=SKIP, 3=BADARG. 실패 시 `msg`에 메시지.
- OpenCV/Eigen/STL은 DLL 내부 은닉 — ABI에 노출 안 됨(크로스 컴파일러 안전).

## 페이로드 매핑 (노드 → 결과 필드)
- ZMap→ZMap (ExposureMerge/Merge2, NoiseFilter, GapFill, Align, EdgeDetector, ZMapLoader): `result.zmap`
- PlaneFit: `result.plane`
- HeightMeasure: `result.heights` (영역별 평면 대비 높이)
- ZMapToCloud: `result.cloud`
- (참고) LineCenter는 점(RefPoint) 출력이라 현재 평탄 구조체 미매핑 — 후속 확장 대상.

## 빌드
루트에서 CMake 구성 시 `VisionSDK`/`VisionSDKTest` 타깃 생성:
```
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-windows -DOpenCV_DIR=D:/Libraries/opencv/build \
      -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VisionSDKTest
```
검증: `build/bin/Release/VisionSDKTest.exe` 실행 → `ALL OK`.

## 배포 시
`VisionSDK.dll` + `opencv_world*.dll`(빌드 시 옆에 복사됨)을 함께 배포. C#은 이 DLL들을
실행 폴더 또는 PATH에 두고 P/Invoke(`examples/VisionSdk.cs` 참고).
