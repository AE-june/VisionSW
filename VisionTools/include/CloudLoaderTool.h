#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// 파일 → PointCloud3D. CloudSaver 출력과 대칭:
//   .bin : 생 float32 x,y,z 연속(12B/점)
//   .xyz : 텍스트 "x y z" 줄
//   .asc : 텍스트 "x y z [...]" 또는 "x,y,z[,...]" — 첫 3열만 사용
//   .ply : binary_little_endian 또는 ascii (property float x/y/z)
// 확장자로 포맷 자동 감지. 한글경로 u8path 보존.
class CloudLoaderTool : public IAlgorithmTool {
public:
    explicit CloudLoaderTool(std::string path) : m_path(std::move(path)) {}
    std::string name() const override { return "CloudLoader"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    std::string m_path;
};

} // namespace vision
