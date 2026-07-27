#pragma once

#include "HeightMap.h"
#include <string>
#include <memory>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  IHeightMapLoader — HeightMap 파일 로딩 인터페이스
//
//  구현체:
//    RawBinaryHeightMapLoader  ← float 배열 바이너리 (현재 구현)
//    CsvHeightMapLoader        ← TODO
//    SmartRayHeightMapLoader   ← TODO (센서 SDK 연동)
// ─────────────────────────────────────────────────────────────────────
class IHeightMapLoader {
public:
    virtual ~IHeightMapLoader() = default;
    virtual HeightMapPtr load(const std::string& path,
                         float xResMm, float yResMm, float zResMm) = 0;
};

// ─────────────────────────────────────────────────────────────────────
//  RawBinaryHeightMapLoader
//
//  파일 포맷 (헤더 없음):
//    [int32 width][int32 height][float32 * width * height]
//    순서: row-major (row0col0, row0col1, ...)
//    NaN = 무효 픽셀
// ─────────────────────────────────────────────────────────────────────
class RawBinaryHeightMapLoader : public IHeightMapLoader {
public:
    HeightMapPtr load(const std::string& path,
                 float xResMm, float yResMm, float zResMm) override;
};

// ─────────────────────────────────────────────────────────────────────
//  HeightMapSaver  — RawBinary 포맷으로 저장 (테스트 데이터 생성용)
// ─────────────────────────────────────────────────────────────────────
bool saveHeightMapRaw(const HeightMap& map, const std::string& path);

} // namespace vision
