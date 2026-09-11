#pragma once
#include "IAlgorithmTool.h"
#include <string>
#include <vector>

namespace vision {

// 단일 피처 검출 정의
struct CaliperFeatureDef {
    std::string kind        = "edge";  // edge|ridge|valley|corner
    std::string dir         = "any";   // rising|falling|any (edge 전용)
    double threshold        = 0.05;    // 검출 임계값 (mm)
    int    smoothWindow     = 3;
    double searchFromMm     = 0;       // 검색 구간 s 시작 (0,0=전체)
    double searchToMm       = 0;       // 검색 구간 s 끝
    int    nth              = 0;       // n번째 검출 (0-based, 음수=뒤에서)
};

// 라인피팅 세그먼트 정의 (z = slope*s + intercept)
struct CaliperLineFitDef {
    double fromMm = 0;
    double toMm   = 0;
};

// 두 피처 간 거리 정의
struct CaliperDistanceDef {
    int    from      = 0;        // 피처 인덱스
    int    to        = 1;        // 피처 인덱스
    std::string mode = "deltaS"; // deltaS|deltaZ|euclidean
    double nominalMm = 0;
    double plusMm    = 0;
    double minusMm   = 0;
};

class ProfileCaliperTool : public IAlgorithmTool {
public:
    struct Params {
        std::vector<CaliperFeatureDef>  features;
        std::vector<CaliperLineFitDef>  lineFits;
        std::vector<CaliperDistanceDef> distances;
    };

    explicit ProfileCaliperTool(Params p = {});
    std::string name() const override { return "ProfileCaliper"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    Params m_params;
};

} // namespace vision
