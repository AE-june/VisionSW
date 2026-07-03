#include "JsonBridge.h"
#include "ThicknessMeasure.h"

namespace vision {

nlohmann::json toolResultToJson(const std::string& toolName, const ToolResult& result) {
    nlohmann::json j;
    j["event"] = "result";
    j["tool"]  = toolName;
    j["ok"]    = (result.status == ToolStatus::Ok);
    j["msg"]   = result.message;

    // Extract measurement values from well-known tool types
    if (toolName == "ThicknessMeasure") {
        // Same — caller may set extra fields.
    }

    return j;
}

} // namespace vision
