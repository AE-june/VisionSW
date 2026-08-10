#pragma once
// HeightMapSidecar.h — 사이드카 JSON 메타 읽기/쓰기 (header-only)
// ⚠️ nlohmann 의존 없이 단순 수작업 직렬화. 포맷: 한 줄 한 필드 flat 객체.
// 파일 경로 규칙: "<imgPath>.meta.json"
//   WriteOnly 필드: xResMm, yResMm, zResMm, zZeroCount, originCol, originRow,
//                   width, height, channels, frameId
// ─────────────────────────────────────────────────────────────────────────────
#include "HeightMap.h"
#include <fstream>
#include <optional>
#include <string>

namespace vision {

struct HeightMapMeta {
    float xResMm     = 1.f;
    float yResMm     = 1.f;
    float zResMm     = 0.001f;
    float zZeroCount = 32768.f;
    float originCol  = 0.f;
    float originRow  = 0.f;
    int   width      = 0;
    int   height     = 0;
    int   channels   = 1;
    std::string frameId;
};

// imgPath + ".meta.json" 에 사이드카 쓰기
inline bool writeSidecar(const std::string& imgPath, const HeightMap& hm) {
    std::ofstream f(imgPath + ".meta.json");
    if (!f) return false;
    // 부동소수 정밀도 충분히 확보 (float ~7자리)
    f.precision(9);
    f << "{\n"
      << "  \"xResMm\": "     << hm.xResMm     << ",\n"
      << "  \"yResMm\": "     << hm.yResMm     << ",\n"
      << "  \"zResMm\": "     << hm.zResMm     << ",\n"
      << "  \"zZeroCount\": " << hm.zZeroCount << ",\n"
      << "  \"originCol\": "  << hm.originCol  << ",\n"
      << "  \"originRow\": "  << hm.originRow  << ",\n"
      << "  \"width\": "      << hm.width      << ",\n"
      << "  \"height\": "     << hm.height     << ",\n"
      << "  \"channels\": "   << hm.channels   << ",\n"
      << "  \"frameId\": \""  << hm.frameId    << "\"\n"
      << "}\n";
    return true;
}

// imgPath + ".meta.json" 읽기. 없거나 파싱 실패 시 nullopt.
inline std::optional<HeightMapMeta> readSidecar(const std::string& imgPath) {
    std::ifstream f(imgPath + ".meta.json");
    if (!f) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    HeightMapMeta m;

    // 단순 key:value 파서 (배열 없는 flat 객체 전용)
    auto findFloat = [&](const std::string& key, float& out) {
        auto p = content.find('"' + key + '"');
        if (p == std::string::npos) return;
        auto c = content.find(':', p);
        if (c == std::string::npos) return;
        try { out = std::stof(content.substr(c + 1)); } catch (...) {}
    };
    auto findInt = [&](const std::string& key, int& out) {
        auto p = content.find('"' + key + '"');
        if (p == std::string::npos) return;
        auto c = content.find(':', p);
        if (c == std::string::npos) return;
        try { out = std::stoi(content.substr(c + 1)); } catch (...) {}
    };
    auto findStr = [&](const std::string& key, std::string& out) {
        auto p = content.find('"' + key + '"');
        if (p == std::string::npos) return;
        auto c  = content.find(':', p);
        if (c == std::string::npos) return;
        auto q1 = content.find('"', c + 1);
        if (q1 == std::string::npos) return;
        auto q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) return;
        out = content.substr(q1 + 1, q2 - q1 - 1);
    };

    findFloat("xResMm",     m.xResMm);
    findFloat("yResMm",     m.yResMm);
    findFloat("zResMm",     m.zResMm);
    findFloat("zZeroCount", m.zZeroCount);
    findFloat("originCol",  m.originCol);
    findFloat("originRow",  m.originRow);
    findInt  ("width",      m.width);
    findInt  ("height",     m.height);
    findInt  ("channels",   m.channels);
    findStr  ("frameId",    m.frameId);

    return m;
}

// 로드된 HeightMap에 사이드카 메타 적용 (픽셀 데이터는 그대로, 메타만 덮어씀)
inline void applySidecar(HeightMap& hm, const HeightMapMeta& m) {
    hm.xResMm     = m.xResMm;
    hm.yResMm     = m.yResMm;
    hm.zResMm     = m.zResMm;
    hm.zZeroCount = m.zZeroCount;
    hm.originCol  = m.originCol;
    hm.originRow  = m.originRow;
    hm.frameId    = m.frameId;
}

} // namespace vision
