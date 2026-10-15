#pragma once
#include "IAlgorithmTool.h"
#include <string>
#include <vector>

namespace vision {

// 기하 요소 정의 (point 추출 또는 line 피팅)
struct CaliperElementDef {
    double fromMm = 0, toMm = 0;      // 검색 구간 (0,0=전체)
    double zFromMm = 0, zToMm = 0;   // z(높이) 범위 (0,0 = 제한 없음)
    std::string type = "point";       // "point" | "line"
    // type="point" 전용 (ProfileFeatureTool 위임)
    std::string kind  = "edge";       // edge|ridge|valley|corner|maxZ|minZ|mean
    std::string dir   = "any";        // rising|falling|any (edge 전용)
    double threshold  = 0.05;
    int    smoothWindow = 3;
    int    nth = 0;
    // type="line": 추가 필드 없음 (fromMm/toMm 범위 최소제곱 피팅)
};

// element 조합 측정 정의
struct CaliperMeasurementDef {
    std::string combo  = "pp";   // pp|pl|ll|l|p (target 조합)
    std::string metric = "euclidean";
    // pp: euclidean|deltaS|deltaZ    pl: perpDist
    // ll: angle|offset               l: tilt|flatness    p: absS|absZ
    int refA = 0, refB = 1;      // element 인덱스 (단일 combo는 refA만 사용)
    double nominalMm = 0, plusMm = 0, minusMm = 0;
};

class ProfileCaliperTool : public IAlgorithmTool {
public:
    struct Params {
        int                                profileIndex = 0;  // 분석할 입력 프로파일(타일) 인덱스
        std::string                        nodeId;            // CaliperResultCache 키 (온디맨드 오버레이용)
        std::vector<CaliperElementDef>     elements;
        std::vector<CaliperMeasurementDef> measurements;
    };
    explicit ProfileCaliperTool(Params p = {});
    std::string name() const override { return "ProfileCaliper"; }
    ToolResult  execute(VisionDataPtr input) override;
private:
    Params m_params;
};

} // namespace vision
