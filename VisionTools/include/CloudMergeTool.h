#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────
//  CloudMergeTool — 두 PointCloud3D를 하나로 합성.
//  입력 포트 0=Master(그대로 유지), 포트 1=Transform(4x4 행렬 적용 후 Master에 합침).
//  행렬: matrixPath 텍스트 파일에서 로드. 공백/개행 구분 숫자 16개(4행 x 4열, row-major).
//    R11 R12 R13 Tx
//    R21 R22 R23 Ty
//    R31 R32 R33 Tz
//    0   0   0   1
//  Transform의 각 점 P에 대해 P' = R·P + T 적용 (R=3x3 회전, T=3x1 이동). 마지막 행은 읽기만 하고 사용 안 함.
//  출력: PointCloud3D — Master.points + transformed(Transform.points)
// ─────────────────────────────────────────────
struct CloudMergeParams {
    std::string matrixPath;   // 4x4 변환 행렬 텍스트 파일 경로 (Transform 입력에 적용)
};

class CloudMergeTool : public IAlgorithmTool {
public:
    explicit CloudMergeTool(CloudMergeParams p = {}) : m_p(p) {}
    std::string name() const override { return "CloudMerge"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    CloudMergeParams m_p;
};

} // namespace vision
