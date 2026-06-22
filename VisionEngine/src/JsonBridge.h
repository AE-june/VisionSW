#pragma once

#include "IAlgorithmTool.h"
#include <nlohmann/json.hpp>
#include <string>

namespace vision {

// Serialize a ToolResult to a JSON event object.
// Returns: { "event": "result", "tool": <name>, "pass": bool, ... }
nlohmann::json toolResultToJson(const std::string& toolName, const ToolResult& result);

} // namespace vision
