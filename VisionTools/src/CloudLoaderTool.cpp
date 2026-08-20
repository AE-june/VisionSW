#include "CloudLoaderTool.h"
#include "Logger.h"
#include <cstdint>
#include <cstring>
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

// .asc 텍스트: 줄마다 "x y z [...]" 또는 "x,y,z[,...]". 첫 3열만 사용.
bool loadAsc(std::istream& in, PointCloud3D& cloud) {
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == '/' || line[0] == '!') continue;
        // 쉼표를 공백으로 교체하여 통일
        for (auto& ch : line) if (ch == ',') ch = ' ';
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

int plyTypeSize(const std::string& t) {
    if (t == "char" || t == "uchar" || t == "int8" || t == "uint8")   return 1;
    if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
    if (t == "int" || t == "uint" || t == "int32" || t == "uint32" ||
        t == "float" || t == "float32")                                 return 4;
    if (t == "double" || t == "float64")                                return 8;
    return 4;
}

// .ply: 프로퍼티 인식 파서. vertex 요소의 x/y/z만 추출, 나머지(rgb/intensity 등) 스킵.
//   binary_little_endian: vertex마다 stride 바이트 읽고 오프셋에서 x/y/z. ascii: 컬럼 인덱스로.
bool loadPly(std::istream& in, PointCloud3D& cloud) {
    std::string line;
    if (!std::getline(in, line) || line.rfind("ply", 0) != 0) return false;
    bool ascii = false, binLE = false;
    long vertexCount = 0;
    bool inVertex = false;
    int stride = 0, offX = -1, offY = -1, offZ = -1;   // 바이트 오프셋(바이너리)
    int ord = 0, idxX = -1, idxY = -1, idxZ = -1;      // 컬럼 인덱스(ascii)
    bool floatXYZ = true;                               // x/y/z 가 4바이트 float 인지

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("format ascii", 0) == 0) ascii = true;
        else if (line.rfind("format binary_little_endian", 0) == 0) binLE = true;
        else if (line.rfind("element vertex", 0) == 0) {
            std::istringstream ss(line); std::string a, b; ss >> a >> b >> vertexCount;
            inVertex = true;
        } else if (line.rfind("element", 0) == 0) {
            inVertex = false;   // 다른 요소(face 등) 시작 → vertex 프로퍼티 종료
        } else if (inVertex && line.rfind("property", 0) == 0) {
            std::istringstream ss(line); std::string kw, type, name; ss >> kw >> type >> name;
            if (type == "list") { ord++; continue; }   // list 프로퍼티(정점엔 드묾) 무시
            const int sz = plyTypeSize(type);
            if      (name == "x") { offX = stride; idxX = ord; if (sz != 4) floatXYZ = false; }
            else if (name == "y") { offY = stride; idxY = ord; if (sz != 4) floatXYZ = false; }
            else if (name == "z") { offZ = stride; idxZ = ord; if (sz != 4) floatXYZ = false; }
            stride += sz;
            ord++;
        } else if (line.rfind("end_header", 0) == 0) break;
    }
    if (offX < 0 || offY < 0 || offZ < 0) return false;
    cloud.points.reserve((size_t)std::max(0L, vertexCount));

    if (binLE) {
        if (!floatXYZ) return false;   // x/y/z 비-float은 미지원(현 데이터는 float)
        std::vector<char> buf((size_t)stride);
        for (long i = 0; i < vertexCount; ++i) {
            if (!in.read(buf.data(), stride)) break;
            Point3f p;
            std::memcpy(&p.x, buf.data() + offX, 4);
            std::memcpy(&p.y, buf.data() + offY, 4);
            std::memcpy(&p.z, buf.data() + offZ, 4);
            cloud.points.push_back(p);
        }
    } else if (ascii) {
        for (long i = 0; i < vertexCount && std::getline(in, line); ++i) {
            std::istringstream ss(line);
            std::vector<double> tok; double v;
            while (ss >> v) tok.push_back(v);
            const int mx = std::max({ idxX, idxY, idxZ });
            if ((int)tok.size() <= mx) continue;
            cloud.points.push_back({ (float)tok[idxX], (float)tok[idxY], (float)tok[idxZ] });
        }
    } else {
        return false;
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
    else if (ext == "asc") ok = loadAsc(in, *cloud);
    else if (ext == "bin") ok = loadBin(in, *cloud);
    else if (ext == "ply") ok = loadPly(in, *cloud);
    else return { ToolStatus::Fail, "CloudLoader: 지원 안 하는 확장자: ." + ext + " (ply/xyz/asc/bin)" };

    if (!ok || cloud->empty())
        return { ToolStatus::Fail, "CloudLoader: 점을 읽지 못했습니다: " + m_path };

    auto out = std::make_shared<VisionData>();
    out->setCloud(cloud);
    out->sourceId = m_path;
    VISION_LOG_INFO("CloudLoader: {} points ← {}", cloud->points.size(), m_path);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
