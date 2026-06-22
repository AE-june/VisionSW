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
    static std::shared_ptr<IAlgorithmTool> create(
        const std::string& type,
        const nlohmann::json& params);
};

} // namespace vision
