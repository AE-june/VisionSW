#include "CloudLoaderTool.h"
#include "Logger.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

namespace vision {

namespace {
std::string lowerExt(const std::string& path) {
    auto pos = path.find_last_of('.');
    std::string e = (pos == std::string::npos) ? "" : path.substr(pos + 1);
    for (auto& ch : e) ch = (char)std::tolower((unsigned char)ch);
    return e;
}

// .xyz 텍스트: 줄마다 "x y z"
bool loadXyz(std::istream& in, PointCloud3D& cloud) {
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Point3f p;
        if (ss >> p.x >> p.y >> p.z) cloud.points.push_back(p);
    }
    return true;
}

// .bin: 생 float32 x,y,z 연속
bool loadBin(std::istream& in, PointCloud3D& cloud) {
    Point3f p;
    while (in.read(reinterpret_cast<char*>(&p), sizeof(Point3f)))
        cloud.points.push_back(p);
    return true;
}

// .ply: 헤더 파싱 후 ascii 또는 binary_little_endian 본문
bool loadPly(std::istream& in, PointCloud3D& cloud) {
    std::string line;
    if (!std::getline(in, line) || line.rfind("ply", 0) != 0) return false;
    bool ascii = false, binLE = false;
    long vertexCount = 0;
    while (std::getline(in, line)) {
        if (line.rfind("format ascii", 0) == 0) ascii = true;
        else if (line.rfind("format binary_little_endian", 0) == 0) binLE = true;
        else if (line.rfind("element vertex", 0) == 0) {
            std::istringstream ss(line); std::string a, b; ss >> a >> b >> vertexCount;
        } else if (line.rfind("end_header", 0) == 0) break;
    }
    cloud.points.reserve((size_t)std::max(0L, vertexCount));
    if (ascii) {
        for (long i = 0; i < vertexCount && std::getline(in, line); ++i) {
            std::istringstream ss(line); Point3f p;
            if (ss >> p.x >> p.y >> p.z) cloud.points.push_back(p);
        }
    } else if (binLE) {
        Point3f p;
        for (long i = 0; i < vertexCount; ++i) {
            if (!in.read(reinterpret_cast<char*>(&p), sizeof(Point3f))) break;
            cloud.points.push_back(p);
        }
    } else {
        return false;   // 지원 안 하는 ply 포맷
    }
    return true;
}
} // namespace

ToolResult CloudLoaderTool::execute(VisionDataPtr /*input*/) {
    if (m_path.empty())
        return { ToolStatus::Fail, "CloudLoader: 파일 경로가 설정되지 않았습니다" };

    std::ifstream in(std::filesystem::u8path(m_path), std::ios::binary);
    if (!in)
        return { ToolStatus::Fail, "CloudLoader: 파일을 열 수 없습니다: " + m_path };

    auto cloud = std::make_shared<PointCloud3D>();
    cloud->frameId = std::filesystem::u8path(m_path).stem().u8string();

    const std::string ext = lowerExt(m_path);
    bool ok = false;
    if      (ext == "xyz") ok = loadXyz(in, *cloud);
    else if (ext == "bin") ok = loadBin(in, *cloud);
    else if (ext == "ply") ok = loadPly(in, *cloud);
    else return { ToolStatus::Fail, "CloudLoader: 지원 안 하는 확장자: ." + ext + " (ply/xyz/bin)" };

    if (!ok || cloud->empty())
        return { ToolStatus::Fail, "CloudLoader: 점을 읽지 못했습니다: " + m_path };

    auto out = std::make_shared<VisionData>();
    out->setCloud(cloud);
    out->sourceId = m_path;
    VISION_LOG_INFO("CloudLoader: {} points ← {}", cloud->points.size(), m_path);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
