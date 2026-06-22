#pragma once

#include "VisionData.h"
#include <string>

namespace vision {

// Result of a single tool execution
enum class ToolStatus { Ok, Fail, Skip };

struct ToolResult {
    ToolStatus      status  = ToolStatus::Ok;
    std::string     message;
    VisionDataPtr   output;
};

// ─────────────────────────────────────────────
//  Base interface for every algorithm tool
//
//  Derive from this and implement:
//    - name()    : unique string ID ("NoiseFilter", "ThicknessMeasure", ...)
//    - execute() : core processing logic
// ─────────────────────────────────────────────
class IAlgorithmTool {
public:
    virtual ~IAlgorithmTool() = default;

    virtual std::string name() const = 0;
    virtual ToolResult  execute(VisionDataPtr input) = 0;

    // Optional lifecycle hooks
    virtual void onAdded()   {}
    virtual void onRemoved() {}
};

} // namespace vision
