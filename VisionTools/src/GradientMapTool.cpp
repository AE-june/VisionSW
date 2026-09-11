#include "GradientMapTool.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <limits>

namespace vision {

GradientMapTool::GradientMapTool(GradientMapParams params) : m_params(std::move(params)) {}

ToolResult GradientMapTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0))
        return { ToolStatus::Fail, "GradientMap: HeightMap 입력이 없습니다." };

    const HeightMap& zm  = *input->inHeightMap(0);
    const int        w   = zm.width, h = zm.height;
    const float      NaN = std::numeric_limits<float>::quiet_NaN();

    // NaN → 0 으로 마스킹 후 Sobel (NaN이 경계에 있으면 기울기 오염)
    cv::Mat src(h, w, CV_32F);
    cv::Mat nanMask(h, w, CV_8U, cv::Scalar(0));
    const float* raw = zm.data.data();
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            size_t i = static_cast<size_t>(r) * w + c;
            if (std::isnan(raw[i])) {
                src.at<float>(r, c)    = 0.f;
                nanMask.at<uint8_t>(r, c) = 255;
            } else {
                src.at<float>(r, c) = raw[i];
            }
        }
    }

    const int ks = (m_params.ksize == 5) ? 5 : 3;
    cv::Mat gx, gy;
    cv::Sobel(src, gx, CV_32F, 1, 0, ks);
    cv::Sobel(src, gy, CV_32F, 0, 1, ks);

    // Sobel 결과를 mm/mm 단위로 정규화 (zResMm / xResMm, zResMm / yResMm)
    // Sobel 3x3 합산 가중치=8 → /8 → 기울기 (mm per pixel).
    // 추가로 pixel→mm 변환: / xResMm (or yResMm)
    const double normX = zm.zResMm / (zm.xResMm * (ks == 3 ? 8.0 : 32.0));
    const double normY = zm.zResMm / (zm.yResMm * (ks == 3 ? 8.0 : 32.0));

    // 결과 HeightMap
    auto out_hm = std::make_shared<HeightMap>(zm);
    out_hm->zResMm = 1.0f;    // 출력은 무차원 기울기값 그대로
    out_hm->zZeroCount = 0.f;
    std::vector<float>& outData = out_hm->data;

    const std::string& mode = m_params.output_mode;
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            size_t i = static_cast<size_t>(r) * w + c;
            if (nanMask.at<uint8_t>(r, c)) { outData[i] = NaN; continue; }
            double gxv = gx.at<float>(r, c) * normX;
            double gyv = gy.at<float>(r, c) * normY;
            if      (mode == "gx")  outData[i] = static_cast<float>(gxv);
            else if (mode == "gy")  outData[i] = static_cast<float>(gyv);
            else                    outData[i] = static_cast<float>(std::sqrt(gxv*gxv + gyv*gyv));
        }
    }

    auto vd = std::make_shared<VisionData>();
    vd->setHeightMap(out_hm);
    vd->sourceId = input->sourceId;
    vd->frames   = input->frames;
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
