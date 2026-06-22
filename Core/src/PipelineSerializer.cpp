#include "IPipelineSerializer.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace vision {

bool JsonPipelineSerializer::save(const Pipeline& pipeline, const std::string& path) {
    VISION_LOG_INFO("Saving pipeline recipe to: {}", path);

    nlohmann::json j;
    j["version"] = "1.0";
    j["tools"] = nlohmann::json::array();

    for (const auto& tool : pipeline.tools()) {
        j["tools"].push_back({ {"type", tool->name()} });
    }

    std::ofstream f(path);
    if (!f) {
        VISION_LOG_ERROR("Cannot open file for writing: {}", path);
        return false;
    }
    f << j.dump(2);
    return f.good();
}

bool JsonPipelineSerializer::load(Pipeline& pipeline, const std::string& path) {
    // Full parameter deserialization requires a ToolFactory.
    // Use VisionEngine's recipe API (cmd: run with nodes array) instead.
    VISION_LOG_WARN("JsonPipelineSerializer::load is not implemented. "
                    "Use VisionEngine WebSocket API to load recipes.");
    (void)pipeline;
    (void)path;
    return false;
}

} // namespace vision
