#pragma once
#include <string>
#include <vector>
#include <array>

namespace vision {

struct Measurement {
    std::string name;
    double      value = 0;
    std::string unit;
    bool        valid = true;
    int         elemIndex = -1;   // 브로드캐스트 원소 인덱스. -1=단일(없음). UI 드롭다운·CSV 열 구분용.
};

struct Decision {
    std::string name;
    bool        pass      = true;
    std::string reason;
    double      measured  = 0;
    double      nominal   = 0;
    double      tolerance = 0;
};

struct Overlay {
    enum class Kind { Cloud, Lines } kind = Kind::Cloud;
    std::vector<std::array<double, 3>> cloudPoints;  // {x_mm, y_mm, z_mm}
    struct LineData {
        double cx = 0, cy = 0, cxMm = 0, cyMm = 0, angleDeg = 0;
        int    roiIndex = 0, pointCount = 0;
        // 검출 라인 끝점 (px). 둘 다 0이면 미지정 → center+angle로 그림.
        double p0x = 0, p0y = 0, p1x = 0, p1y = 0;
    };
    std::vector<LineData> lines;
};

} // namespace vision
