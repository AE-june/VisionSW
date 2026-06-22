#pragma once

#include "Pipeline.h"
#include <string>
#include <memory>

namespace vision {

// ─────────────────────────────────────────────
//  Serializer interface (Recipe 저장/불러오기)
//
//  구현체:
//    JsonPipelineSerializer  ← 현재 구현 대상
//    XmlPipelineSerializer   ← TODO
//    BinaryPipelineSerializer← TODO (성능 필요 시)
// ─────────────────────────────────────────────
class IPipelineSerializer {
public:
    virtual ~IPipelineSerializer() = default;

    virtual bool save(const Pipeline& pipeline, const std::string& path) = 0;
    virtual bool load(Pipeline& pipeline,       const std::string& path) = 0;
};

// ─────────────────────────────────────────────
//  JSON 구현체 (nlohmann_json 예정)
// ─────────────────────────────────────────────
class JsonPipelineSerializer : public IPipelineSerializer {
public:
    bool save(const Pipeline& pipeline, const std::string& path) override;
    bool load(Pipeline& pipeline,       const std::string& path) override;
};

} // namespace vision
