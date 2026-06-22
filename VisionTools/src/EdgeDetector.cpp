#include "EdgeDetector.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace vision {

EdgeDetector::EdgeDetector(Params params) : m_params(params) {}

ToolResult EdgeDetector::execute(VisionDataPtr input) {
    if (!input || !input->hasImage())
        return { ToolStatus::Fail, "EdgeDetector requires 2D image" };

    VISION_LOG_DEBUG("EdgeDetector: algo={} t1={} t2={}",
        static_cast<int>(m_params.algorithm), m_params.threshold1, m_params.threshold2);

    const auto& src = *input->image;
    int W = src.width, H = src.height;

    // Convert to grayscale
    std::vector<float> gray(static_cast<size_t>(W) * H);
    if (src.channels == 1) {
        for (int i = 0; i < W * H; ++i) gray[i] = src.data[i];
    } else {
        for (int i = 0; i < W * H; ++i) {
            // BGR weighted luminance
            float b = src.data[i * src.channels + 0];
            float g = src.data[i * src.channels + 1];
            float r = src.data[i * src.channels + 2];
            gray[i] = 0.114f * b + 0.587f * g + 0.299f * r;
        }
    }

    // Compute Sobel gradients
    std::vector<float> Gx(static_cast<size_t>(W) * H, 0.f);
    std::vector<float> Gy(static_cast<size_t>(W) * H, 0.f);
    std::vector<float> Gmag(static_cast<size_t>(W) * H, 0.f);

    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            // Sobel Kx: [[-1,0,1],[-2,0,2],[-1,0,1]]
            // Sobel Ky: [[-1,-2,-1],[0,0,0],[1,2,1]]
            float gx = -gray[(y-1)*W+(x-1)] + gray[(y-1)*W+(x+1)]
                     - 2.f*gray[y*W+(x-1)]   + 2.f*gray[y*W+(x+1)]
                     - gray[(y+1)*W+(x-1)]   + gray[(y+1)*W+(x+1)];
            float gy = -gray[(y-1)*W+(x-1)] - 2.f*gray[(y-1)*W+x] - gray[(y-1)*W+(x+1)]
                     + gray[(y+1)*W+(x-1)]   + 2.f*gray[(y+1)*W+x] + gray[(y+1)*W+(x+1)];
            Gx[y*W+x] = gx;
            Gy[y*W+x] = gy;
            Gmag[y*W+x] = std::sqrt(gx*gx + gy*gy);
        }
    }

    auto out = std::make_shared<VisionData>(*input);
    out->image = std::make_shared<Image2D>();
    out->image->width = W;
    out->image->height = H;
    out->image->channels = 1;
    out->image->data.assign(static_cast<size_t>(W) * H, 0);
    auto& dst = out->image->data;

    if (m_params.algorithm == Algorithm::Sobel) {
        float t1 = m_params.threshold1;
        for (int i = 0; i < W * H; ++i)
            dst[i] = Gmag[i] >= t1 ? 255 : 0;

    } else { // Canny
        float lo = m_params.threshold1;
        float hi = m_params.threshold2;

        // Non-maximum suppression
        std::vector<float> nms(static_cast<size_t>(W) * H, 0.f);
        for (int y = 1; y < H - 1; ++y) {
            for (int x = 1; x < W - 1; ++x) {
                float mag = Gmag[y*W+x];
                if (mag == 0.f) continue;
                // Angle in degrees, quantized to 4 directions
                float angle = std::atan2(Gy[y*W+x], Gx[y*W+x]) * (180.f / 3.14159265f);
                int a = (static_cast<int>(angle + 202.5f) % 180) / 45;
                float n1, n2;
                switch (a) {
                    case 0:  n1 = Gmag[y*W+(x-1)];     n2 = Gmag[y*W+(x+1)];     break;
                    case 1:  n1 = Gmag[(y-1)*W+(x+1)]; n2 = Gmag[(y+1)*W+(x-1)]; break;
                    case 2:  n1 = Gmag[(y-1)*W+x];     n2 = Gmag[(y+1)*W+x];     break;
                    default: n1 = Gmag[(y-1)*W+(x-1)]; n2 = Gmag[(y+1)*W+(x+1)]; break;
                }
                if (mag >= n1 && mag >= n2) nms[y*W+x] = mag;
            }
        }

        // Double threshold
        for (int i = 0; i < W * H; ++i) {
            if      (nms[i] >= hi) dst[i] = 255;
            else if (nms[i] >= lo) dst[i] = 128;   // weak edge
        }
        // Promote weak edges adjacent to strong edges
        for (int y = 1; y < H - 1; ++y) {
            for (int x = 1; x < W - 1; ++x) {
                if (dst[y*W+x] != 128) continue;
                bool near = false;
                for (int dy = -1; dy <= 1 && !near; ++dy)
                    for (int dx = -1; dx <= 1 && !near; ++dx)
                        if (dst[(y+dy)*W+(x+dx)] == 255) near = true;
                dst[y*W+x] = near ? 255 : 0;
            }
        }
    }

    return { ToolStatus::Ok, "", out };
}

} // namespace vision
