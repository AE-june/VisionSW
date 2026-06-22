#include "NoiseFilter.h"
#include "Logger.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace vision {

NoiseFilter::NoiseFilter(Params params) : m_params(params) {}

ToolResult NoiseFilter::execute(VisionDataPtr input) {
    if (!input) return { ToolStatus::Fail, "null input" };
    if (input->hasImage()) return filter2D(input);
    if (input->hasCloud()) return filter3D(input);
    return { ToolStatus::Skip, "no data to filter" };
}

// Separable Gaussian kernel
static std::vector<float> makeGaussianKernel(int size) {
    if (size % 2 == 0) ++size;
    std::vector<float> k(size);
    int half = size / 2;
    float sigma = std::max(1.f, half / 2.f);
    float sum = 0.f;
    for (int i = 0; i < size; ++i) {
        float x = static_cast<float>(i - half);
        k[i] = std::exp(-0.5f * x * x / (sigma * sigma));
        sum += k[i];
    }
    for (auto& v : k) v /= sum;
    return k;
}

ToolResult NoiseFilter::filter2D(VisionDataPtr input) {
    VISION_LOG_DEBUG("NoiseFilter::filter2D kernel={}", m_params.kernelSize);
    const auto& src = *input->image;
    int W = src.width, H = src.height, C = src.channels;

    auto out = std::make_shared<VisionData>(*input);
    out->image = std::make_shared<Image2D>();
    out->image->width = W;
    out->image->height = H;
    out->image->channels = C;
    out->image->data.resize(static_cast<size_t>(W) * H * C);

    int k = std::max(3, m_params.kernelSize | 1);  // ensure odd
    auto kernel = makeGaussianKernel(k);
    int half = k / 2;

    // Horizontal pass into temp buffer
    std::vector<float> tmp(static_cast<size_t>(W) * H * C);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int c = 0; c < C; ++c) {
                float sum = 0.f;
                for (int dx = -half; dx <= half; ++dx) {
                    int xx = std::clamp(x + dx, 0, W - 1);
                    sum += kernel[dx + half] * src.data[(y * W + xx) * C + c];
                }
                tmp[(y * W + x) * C + c] = sum;
            }
        }
    }

    // Vertical pass to output
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int c = 0; c < C; ++c) {
                float sum = 0.f;
                for (int dy = -half; dy <= half; ++dy) {
                    int yy = std::clamp(y + dy, 0, H - 1);
                    sum += kernel[dy + half] * tmp[(yy * W + x) * C + c];
                }
                out->image->data[(y * W + x) * C + c] =
                    static_cast<uint8_t>(std::clamp(sum, 0.f, 255.f));
            }
        }
    }

    return { ToolStatus::Ok, "", out };
}

ToolResult NoiseFilter::filter3D(VisionDataPtr input) {
    VISION_LOG_DEBUG("NoiseFilter::filter3D radius={} minNeighbors={}",
                     m_params.radius, m_params.minNeighbors);
    const auto& src = *input->cloud;

    // Guard against excessive computation for very large clouds
    const size_t maxBruteForce = 50000;
    if (src.points.size() > maxBruteForce) {
        VISION_LOG_WARN("NoiseFilter::filter3D: {} points exceeds limit, skipping",
                        src.points.size());
        return { ToolStatus::Ok, "cloud too large, skipped", input };
    }

    float r2 = m_params.radius * m_params.radius;
    int minN = m_params.minNeighbors;

    auto out = std::make_shared<VisionData>(*input);
    out->cloud = std::make_shared<PointCloud3D>();
    out->cloud->frameId = src.frameId;

    for (size_t i = 0; i < src.points.size(); ++i) {
        const auto& p = src.points[i];
        int cnt = 0;
        for (size_t j = 0; j < src.points.size(); ++j) {
            if (i == j) continue;
            const auto& q = src.points[j];
            float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
            if (dx*dx + dy*dy + dz*dz <= r2) {
                if (++cnt >= minN) break;
            }
        }
        if (cnt >= minN) out->cloud->points.push_back(p);
    }

    VISION_LOG_DEBUG("NoiseFilter::filter3D {} → {} points",
                     src.points.size(), out->cloud->points.size());
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
