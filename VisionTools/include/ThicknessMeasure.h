#pragma once

#include "IAlgorithmTool.h"
#include <array>

namespace vision {

// ─────────────────────────────────────────────
//  ThicknessMeasure  (3D)
//  Measures surface thickness within an ROI
//  via plane fitting on top/bottom surfaces.
// ─────────────────────────────────────────────
class ThicknessMeasure : public IAlgorithmTool {
public:
    struct Roi {
        float xMin = 0.f, xMax = 100.f;
        float yMin = 0.f, yMax = 100.f;
    };

    struct Params {
        Roi   roi;
        float toleranceMm = 0.05f;   // ±tolerance for pass/fail
        float nominalMm   = 0.f;     // 0 = measure only, no pass/fail
    };

    struct MeasureResult {
        float thicknessMm = 0.f;
        float minMm       = 0.f;
        float maxMm       = 0.f;
        bool  pass        = false;
    };

    explicit ThicknessMeasure(Params params = {});

    std::string name() const override { return "ThicknessMeasure"; }
    ToolResult  execute(VisionDataPtr input) override;

    // Last measurement — readable after execute()
    const MeasureResult& lastResult() const { return m_lastResult; }
    const Params&        params()     const { return m_params; }
    void setParams(const Params& p)         { m_params = p; }

private:
    Params        m_params;
    MeasureResult m_lastResult;
};

} // namespace vision
