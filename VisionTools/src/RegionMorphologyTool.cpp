#include "RegionMorphologyTool.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <utility>

namespace vision {

RegionMorphologyTool::RegionMorphologyTool(RegionMorphologyParams params)
    : m_params(std::move(params)) {}

ToolResult RegionMorphologyTool::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "RegionMorphology: 입력이 없습니다." };

    const auto rg = input->inRegion(0);
    if (!rg || rg->empty())
        return { ToolStatus::Fail, "RegionMorphology: Region 입력이 없습니다." };

    const int w = rg->width, h = rg->height;
    const int r = std::max(1, m_params.radius);

    // 구조 원소
    const int shape = (m_params.shape == "ellipse")
        ? cv::MORPH_ELLIPSE : cv::MORPH_RECT;
    cv::Mat kernel = cv::getStructuringElement(shape,
        cv::Size(2 * r + 1, 2 * r + 1));

    // 마스크 → cv::Mat (1채널 0/1)
    cv::Mat mat(h, w, CV_8U, const_cast<uint8_t*>(rg->mask.data()));
    cv::Mat result;

    int op = cv::MORPH_DILATE;
    if      (m_params.op == "erode")  op = cv::MORPH_ERODE;
    else if (m_params.op == "open")   op = cv::MORPH_OPEN;
    else if (m_params.op == "close")  op = cv::MORPH_CLOSE;

    cv::morphologyEx(mat, result, op, kernel);

    auto out = std::make_shared<Region>(Region::makeEmpty(w, h));
    out->frameId = rg->frameId;
    out->label   = rg->label;
    std::copy(result.datastart, result.dataend, out->mask.begin());

    auto vd = std::make_shared<VisionData>();
    vd->setRegion(std::move(out));
    vd->sourceId = input->sourceId;
    vd->frames   = input->frames;
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
