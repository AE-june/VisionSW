#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// 파일 → PointCloud3D. CloudSaver 출력과 대칭:
//   .bin : 생 float32 x,y,z 연속(12B/점)
//   .xyz : 텍스트 "x y z" 줄
//   .asc : .xyz와 동일한 텍스트 파서 사용 (추가 컬럼은 무시)
//   .pcd : .xyz와 동일한 텍스트 파서 사용(단순 "x y z" 라인 형식 — PCL 표준 헤더 아님)
//   .ply : binary_little_endian 또는 ascii (property float x/y/z)
// 확장자로 포맷 자동 감지. 한글경로 u8path 보존.
class CloudLoaderTool : public IAlgorithmTool {
public:
    explicit CloudLoaderTool(std::string path, bool swapXY = false)
        : m_path(std::move(path)), m_swapXY(swapXY) {}
    std::string name() const override { return "CloudLoader"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    std::string m_path;
    bool m_swapXY = false;   // true면 로드한 점의 x/y를 맞바꿈(예: Keyence — 스캔방향 Y, 레이저라인 X)
};

} // namespace vision
