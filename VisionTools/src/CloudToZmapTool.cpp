#include "CloudToZmapTool.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace vision {

namespace {
// 격자 픽셀 수 상한 — 해상도가 너무 촘촘하거나 범위가 넓어 메모리가 과도하게
// 커지는 것을 막는 안전장치(약 800MB float 버퍼 = 2억 픽셀 기준으로 넉넉히 설정).
constexpr size_t kMaxPixels = 200'000'000;
} // namespace

ToolResult CloudToZmapTool::execute(VisionDataPtr input) {
    if (!input)
        return { ToolStatus::Fail, "CloudToZmap: 입력이 없습니다" };

    // 포트 0에 여러 클라우드가 연결됐을 수 있음(main.cpp가 같은 포트로 들어온 clouds를
    // 병합) — 전부 모아 하나의 공유 격자에 비닝한다.
    std::vector<const PointCloud3D*> clouds;
    size_t totalPts = 0;
    for (const auto& c : input->inClouds(0)) {
        if (!c || c->points.empty()) continue;
        clouds.push_back(c.get());
        totalPts += c->points.size();
    }
    if (clouds.empty())
        return { ToolStatus::Fail, "CloudToZmap: PointCloud3D 입력이 없습니다" };

    if (m_p.lateralResMm <= 0 || m_p.transportResMm <= 0 || m_p.verticalResMm <= 0)
        return { ToolStatus::Fail, "CloudToZmap: 해상도 파라미터는 0보다 커야 합니다" };

    // bounding box: col ← y(lateral), row ← x(transport) — 모든 클라우드의 합집합
    float latMin = std::numeric_limits<float>::max(), latMax = std::numeric_limits<float>::lowest();
    float traMin = std::numeric_limits<float>::max(), traMax = std::numeric_limits<float>::lowest();
    for (const auto* cloud : clouds) {
        for (const auto& p : cloud->points) {
            latMin = std::min(latMin, p.y); latMax = std::max(latMax, p.y);
            traMin = std::min(traMin, p.x); traMax = std::max(traMax, p.x);
        }
    }

    const int width  = (int)std::llround((double)(latMax - latMin) / m_p.lateralResMm) + 1;
    const int height = (int)std::llround((double)(traMax - traMin) / m_p.transportResMm) + 1;
    if (width <= 0 || height <= 0)
        return { ToolStatus::Fail, "CloudToZmap: 격자 크기가 유효하지 않습니다" };
    if ((size_t)width * (size_t)height > kMaxPixels)
        return { ToolStatus::Fail, "CloudToZmap: 해상도 대비 격자가 너무 큽니다(" +
                 std::to_string(width) + "x" + std::to_string(height) +
                 ") — lateralResMm/transportResMm을 키우세요" };

    const bool needList = (m_p.agg == "median");
    std::vector<float> gridSum, gridMin, gridMax;
    std::vector<int>   gridCnt;
    std::vector<std::vector<float>> gridList;
    const size_t nCells = (size_t)width * (size_t)height;
    if (needList) {
        gridList.assign(nCells, {});
    } else {
        gridSum.assign(nCells, 0.f);
        gridCnt.assign(nCells, 0);
        gridMin.assign(nCells, std::numeric_limits<float>::max());
        gridMax.assign(nCells, std::numeric_limits<float>::lowest());
    }

    for (const auto* cloud : clouds) {
        for (const auto& p : cloud->points) {
            int col = (int)std::llround((double)(p.y - latMin) / m_p.lateralResMm);
            int row = (int)std::llround((double)(p.x - traMin) / m_p.transportResMm);
            if (col < 0 || col >= width || row < 0 || row >= height) continue;
            const size_t idx = (size_t)row * width + col;
            if (needList) {
                gridList[idx].push_back(p.z);
            } else {
                gridSum[idx] += p.z;
                gridCnt[idx] += 1;
                gridMin[idx] = std::min(gridMin[idx], p.z);
                gridMax[idx] = std::max(gridMax[idx], p.z);
            }
        }
    }

    auto hm = std::make_shared<HeightMap>();
    hm->width  = width;
    hm->height = height;
    hm->channels = 1;
    hm->xResMm = (float)m_p.lateralResMm;
    hm->yResMm = (float)m_p.transportResMm;
    hm->zResMm = (float)m_p.verticalResMm;
    hm->zZeroCount = (float)m_p.zZeroCount;
    hm->originCol = (float)(-latMin / m_p.lateralResMm);
    hm->originRow = (float)(-traMin / m_p.transportResMm);
    hm->frameId = input->sourceId;
    hm->data.assign(nCells, std::numeric_limits<float>::quiet_NaN());

    size_t filled = 0;
    for (size_t idx = 0; idx < nCells; ++idx) {
        double zMm = std::numeric_limits<double>::quiet_NaN();
        if (needList) {
            auto& list = gridList[idx];
            if (list.empty()) continue;
            if (m_p.agg == "top")    zMm = *std::max_element(list.begin(), list.end());
            else if (m_p.agg == "bottom") zMm = *std::min_element(list.begin(), list.end());
            else {   // median
                std::nth_element(list.begin(), list.begin() + list.size() / 2, list.end());
                zMm = list[list.size() / 2];
                if (list.size() % 2 == 0) {
                    auto mx = std::max_element(list.begin(), list.begin() + list.size() / 2);
                    zMm = (zMm + *mx) / 2.0;
                }
            }
        } else {
            if (gridCnt[idx] == 0) continue;
            if (m_p.agg == "top")         zMm = gridMax[idx];
            else if (m_p.agg == "bottom") zMm = gridMin[idx];
            else                          zMm = gridSum[idx] / gridCnt[idx];   // mean
        }
        // raw = round(z/verticalResMm + zZeroCount), [0,65535] clamp — ZMapBinning.h와 동일한
        // 16bit 그레이스케일 양자화. Saver가 이후 실제 16bit 이미지로 저장할 때 다시 clamp하므로
        // 여기서 미리 같은 범위로 넣어야 음수/과대 z가 무효(0)로 뭉개지지 않는다.
        double raw = std::round(zMm / m_p.verticalResMm + m_p.zZeroCount);
        raw = std::clamp(raw, 0.0, 65535.0);
        hm->data[idx] = (float)raw;
        ++filled;
    }

    auto out = std::make_shared<VisionData>();
    out->setHeightMap(hm);
    out->sourceId = input->sourceId;
    VISION_LOG_INFO("CloudToZmap: {} clouds, {} points → {}x{} Zmap ({} valid px, agg={})",
                     clouds.size(), totalPts, width, height, filled, m_p.agg);
    return { ToolStatus::Ok, "", out };
}

} // namespace vision
