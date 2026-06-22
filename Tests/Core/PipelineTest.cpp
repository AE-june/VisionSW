#include <gtest/gtest.h>
#include "Pipeline.h"
#include "IAlgorithmTool.h"

using namespace vision;

// ─── Minimal stub tool for pipeline tests ───────────────────────────────────
class PassThroughTool : public IAlgorithmTool {
public:
    std::string name() const override { return "PassThrough"; }
    ToolResult  execute(VisionDataPtr input) override {
        return { ToolStatus::Ok, "", input };
    }
};

class FailTool : public IAlgorithmTool {
public:
    std::string name() const override { return "FailTool"; }
    ToolResult  execute(VisionDataPtr) override {
        return { ToolStatus::Fail, "intentional failure" };
    }
};

// ─── Tests ──────────────────────────────────────────────────────────────────
TEST(PipelineTest, EmptyPipelineReturnsInput) {
    Pipeline p;
    auto data = std::make_shared<VisionData>();
    EXPECT_EQ(p.run(data), data);
}

TEST(PipelineTest, NullInputReturnsNull) {
    Pipeline p;
    EXPECT_EQ(p.run(nullptr), nullptr);
}

TEST(PipelineTest, AddAndCountTools) {
    Pipeline p;
    p.addTool(std::make_shared<PassThroughTool>());
    p.addTool(std::make_shared<PassThroughTool>());
    EXPECT_EQ(p.toolCount(), 2u);
}

TEST(PipelineTest, FailToolReturnsNull) {
    Pipeline p;
    p.addTool(std::make_shared<FailTool>());
    auto data = std::make_shared<VisionData>();
    EXPECT_EQ(p.run(data), nullptr);
}
