#include "JsonBridge.h"
#include "LineFitHeightMeasure.h"
#include "ThicknessMeasure.h"

namespace vision {

nlohmann::json toolResultToJson(const std::string& toolName, const ToolResult& result) {
    nlohmann::json j;
    j["event"] = "result";
    j["tool"]  = toolName;
    j["ok"]    = (result.status == ToolStatus::Ok);
    j["msg"]   = result.message;

    // Extract measurement values from well-known tool types
    if (toolName == "LineFitHeightMeasure" || toolName == "LineFitHeight") {
        // Result is stored in the output data's source comment.
        // We encode what we have; the tool stores last result internally.
        // The caller injects the concrete result via the 'extra' field.
    }
    if (toolName == "ThicknessMeasure") {
        // Same — caller may set extra fields.
    }

    return j;
}

} // namespace vision
