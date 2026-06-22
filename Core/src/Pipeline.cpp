#include "Pipeline.h"
#include "Logger.h"
#include <algorithm>
#include <stdexcept>

namespace vision {

void Pipeline::addTool(std::shared_ptr<IAlgorithmTool> tool) {
    if (!tool) throw std::invalid_argument("null tool");
    tool->onAdded();
    m_tools.push_back(std::move(tool));
    VISION_LOG_INFO("Tool added: {}", m_tools.back()->name());
}

void Pipeline::removeTool(const std::string& toolName) {
    auto it = std::find_if(m_tools.begin(), m_tools.end(),
        [&](const auto& t){ return t->name() == toolName; });
    if (it != m_tools.end()) {
        (*it)->onRemoved();
        m_tools.erase(it);
    }
}

void Pipeline::clearTools() {
    for (auto& t : m_tools) t->onRemoved();
    m_tools.clear();
}

VisionDataPtr Pipeline::run(VisionDataPtr input) {
    if (!input) return nullptr;

    VisionDataPtr current = input;
    for (auto& tool : m_tools) {
        VISION_LOG_DEBUG("Running tool: {}", tool->name());
        ToolResult result = tool->execute(current);

        if (result.status == ToolStatus::Fail) {
            VISION_LOG_ERROR("Tool '{}' failed: {}", tool->name(), result.message);
            return nullptr;
        }
        if (result.status == ToolStatus::Skip) {
            VISION_LOG_WARN("Tool '{}' skipped", tool->name());
            continue;
        }
        if (result.output) {
            current = result.output;
        }
    }
    return current;
}

std::size_t Pipeline::toolCount() const {
    return m_tools.size();
}

std::vector<std::string> Pipeline::toolNames() const {
    std::vector<std::string> names;
    names.reserve(m_tools.size());
    for (const auto& t : m_tools) names.push_back(t->name());
    return names;
}

const std::vector<std::shared_ptr<IAlgorithmTool>>& Pipeline::tools() const {
    return m_tools;
}

} // namespace vision
