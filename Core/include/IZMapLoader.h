#pragma once

#include "ZMap.h"
#include <string>
#include <memory>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  IZMapLoader — ZMap 파일 로딩 인터페이스
//
//  구현체:
//    RawBinaryZMapLoader  ← float 배열 바이너리 (현재 구현)
//    CsvZMapLoader        ← TODO
//    SmartRayZMapLoader   ← TODO (센서 SDK 연동)
// ─────────────────────────────────────────────────────────────────────
class IZMapLoader {
public:
    virtual ~IZMapLoader() = default;
    virtual ZMapPtr load(const std::string& path,
                         float xResMm, float yResMm, float zResMm) = 0;
};

// ─────────────────────────────────────────────────────────────────────
//  RawBinaryZMapLoader
//
//  파일 포맷 (헤더 없음):
//    [int32 width][int32 height][float32 * width * height]
//    순서: row-major (row0col0, row0col1, ...)
//    NaN = 무효 픽셀
// ─────────────────────────────────────────────────────────────────────
class RawBinaryZMapLoader : public IZMapLoader {
public:
    ZMapPtr load(const std::string& path,
                 float xResMm, float yResMm, float zResMm) override;
};

// ─────────────────────────────────────────────────────────────────────
//  ZMapSaver  — RawBinary 포맷으로 저장 (테스트 데이터 생성용)
// ─────────────────────────────────────────────────────────────────────
bool saveZMapRaw(const ZMap& map, const std::string& path);

} // namespace vision
