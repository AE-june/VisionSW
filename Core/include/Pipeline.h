#pragma once

#include "IAlgorithmTool.h"
#include <vector>
#include <memory>
#include <string>

namespace vision {

// ─────────────────────────────────────────────
//  Linear Pipeline  (순차 실행)
//
//  Phase 2 목표: DAG Pipeline으로 확장
//    - 노드 병렬 실행 (Topological Sort)
//    - 스레드 풀 연동
// ─────────────────────────────────────────────
class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    // Tool management
    void addTool(std::shared_ptr<IAlgorithmTool> tool);
    void removeTool(const std::string& toolName);
    void clearTools();

    // Execution
    VisionDataPtr run(VisionDataPtr input);

    // Introspection
    std::size_t toolCount() const;
    std::vector<std::string> toolNames() const;
    const std::vector<std::shared_ptr<IAlgorithmTool>>& tools() const;

private:
    std::vector<std::shared_ptr<IAlgorithmTool>> m_tools;
};

} // namespace vision
