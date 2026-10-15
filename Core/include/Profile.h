#pragma once
// Profile — 1D 샘플 신호 (높이맵 단면). 1급 iconic 타입.
// z는 mm 직접 저장 — HeightMap의 raw count 인코딩을 쓰지 않는다.
// SoA 배치: 같은 인덱스 i가 한 샘플.
#include <vector>
#include <string>
#include <memory>
#include <cmath>

namespace vision {

struct Profile {
    std::vector<double> s;       // 경로 시작점부터의 호장(arc length), mm. 단조 증가. s[0]=0.
    std::vector<double> x, y;   // 샘플 위치 (소속 프레임 mm 좌표)
    std::vector<double> z;       // 샘플 높이, mm. NaN = 무효.
    std::string frameId;          // 소스 HeightMap의 frameId 그대로 복사
    std::string label;            // 다중 추출 시 식별용 (예: "row:120")

    std::size_t size()  const { return z.size(); }
    bool        empty() const { return z.empty(); }
    bool        valid(std::size_t i) const {
        return i < z.size() && !std::isnan(z[i]);
    }
};

using ProfilePtr = std::shared_ptr<Profile>;

} // namespace vision
