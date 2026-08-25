#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────
//  NotchMeasureV2Tool
//  검출 알고리즘은 NotchMeasureTool(V1)과 동일 — chunk 머지, 3차 다항식 강건 피팅으로
//  land 기준선 추출, notch 개구 검출(notchMinCols/notchMaxGapUm), flat/corner 바닥 탐색,
//  이웃 chunk 이동중앙값 안정화(무효 chunk도 채워 넣음)까지 V1 그대로 포팅.
//  단, 출력 스키마는 기존 V2 형식을 그대로 유지한다(하위 호환 — CsvWriter 출력 컬럼명,
//  UI의 NotchProfileChart, 외부 아티팩트 CSV 업로더 등이 이 라벨을 참조하기 때문).
//
//  축 관례: x=transport(스캔), y=lateral(횡), z=높이 — V1/CloudLoader와 동일.
//
//  출력: Profile[] — depth_left_um / depth_right_um / depth_combined_um (깊이, µm) +
//                    notch_floor_z_mm / land_left_z_mm / land_right_z_mm (절대 높이, mm)
//        PointCloud3D — land/floor로 분류된 원본 점만 필터링(V1과 동일 판정식)
// ─────────────────────────────────────────────
struct NotchMeasureV2Params {
    // 센서 기하 (mm)
    double lateralResMm    = 0.0063;  // y(lateral) 컬럼 binning — V1의 lateralPitchMm
    double transportResMm  = 0.008;   // x(transport) profile 그룹핑

    // 프로파일 머지 (V1과 동일)
    int    avgProfiles     = 1;       // 측정 전 합칠 연속 profile 수 (1=개별)
    std::string avgMethod  = "mean";  // 컬럼별 여러 profile 값을 합치는 방식: mean|median

    // land 기준선 — 3차 다항식 강건 피팅(이상치 반복 제거)
    int    landFitIters    = 4;

    // notch 개구 검출
    double notchTrigUm     = -150.0;  // land 기준선 대비 이 값(부호 있음, 보통 음수)보다 낮으면 notch 후보
    double notchMaxGapUm   = 50.0;    // notch 연속구간 판정 시 허용 gap (y방향, µm)
    int    notchMinCols    = 20;      // notch로 인정할 최소 연속 컬럼 수 — 너무 크면 profile이 통째로 무효 처리됨

    // 바닥 검출
    std::string method     = "flat";   // flat|corner
    std::string floorAgg   = "median"; // 바닥 창 안 집계: median|mean
    double floorWinUm      = 150.0;    // (flat) 바닥 탐색 창 폭(µm)
    int    floorMinPts     = 12;       // (flat) 창 안 최소 점 개수
    double floorSearchFrac = 1.0;      // (flat, V2 확장) 창 중심 후보를 notch 폭 가운데 이 비율로 제한. 1.0=V1과 동일(전체 범위)
    int    smoothCols      = 3;        // (corner) 기울기 계산 이동평균 폭
    double slopeDrop       = 0.35;     // (corner) 기울기 급감 판정 비율
    double cornerSearchUm  = 500.0;    // (corner) 코너 탐색 범위(µm)

    // land 대표값
    bool   landFlatFilter  = false;    // true=V1과 동일(|rel|<landTolUm인 평탄한 점만 사용) | false(기본)=평탄도 필터 없이 구간 내 전체 점을 landAgg로 집계
    double landTolUm       = 30.0;     // landFlatFilter=true일 때만 사용
    std::string landAgg    = "median"; // median(기본)=이상치에 강건, 평탄도 필터 없이도 안정적 | mean=V1과 동일(landFlatFilter=true와 조합 권장)
    double landMaxDistMm   = 0.0;      // notch 경계에서 이 거리(mm) 이내 점만 land로 참조. 0=제한 없음(전체 영역)

    // 기울기 기반 land 시작점(edge) 탐색 — 노치 경계에서 바깥으로 나가며 기울기 부호 변화 or 평탄 구간 첫 점
    double edgeSlopeTolUmPerMm = 30.0;  // 평탄 판정 임계 기울기 편차 (µm/mm). land 피팅 기울기와의 차이가 이 값 미만이면 평탄 구간
    int    edgeSlopeWindowPts  = 5;     // 기울기 계산 윈도우 크기 (점 수). W점 간격으로 기울기 산출해 노이즈 억제

    // 안정화 — 이웃 chunk(±floorStabilizeHalf) 이동중앙값으로 보정 + 무효 chunk 채우기 (V1 stabilize()와 동일)
    int    floorStabilizeHalf        = 25;
    double floorStabilizeCenterTolUm = 50.0;  // 바닥 중심위치(y) 보정 임계값(µm)
    double floorStabilizeZTolUm      = 60.0;  // 바닥 상대높이(µm) 보정 임계값

    // 출력 PointCloud3D — land/floor 판정 (V1과 동일 판정식)
    double floorTolUm      = 40.0;    // 바닥 점 판정: |상대높이 - floorZRel| < 이 값(µm)
    double landMarginMm    = 0.020;   // notch 안팎 판정 마진(mm)
};

class NotchMeasureV2Tool : public IAlgorithmTool {
public:
    explicit NotchMeasureV2Tool(NotchMeasureV2Params p = {}) : m_p(p) {}
    std::string name() const override { return "NotchMeasureV2"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    NotchMeasureV2Params m_p;
};

} // namespace vision
