#include "JsonBridge.h"

namespace vision {

nlohmann::json toolResultToJson(const std::string& toolName, const ToolResult& result) {
    nlohmann::json j;
    j["event"] = "result";
    j["tool"]  = toolName;
    j["ok"]    = (result.status == ToolStatus::Ok);
    j["msg"]   = result.message;
    return j;
}

} // namespace vision
