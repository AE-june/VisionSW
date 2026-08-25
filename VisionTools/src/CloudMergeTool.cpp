#include "CloudMergeTool.h"
#include "Logger.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace vision {

namespace {
// 4x4 행렬 텍스트 파일 로드: 공백/개행 구분 숫자 16개(4x4, row-major).
// 위 3행(R 3x3 + T 3x1)만 사용, 마지막 행([0 0 0 1])은 읽되 사용 안 함.
bool loadMatrix4x4(const std::string& path, std::array<double, 12>& m) {
    std::ifstream in(std::filesystem::u8path(path));
    if (!in) return false;
    std::vector<double> vals;
    double v;
    while (in >> v) vals.push_back(v);
    if (vals.size() < 12) return false;
    for (int i = 0; i < 12; ++i) m[i] = vals[i];
    return true;
}
} // namespace

ToolResult CloudMergeTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "CloudMerge: 입력이 없습니다" };

    auto master    = input->inCloud(0);
    auto transform = input->inCloud(1);
    if (!master)    return { ToolStatus::Fail, "CloudMerge: Master(포트0) PointCloud3D 입력이 없습니다" };
    if (!transform) return { ToolStatus::Fail, "CloudMerge: Transform(포트1) PointCloud3D 입력이 없습니다" };
    if (m_p.matrixPath.empty())
        return { ToolStatus::Fail, "CloudMerge: 행렬 파일 경로가 설정되지 않았습니다" };

    std::array<double, 12> m{};
    if (!loadMatrix4x4(m_p.matrixPath, m))
        return { ToolStatus::Fail, "CloudMerge: 행렬 파일을 읽지 못했습니다: " + m_p.matrixPath };

    auto out = std::make_shared<PointCloud3D>();
    out->points.reserve(master->points.size() + transform->points.size());
    out->points.insert(out->points.end(), master->points.begin(), master->points.end());

    const double r11 = m[0], r12 = m[1], r13 = m[2], tx = m[3];
    const double r21 = m[4], r22 = m[5], r23 = m[6], ty = m[7];
    const double r31 = m[8], r32 = m[9], r33 = m[10], tz = m[11];
    for (const auto& p : transform->points) {
        Point3f q;
        q.x = (float)(r11 * p.x + r12 * p.y + r13 * p.z + tx);
        q.y = (float)(r21 * p.x + r22 * p.y + r23 * p.z + ty);
        q.z = (float)(r31 * p.x + r32 * p.y + r33 * p.z + tz);
        out->points.push_back(q);
    }
    out->frameId = master->frameId;

    auto o = std::make_shared<VisionData>();
    o->setCloud(out);
    o->sourceId = input->sourceId;
    VISION_LOG_INFO("CloudMerge: master {} + transform(변환) {} → {} points",
                     master->points.size(), transform->points.size(), out->points.size());
    return { ToolStatus::Ok, "", o };
}

} // namespace vision
