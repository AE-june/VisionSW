#pragma once
#include "VisionData.h"
#include <string>
#include <vector>
#include <cstddef>

namespace vision {

// 입력 포트의 선언 타입 + 배열여부. UI 레시피 메타에서 온다.
struct PortMeta {
    std::string type;        // "Region"/"HeightMap"/"Profile"/"Geometry"/"PointCloud3D"/"Line"/그 외
    bool        isArray = false;
};

// 브로드캐스트 축 길이 수집.
//  스칼라 선언(isArray=false) 포트가 받은 (type) 벡터 길이가 >1이면 축으로 채택.
//  배열 선언 포트·길이<=1·매핑 없는 타입은 축 아님.
std::vector<std::size_t> broadcastAxisLengths(
    const VisionData& in, const std::vector<PortMeta>& metas);

// i번째 원소로 슬라이스한 입력을 새로 만든다.
//  축이 된 포트(스칼라선언 + 해당 type 길이>1)만 원소 i 단일로 축소, 나머지는 통째 유지.
VisionDataPtr sliceBroadcastInput(
    const VisionData& in, std::size_t i, const std::vector<PortMeta>& metas);

} // namespace vision
