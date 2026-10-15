#pragma once

#include "IAlgorithmTool.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace vision {

class ToolFactory {
public:
    // Create a tool instance from type string + JSON params.
    // Returns nullptr if type is unknown.
    // noPreview: 배치(폴더검사) 등에서 미리보기가 필요 없을 때 true.
    // ExposureMerge처럼 미리보기용 단계별 이미지를 별도로 만드는 툴은 이 값으로
    // 실제 출력에 쓸 단계만 계산해 메모리를 절약한다.
    static std::shared_ptr<IAlgorithmTool> create(
        const std::string& type,
        const nlohmann::json& params,
        bool noPreview = false);
};

} // namespace vision
