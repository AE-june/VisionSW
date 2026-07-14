/* ============================================================================
 * VisionSDK — VisionSW의 모든 노드를 외부(C++/C#)에서 함수로 호출하는 C ABI.
 *
 *  - 순수 C 인터페이스(extern "C", __declspec)로 C++ name-mangling/STL을 노출하지
 *    않아 MSVC 컴파일러/런타임이 달라도 크로스랭귀지 호출 가능.
 *  - 데이터는 평탄 구조체(float* 등)로 주고받음. OpenCV/Eigen/STL은 DLL 내부 은닉.
 *  - 각 노드마다 전용 함수(vsdk_noise_filter 등) + 임의 노드용 제네릭(vsdk_run) 제공.
 *  - 파라미터는 JSON 문자열(노드별 params. UI 레시피의 params와 동일 스키마).
 *
 *  메모리 규약: 출력 VsdkResult의 버퍼는 SDK가 malloc. 사용 후 반드시
 *              vsdk_free_result()로 해제. 입력 버퍼는 호출자 소유(SDK는 복사만).
 * ==========================================================================*/
#ifndef VISION_SDK_H
#define VISION_SDK_H

#ifdef _WIN32
  #ifdef VISIONSDK_EXPORTS
    #define VSDK_API __declspec(dllexport)
  #else
    #define VSDK_API __declspec(dllimport)
  #endif
#else
  #define VSDK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 상태 코드 */
enum { VSDK_OK = 0, VSDK_FAIL = 1, VSDK_SKIP = 2, VSDK_BADARG = 3 };

/* 높이맵. data = width*height 개 float, row-major, NaN=무효 픽셀.
 * 좌표: x_mm=(col-originCol)*xResMm, y_mm=(row-originRow)*yResMm,
 *       z_mm=(rawcount-zZeroCount)*zResMm. */
typedef struct {
    int   width;
    int   height;
    float xResMm;
    float yResMm;
    float zResMm;
    float zZeroCount;
    float originCol;
    float originRow;
    float* data;        /* 길이 width*height */
} VsdkZMap;

/* 3D 포인트클라우드. xyz = count*3 float (x,y,z mm 반복). */
typedef struct {
    int    count;
    float* xyz;
} VsdkCloud;

/* 평면 z = a*x + b*y + c (mm). valid!=0 이면 유효. */
typedef struct {
    double a, b, c;
    int    valid;
} VsdkPlane;

/* 측정 높이 배열 (예: HeightMeasure의 영역별 평면 대비 높이). */
typedef struct {
    int     count;
    double* values;
} VsdkHeights;

/* 노드 실행 결과. 노드가 생성한 페이로드 필드만 채워짐(나머지는 0/NULL).
 * zmap.data / cloud.xyz / heights.values 가 NULL 이 아니면 그 페이로드가 유효. */
typedef struct {
    int         status;      /* VSDK_OK 등 */
    VsdkZMap    zmap;
    VsdkCloud   cloud;
    VsdkPlane   plane;
    VsdkHeights heights;
    char        msg[256];    /* 실패 시 메시지 */
} VsdkResult;

/* SDK 버전 문자열. */
VSDK_API const char* vsdk_version(void);

/* 결과 버퍼 해제. 모든 실행 함수 사용 후 호출. */
VSDK_API void vsdk_free_result(VsdkResult* r);

/* ── 제네릭: 임의 노드를 타입 문자열 + JSON 파라미터로 실행 ──────────────────
 *  type      : 노드 타입 (예 "NoiseFilter","ExposureMerge2","PlaneFit"...).
 *  paramsJson: 해당 노드 파라미터 JSON (없으면 NULL 또는 "{}").
 *  inZmap    : 입력 ZMap (로더 노드면 NULL 가능).
 *  inPlane   : 입력 평면 (HeightMeasure처럼 평면 입력이 필요한 노드용, 아니면 NULL).
 *  out       : 결과. 반환값 = status. */
VSDK_API int vsdk_run(const char* type, const char* paramsJson,
                      const VsdkZMap* inZmap, const VsdkPlane* inPlane,
                      VsdkResult* out);

/* ── 노드별 전용 함수 (각 노드를 개별 함수로 접근) ─────────────────────────── */
VSDK_API int vsdk_zmap_load(const char* path, float xResMm, float yResMm, float zResMm, VsdkResult* out);
VSDK_API int vsdk_exposure_split(const VsdkZMap* in, const char* paramsJson, VsdkResult* out);   /* ExposureMerge */
VSDK_API int vsdk_exposure_merge(const VsdkZMap* in, const char* paramsJson, VsdkResult* out);   /* ExposureMerge2 */
VSDK_API int vsdk_noise_filter (const VsdkZMap* in, const char* paramsJson, VsdkResult* out);
VSDK_API int vsdk_gap_fill     (const VsdkZMap* in, const char* paramsJson, VsdkResult* out);
VSDK_API int vsdk_edge_detector(const VsdkZMap* in, const char* paramsJson, VsdkResult* out);
VSDK_API int vsdk_align        (const VsdkZMap* in, const char* paramsJson, VsdkResult* out);
VSDK_API int vsdk_plane_fit    (const VsdkZMap* in, const char* paramsJson, VsdkResult* out);
VSDK_API int vsdk_zmap_to_cloud(const VsdkZMap* in, const char* paramsJson, VsdkResult* out);
VSDK_API int vsdk_thickness    (const VsdkZMap* in, const char* paramsJson, VsdkResult* out);
/* HeightMeasure: ZMap + (선택)평면 입력 → 영역별 높이. */
VSDK_API int vsdk_height_measure(const VsdkZMap* in, const VsdkPlane* plane, const char* paramsJson, VsdkResult* out);

/* 조직화된 point cloud 이중노출 머지 (per-point X 보존).
 *  xyz = numProfiles*width 개 점(x,y,z 3연속 float, row-major). 짝수 프로파일=저노출, 홀수=고노출.
 *  결과 = out->cloud (count=(numProfiles/2)*width, row-major 조직화, 무효셀=NaN점).
 *  paramsJson: matchTol/tolX/tolY/gapK (기본 strict 20/5/30/0). */
VSDK_API int vsdk_exposure_merge_cloud(const float* xyz, int width, int numProfiles,
                                       const char* paramsJson, VsdkResult* out);

#ifdef __cplusplus
}
#endif

#endif /* VISION_SDK_H */
