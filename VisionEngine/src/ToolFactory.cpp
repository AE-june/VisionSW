#include "ToolFactory.h"
#include "NoiseFilter.h"
#include "EdgeDetector.h"
#include "ThicknessMeasure.h"
#include "LineFitHeightMeasure.h"
#include "PlaneFitTool.h"
#include "HeightFromPlaneTool.h"
#include "CsvWriterTool.h"
#include "LineCenterTool.h"
#include "IZMapLoader.h"
#include "VisionData.h"
#include "ZMap.h"
#include "Logger.h"
#include <limits>

// stb for PNG/JPG loading
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace vision {

// ── Loader tools (defined here, used by ToolFactory) ─────────────────────

class ZMapLoaderTool : public IAlgorithmTool {
    std::string m_path;
    float m_xResMm, m_yResMm, m_zResMm;
public:
    ZMapLoaderTool(std::string path, float xRes, float yRes, float zRes)
        : m_path(std::move(path)), m_xResMm(xRes), m_yResMm(yRes), m_zResMm(zRes) {}
    std::string name() const override { return "ZMapLoader"; }

    ToolResult execute(VisionDataPtr) override {
        if (m_path.empty())
            return { ToolStatus::Fail, "ZMapLoader: 파일 경로가 설정되지 않았습니다" };

        // PNG → ZMap: 16비트 grayscale이면 그대로, 8비트면 0~255 → float
        int w, h, ch;
        // 먼저 16비트로 시도
        uint16_t* raw16 = stbi_load_16(m_path.c_str(), &w, &h, &ch, 1);
        if (raw16) {
            auto zmap = std::make_shared<ZMap>();
            zmap->width  = w;
            zmap->height = h;
            zmap->xResMm = m_xResMm;
            zmap->yResMm = m_yResMm;
            zmap->zResMm = m_zResMm;   // count당 mm (분해능 그대로 적용)
            zmap->data.resize(static_cast<size_t>(w) * h);
            for (int i = 0; i < w * h; ++i)
                zmap->data[i] = (raw16[i] == 0)
                    ? std::numeric_limits<float>::quiet_NaN()   // 0 = 미측정(배경) → 무효
                    : static_cast<float>(raw16[i]);
            stbi_image_free(raw16);

            auto data = std::make_shared<VisionData>();
            data->zmap     = zmap;
            data->sourceId = m_path;
            VISION_LOG_INFO("ZMapLoader: {}x{} 16bit PNG loaded from {}", w, h, m_path);
            return { ToolStatus::Ok, "", data };
        }

        // 8비트 fallback
        unsigned char* raw8 = stbi_load(m_path.c_str(), &w, &h, &ch, 1);
        if (!raw8)
            return { ToolStatus::Fail, "ZMapLoader: 파일을 읽을 수 없습니다: " + m_path };

        auto zmap = std::make_shared<ZMap>();
        zmap->width  = w;
        zmap->height = h;
        zmap->xResMm = m_xResMm;
        zmap->yResMm = m_yResMm;
        zmap->zResMm = m_zResMm;   // count당 mm (분해능 그대로 적용)
        zmap->data.resize(static_cast<size_t>(w) * h);
        for (int i = 0; i < w * h; ++i)
            zmap->data[i] = (raw8[i] == 0)
                ? std::numeric_limits<float>::quiet_NaN()   // 0 = 미측정(배경) → 무효
                : static_cast<float>(raw8[i]);
        stbi_image_free(raw8);

        auto data = std::make_shared<VisionData>();
        data->zmap     = zmap;
        data->sourceId = m_path;
        VISION_LOG_INFO("ZMapLoader: {}x{} 8bit PNG → ZMap from {}", w, h, m_path);
        return { ToolStatus::Ok, "", data };
    }
};

class ImageLoaderTool : public IAlgorithmTool {
    std::string m_path;
public:
    explicit ImageLoaderTool(std::string path) : m_path(std::move(path)) {}
    std::string name() const override { return "ImageLoader"; }

    ToolResult execute(VisionDataPtr) override {
        if (m_path.empty())
            return { ToolStatus::Fail, "ImageLoader: path not set" };
        int w, h, ch;
        unsigned char* raw = stbi_load(m_path.c_str(), &w, &h, &ch, 0);
        if (!raw)
            return { ToolStatus::Fail, "ImageLoader: cannot load " + m_path };

        auto img = std::make_shared<Image2D>();
        img->width    = w;
        img->height   = h;
        img->channels = ch;
        img->data.assign(raw, raw + static_cast<size_t>(w) * h * ch);
        stbi_image_free(raw);

        auto data = std::make_shared<VisionData>();
        data->image    = img;
        data->sourceId = m_path;
        return { ToolStatus::Ok, "", data };
    }
};

// ── Helpers ───────────────────────────────────────────────────────────────

static Rect2D roiFromJson(const nlohmann::json& j, const std::string& key) {
    Rect2D r;
    if (j.contains(key) && j[key].is_object()) {
        const auto& v = j[key];
        r.x = v.value("x", 0);
        r.y = v.value("y", 0);
        r.w = v.value("w", 0);
        r.h = v.value("h", 0);
    }
    return r;
}

// ── ToolFactory::create ───────────────────────────────────────────────────

std::shared_ptr<IAlgorithmTool> ToolFactory::create(
    const std::string& type,
    const nlohmann::json& p)
{
    if (type == "ZMapLoader") {
        return std::make_shared<ZMapLoaderTool>(
            p.value("path",    ""),
            p.value("xResMm",  1.0f),
            p.value("yResMm",  1.0f),
            p.value("zResMm",  0.001f));
    }
    if (type == "ImageLoader") {
        return std::make_shared<ImageLoaderTool>(p.value("path", ""));
    }
    if (type == "NoiseFilter") {
        NoiseFilter::Params params;
        params.kernelSize   = p.value("kernelSize",   3);
        params.radius       = p.value("radius",       1.0f);
        params.minNeighbors = p.value("minNeighbors", 5);
        return std::make_shared<NoiseFilter>(params);
    }
    if (type == "EdgeDetector") {
        EdgeDetector::Params params;
        std::string algo = p.value("algorithm", "Canny");
        params.algorithm   = (algo == "Sobel")
                             ? EdgeDetector::Algorithm::Sobel
                             : EdgeDetector::Algorithm::Canny;
        params.threshold1  = p.value("threshold1", 50.0f);
        params.threshold2  = p.value("threshold2", 150.0f);
        params.apertureSize = p.value("apertureSize", 3);
        return std::make_shared<EdgeDetector>(params);
    }
    if (type == "LineFitHeight") {
        LineFitParams params;
        params.roiFit1      = roiFromJson(p, "roiFit1");
        params.roiFit2      = roiFromJson(p, "roiFit2");
        params.roiMeasure   = roiFromJson(p, "roiMeasure");
        std::string agg = p.value("aggregation", "Max");
        if      (agg == "Mean")     params.aggregation = ZAggregation::Mean;
        else if (agg == "HighTail") params.aggregation = ZAggregation::HighTail;
        else                        params.aggregation = ZAggregation::Max;
        params.useRansac         = p.value("useRansac",         false);
        params.ransacIterations  = p.value("ransacIterations",  200);
        params.ransacThresholdMm = p.value("ransacThreshold",   0.05f);
        params.referenceMode     = ReferenceMode::Line;
        return std::make_shared<LineFitHeightMeasure>(params);
    }
    if (type == "PlaneFit") {
        PlaneFitParams params;

        // rois 배열: 모두 reference ROI (평면 피팅 전용), xPct/yPct/wPct/hPct
        if (p.contains("rois") && p["rois"].is_array()) {
            for (const auto& r : p["rois"]) {
                PlaneFitParams::ROI roi;
                roi.xPct = r.value("xPct", 0.f);
                roi.yPct = r.value("yPct", 0.f);
                roi.wPct = r.value("wPct", 1.f);
                roi.hPct = r.value("hPct", 1.f);
                params.refRois.push_back(roi);
            }
        }

        std::string algo = p.value("algorithm", "LeastSquares");
        if      (algo == "RANSAC") params.algorithm = PlaneFitParams::Algorithm::RANSAC;
        else if (algo == "SVD")    params.algorithm = PlaneFitParams::Algorithm::SVD;
        else                       params.algorithm = PlaneFitParams::Algorithm::LeastSquares;

        params.ransacThresholdMm = p.value("ransacThreshold",  0.05f);
        params.ransacIterations  = p.value("ransacIterations",  200);
        params.maxCloudPoints    = p.value("maxCloudPoints",    200000);

        return std::make_shared<PlaneFitTool>(params);
    }
    if (type == "HeightMeasure") {
        HeightFromPlaneParams params;

        // rois 배열: measure ROI들, xPct/yPct/wPct/hPct
        if (p.contains("rois") && p["rois"].is_array()) {
            for (const auto& r : p["rois"]) {
                HeightFromPlaneParams::ROI roi;
                roi.xPct = r.value("xPct", 0.f);
                roi.yPct = r.value("yPct", 0.f);
                roi.wPct = r.value("wPct", 1.f);
                roi.hPct = r.value("hPct", 1.f);
                roi.isCircle = (r.value("shape", std::string("rect")) == "circle");
                params.measureRois.push_back(roi);
            }
        }

        std::string agg = p.value("aggregation", "Mean");
        if      (agg == "Max")      params.aggregation = HeightFromPlaneParams::Aggregation::Max;
        else if (agg == "HighTail") params.aggregation = HeightFromPlaneParams::Aggregation::HighTail;
        else                        params.aggregation = HeightFromPlaneParams::Aggregation::Mean;

        params.highTailPct  = p.value("highTailPct",  20.f);
        params.useTolerance = p.value("useTolerance", false);
        params.nominalMm    = p.value("nominalMm",    0.f);
        params.toleranceMm  = p.value("toleranceMm",  0.05f);

        return std::make_shared<HeightFromPlaneTool>(params);
    }
    if (type == "LineCenter") {
        LineCenterParams params;
        // rois 배열의 첫 ROI를 검색 영역으로 사용 (xPct/yPct/wPct/hPct)
        if (p.contains("rois") && p["rois"].is_array() && !p["rois"].empty()) {
            const auto& r = p["rois"][0];
            params.xPct = r.value("xPct", 0.f);
            params.yPct = r.value("yPct", 0.f);
            params.wPct = r.value("wPct", 1.f);
            params.hPct = r.value("hPct", 1.f);
            params.angleDeg = r.value("angleDeg", 0.f);
        }
        std::string sdir = p.value("scanDir", "lr");
        if      (sdir == "rl") params.scanDir = ScanDir::Rl;
        else if (sdir == "tb") params.scanDir = ScanDir::Tb;
        else if (sdir == "bt") params.scanDir = ScanDir::Bt;
        else                   params.scanDir = ScanDir::Lr;
        params.polarity  = (p.value("polarity", "d2l") == "l2d")
                         ? Polarity::LightToDark : Polarity::DarkToLight;
        params.threshold = p.value("threshold", 1.f);
        return std::make_shared<LineCenterTool>(params);
    }
    if (type == "CsvWriter") {
        CsvWriterParams params;
        params.path = p.value("path", "");
        params.label = p.value("label", "");
        return std::make_shared<CsvWriterTool>(params);
    }
    if (type == "ThicknessMeasure") {
        ThicknessMeasure::Params params;
        if (p.contains("roi") && p["roi"].is_object()) {
            const auto& r = p["roi"];
            params.roi.xMin = r.value("xMin", 0.f);
            params.roi.xMax = r.value("xMax", 100.f);
            params.roi.yMin = r.value("yMin", 0.f);
            params.roi.yMax = r.value("yMax", 100.f);
        }
        params.nominalMm   = p.value("nominalMm",   0.f);
        params.toleranceMm = p.value("toleranceMm", 0.05f);
        return std::make_shared<ThicknessMeasure>(params);
    }

    VISION_LOG_WARN("ToolFactory: unknown tool type '{}'", type);
    return nullptr;
}

} // namespace vision
