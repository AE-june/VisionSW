#include "ToolFactory.h"
#include "ZMapCache.h"
#include "NoiseFilter.h"
#include "EdgeDetector.h"
#include "ThicknessMeasure.h"
#include "PlaneFitTool.h"
#include "HeightFromPlaneTool.h"
#include "CsvWriterTool.h"
#include "LineCenterTool.h"
#include "AlignTool.h"
#include "IZMapLoader.h"
#include "VisionData.h"
#include "ZMap.h"
#include "Logger.h"
#include <limits>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <vector>
#include <deque>
#include <filesystem>
#include <unordered_set>
#include <ctime>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>
#include <iomanip>

// stb for PNG/JPG loading
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// OpenCV for saving (16-bit PNG/TIFF + 일반 포맷)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace vision {

// ── ZMap 파일 글로벌 캐시 (폴더검사 시 반복 IO 제거) ──────────────────────
std::unordered_map<std::string, std::shared_ptr<ZMap>> g_zmapFileCache;
std::unordered_set<std::string> g_preloadedFolders;
std::mutex g_zmapFileCacheMtx;

std::shared_ptr<ZMap> loadZMapFromFile(const std::string& path,
                                       float xRes, float yRes, float zRes) {
    int w, h, ch;
    uint16_t* raw16 = stbi_load_16(path.c_str(), &w, &h, &ch, 1);
    if (raw16) {
        auto zmap = std::make_shared<ZMap>();
        zmap->width=w; zmap->height=h;
        zmap->xResMm=xRes; zmap->yResMm=yRes; zmap->zResMm=zRes;
        zmap->zZeroCount=32768.f;
        zmap->data.resize((size_t)w*h);
        for (int i=0;i<w*h;++i)
            zmap->data[i] = raw16[i]==0 ? std::numeric_limits<float>::quiet_NaN()
                                        : static_cast<float>(raw16[i]);
        stbi_image_free(raw16);
        return zmap;
    }
    unsigned char* raw8 = stbi_load(path.c_str(), &w, &h, &ch, 1);
    if (!raw8) return nullptr;
    auto zmap = std::make_shared<ZMap>();
    zmap->width=w; zmap->height=h;
    zmap->xResMm=xRes; zmap->yResMm=yRes; zmap->zResMm=zRes;
    zmap->zZeroCount=128.f;
    zmap->data.resize((size_t)w*h);
    for (int i=0;i<w*h;++i)
        zmap->data[i] = raw8[i]==0 ? std::numeric_limits<float>::quiet_NaN()
                                   : static_cast<float>(raw8[i]);
    stbi_image_free(raw8);
    return zmap;
}

int preloadFolder(const std::string& folder, float xRes, float yRes, float zRes) {
    namespace fs = std::filesystem;
    // Collect files not yet cached
    std::vector<std::string> toLoad;
    {
        std::lock_guard<std::mutex> lk(g_zmapFileCacheMtx);
        if (g_preloadedFolders.count(folder)) return 0;
        g_preloadedFolders.insert(folder);
        std::error_code ec;
        for (auto& e : fs::directory_iterator(folder, ec)) {
            if (e.path().extension() == ".png") {
                std::string fp = e.path().string();
                if (!g_zmapFileCache.count(fp))
                    toLoad.push_back(fp);
            }
        }
    }
    if (toLoad.empty()) return 0;

    // Load in parallel using hardware concurrency
    const int nThreads = static_cast<int>(std::thread::hardware_concurrency());
    const int n = static_cast<int>(toLoad.size());
    std::vector<std::pair<std::string, std::shared_ptr<ZMap>>> results(n);
    std::atomic<int> idx{0};

    auto worker = [&]() {
        int i;
        while ((i = idx.fetch_add(1)) < n) {
            results[i] = { toLoad[i], loadZMapFromFile(toLoad[i], xRes, yRes, zRes) };
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(nThreads);
    for (int t = 0; t < nThreads; ++t)
        threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    int loaded = 0;
    {
        std::lock_guard<std::mutex> lk(g_zmapFileCacheMtx);
        for (auto& [path, zm] : results)
            if (zm) { g_zmapFileCache[path] = zm; ++loaded; }
    }
    return loaded;
}

// ── Loader tools (defined here, used by ToolFactory) ─────────────────────

class ZMapLoaderTool : public IAlgorithmTool {
    std::string m_path;
    std::string m_folder;
    float m_xResMm, m_yResMm, m_zResMm;
public:
    // 폴더 전체 프리로드는 여기서 하지 않음 — 인터랙티브 편집은 ParamPanel이 폴더 선택 시
    // 명시적으로 "preload" 커맨드를 보내고, 폴더검사(배치)는 워커별로 필요한 파일만
    // "prefetch" 커맨드로 미리 당겨오므로 생성자에서 폴더 전체를 긁으면 워커마다 중복 로드됨.
    ZMapLoaderTool(std::string path, std::string folder, float xRes, float yRes, float zRes)
        : m_path(std::move(path)), m_folder(std::move(folder)),
          m_xResMm(xRes), m_yResMm(yRes), m_zResMm(zRes)
    {}
    std::string name() const override { return "ZMapLoader"; }

    ToolResult execute(VisionDataPtr) override {
        if (m_path.empty())
            return { ToolStatus::Fail, "ZMapLoader: 파일 경로가 설정되지 않았습니다" };

        // 캐시 확인
        {
            std::lock_guard<std::mutex> lk(g_zmapFileCacheMtx);
            auto it = g_zmapFileCache.find(m_path);
            if (it != g_zmapFileCache.end()) {
                auto data = std::make_shared<VisionData>();
                data->zmap = it->second;
                data->sourceId = m_path;
                VISION_LOG_INFO("ZMapLoader: cache hit {}", m_path);
                return { ToolStatus::Ok, "", data };
            }
        }

        // 캐시 미스 → 파일 로드 후 캐시 저장
        auto zmap = loadZMapFromFile(m_path, m_xResMm, m_yResMm, m_zResMm);
        if (!zmap)
            return { ToolStatus::Fail, "ZMapLoader: 파일을 읽을 수 없습니다: " + m_path };

        {
            std::lock_guard<std::mutex> lk(g_zmapFileCacheMtx);
            g_zmapFileCache[m_path] = zmap;
        }
        VISION_LOG_INFO("ZMapLoader: {}x{} loaded from {}", zmap->width, zmap->height, m_path);
        auto data = std::make_shared<VisionData>();
        data->zmap = zmap;
        data->sourceId = m_path;
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

// ── ExposureSplit (이중노출 분리): 인터리브 홀짝 이중노출 ZMap → 저노출/장노출 분리.
//   짝/홀 행 = 저/장 노출. 저노출은 상/하부 구간 8배 행확장(makeZLow),
//   장노출은 2배 복원(makeZ). 출력: 1.저노출(확장) / 2.장노출 중 선택.
//   (머지/리플렉션 제거는 별도 '이중노출 머지'(ExposureMerge2) 노드로 분리됨)
class ExposureMergeTool : public IAlgorithmTool {
    int   m_outputStage;   // 0=저노출(확장), 1=장노출
    bool  m_skipStages;    // true면 결과창 미리보기용 다른 단계는 만들지 않음(배치 가속/메모리 절약)
public:
    ExposureMergeTool(int outputStage, bool skipStages)
        : m_outputStage(outputStage), m_skipStages(skipStages) {}
    std::string name() const override { return "ExposureMerge"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->hasZMap())
            return { ToolStatus::Fail, "ExposureMerge: ZMap 입력이 필요합니다" };

        using clk = std::chrono::steady_clock;
        auto t0 = clk::now();
        auto lap = [&](const char* tag) {
            auto ms = std::chrono::duration<double,std::milli>(clk::now()-t0).count();
            VISION_LOG_INFO("[ExposureMerge] {}: {:.1f} ms", tag, ms);
            t0 = clk::now();
        };

        const auto& zm = *input->zmap;
        const int w = zm.width, h = zm.height;
        if (h < 2) return { ToolStatus::Fail, "ExposureMerge: 이미지 높이가 너무 작습니다" };


        const int n = h / 2;                 // 홀짝 쌍 개수 = 출력 행 수
        const int si = std::clamp(m_outputStage, 0, 1);   // 0=저노출(확장), 1=장노출
        const float NaN = std::numeric_limits<float>::quiet_NaN();
        // 입력 ZMap 데이터(raw count float, 무효=NaN)를 그대로 사용
        auto zAt = [&](int row, int c) -> float { return zm.data[(size_t)row * w + c]; };

        // 짝수행 = 저노출로 가정 (I/L 파일 로드 없이 ZMap만 사용)
        const bool evenIsLow = true;
        auto loRow = [&](int r){ return evenIsLow ? 2*r   : 2*r+1; };
        auto hiRow = [&](int r){ return evenIsLow ? 2*r+1 : 2*r;   };

        // ① 홀짝 분리 → 저노출(low)/장노출(high) 배열 (n×w, 무효=NaN)
        std::vector<float> low((size_t)n*w), high((size_t)n*w);
        for (int r = 0; r < n; ++r) for (int c = 0; c < w; ++c) {
            size_t i = (size_t)r*w + c;
            low[i]  = zAt(loRow(r), c);
            high[i] = zAt(hiRow(r), c);
        }
        lap("홀짝 분리");

        // 각 단계를 원본 행수(h)로 복제해 ZMap 생성 (병합행 r → 원래 두 행 2r,2r+1)
        // half(n행) → scale배 확장 (선형 보간). 다른 단계에 사용.
        auto makeZ = [&](const std::vector<float>& half, int scale = 2) {
            auto z = std::make_shared<ZMap>();
            const int outH = n * scale;
            z->width=w; z->height=outH;
            z->xResMm=zm.xResMm; z->yResMm=zm.yResMm;
            z->zResMm=zm.zResMm; z->zZeroCount=zm.zZeroCount;
            z->data.assign((size_t)outH*w, NaN);
            for (int r = 0; r < n; ++r) {
                for (int s = 0; s < scale; ++s) {
                    float* dst = &z->data[(size_t)(r*scale+s)*w];
                    if (s == 0 || r+1 >= n) {
                        std::copy(&half[(size_t)r*w], &half[(size_t)r*w+w], dst);
                    } else {
                        const float t = (float)s / scale;
                        for (int c = 0; c < w; ++c) {
                            float a = half[(size_t)r*w+c];
                            float b = half[(size_t)(r+1)*w+c];
                            if      (!std::isnan(a) && !std::isnan(b)) dst[c] = a*(1.f-t) + b*t;
                            else if (!std::isnan(a))                    dst[c] = a;
                            else                                        dst[c] = b;
                        }
                    }
                }
            }
            return z;
        };

        // 저노출 전용: 상부/하부 각 1000행은 8배 선형 보간, 중간은 1배 그대로.
        auto makeZLow = [&](const std::vector<float>& half) {
            const int top = std::min(200, n / 2);
            const int bot = std::min(200, n - top);
            const int mid = n - top - bot;
            const int outH = top*8 + mid + bot*8;
            auto z = std::make_shared<ZMap>();
            z->width=w; z->height=outH;
            z->xResMm=zm.xResMm; z->yResMm=zm.yResMm;
            z->zResMm=zm.zResMm; z->zZeroCount=zm.zZeroCount;
            z->data.assign((size_t)outH*w, NaN);
            int outRow = 0;
            // 8배 보간 구간 (r0..r0+count-1)
            auto expand8 = [&](int r0, int count) {
                for (int r = r0; r < r0+count; ++r) {
                    for (int s = 0; s < 8; ++s) {
                        float* dst = &z->data[(size_t)outRow*w];
                        if (s == 0 || r+1 >= n) {
                            std::copy(&half[(size_t)r*w], &half[(size_t)r*w+w], dst);
                        } else {
                            const float t = (float)s / 8;
                            for (int c = 0; c < w; ++c) {
                                float a = half[(size_t)r*w+c];
                                float b = half[(size_t)(r+1)*w+c];
                                if      (!std::isnan(a) && !std::isnan(b)) dst[c] = a*(1.f-t)+b*t;
                                else if (!std::isnan(a))                    dst[c] = a;
                                else                                        dst[c] = b;
                            }
                        }
                        ++outRow;
                    }
                }
            };
            expand8(0, top);
            for (int r = top; r < top+mid; ++r) {   // 중간: 1배 그대로
                std::copy(&half[(size_t)r*w], &half[(size_t)r*w+w], &z->data[(size_t)outRow*w]);
                ++outRow;
            }
            expand8(top+mid, bot);
            return z;
        };

        ZMapPtr zLow, zHigh;
        if (m_skipStages) {
            // 미리보기가 필요 없으면 실제 출력으로 쓸 단계 하나만 업샘플링(메모리 절약).
            if (si == 0) { zLow = makeZLow(low); }
            else         { zHigh = makeZ(high);  }
            lap("makeZ(선택 1단계)");
        } else {
            zLow = makeZLow(low); lap("makeZLow(8x)");
            zHigh = makeZ(high);  lap("makeZ(2x)");
        }

        const ZMapPtr stageZmaps[] = { zLow, zHigh };

        auto data = std::make_shared<VisionData>();
        data->zmap = stageZmaps[si];
        data->sourceId = input->sourceId;
        if (!m_skipStages) {
            data->stages = std::make_shared<std::vector<std::pair<std::string, ZMapPtr>>>();
            data->stages->push_back({ "1. 저노출(확장)", zLow });
            data->stages->push_back({ "2. 장노출",       zHigh });
        }
        VISION_LOG_INFO("ExposureSplit: {}x{} → 저노출 {}행 / 장노출 {}행",
                        w, h, zLow ? zLow->height : 0, zHigh ? zHigh->height : 0);
        return { ToolStatus::Ok, "", data };
    }
};

// ── RowStretch (행 늘리기): 지정 ROI(세로 밴드, 가로 전체)의 행을 배수만큼 선형보간
//    업샘플. 밴드마다 개별 배수. 밴드 밖은 ×1 그대로. 출력 높이 = Σ(행별 배수).
//    (기존 이중노출 분리 노드가 저노출 상/하단을 늘리던 방식을 ROI로 일반화 — 홀짝 분리 없음)
class RowStretchTool : public IAlgorithmTool {
public:
    struct Band { float yPct, hPct; int scale; };
private:
    std::vector<Band> m_bands;
public:
    explicit RowStretchTool(std::vector<Band> bands) : m_bands(std::move(bands)) {}
    std::string name() const override { return "RowStretch"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->hasZMap())
            return { ToolStatus::Fail, "행 늘리기: ZMap 입력이 필요합니다" };
        const auto& zm = *input->zmap;
        const int w = zm.width, h = zm.height;
        if (w <= 0 || h <= 0) return { ToolStatus::Fail, "행 늘리기: 빈 ZMap" };
        const float NaN = std::numeric_limits<float>::quiet_NaN();

        // 입력 행마다 배수 결정 (밴드에 속하면 그 밴드 배수, 겹치면 뒤 밴드 우선, 아니면 1)
        std::vector<int> rowScale((size_t)h, 1);
        for (const auto& b : m_bands) {
            int y0 = std::clamp((int)(b.yPct * h),            0, h);
            int y1 = std::clamp((int)((b.yPct + b.hPct) * h), 0, h);
            int s  = std::max(1, b.scale);
            for (int r = y0; r < y1; ++r) rowScale[r] = s;
        }
        size_t outH = 0; for (int r = 0; r < h; ++r) outH += (size_t)rowScale[r];

        auto z = std::make_shared<ZMap>();
        z->width = w; z->height = (int)outH;
        z->xResMm = zm.xResMm; z->yResMm = zm.yResMm;   // yRes는 기존 노드와 동일하게 유지
        z->zResMm = zm.zResMm; z->zZeroCount = zm.zZeroCount;
        z->originCol = zm.originCol; z->originRow = zm.originRow;
        z->data.assign((size_t)outH * w, NaN);

        auto at = [&](int r, int c){ return zm.data[(size_t)r * w + c]; };
        size_t outRow = 0;
        for (int r = 0; r < h; ++r) {
            const int s = rowScale[r];
            for (int k = 0; k < s; ++k) {
                float* dst = &z->data[outRow * w];
                if (k == 0 || r + 1 >= h) {          // 원본 행 그대로 (마지막 행도 그대로)
                    std::copy(&zm.data[(size_t)r * w], &zm.data[(size_t)r * w + w], dst);
                } else {                              // r ~ r+1 선형보간 (NaN 인지)
                    const float t = (float)k / s;
                    for (int c = 0; c < w; ++c) {
                        float a = at(r, c), b = at(r + 1, c);
                        if      (!std::isnan(a) && !std::isnan(b)) dst[c] = a * (1.f - t) + b * t;
                        else if (!std::isnan(a))                    dst[c] = a;
                        else                                        dst[c] = b;
                    }
                }
                ++outRow;
            }
        }

        auto data = std::make_shared<VisionData>();
        data->zmap = z;
        data->sourceId = input->sourceId;
        VISION_LOG_INFO("RowStretch: {}x{} → {}x{} (밴드 {}개)", w, h, w, (int)outH, (int)m_bands.size());
        return { ToolStatus::Ok, "", data };
    }
};

// ── DualExposureMerge (이중노출 머지, 재구현): 인터리브 홀짝 → 오프셋보정 →
//    저노출우선 머지 → 연속성(영역성장) 필터로 fill 리플렉션 제거 → 반해상도 출력.
//    규칙: 겹침은 저노출 우선(리플 자동배제), fill은 신뢰 씨앗에서 연결성으로 검증.
//    [증분1] ① 연속성 주력. ② 신뢰표면편차+I/LLT 게이팅, I중앙값 홀짝판별, 자동보정은 추후.
class DualExposureMergeTool : public IAlgorithmTool {
    float m_matchTol;   // 겹침 일치 허용(카운트) — 씨앗/오프셋 추정용
    float m_reflTol;    // 리플 허용(카운트) — ② 예약
    float m_tolX, m_tolY;
    int   m_gapK;
    bool  m_halfRes;
    bool  m_noPreview;  // true(검사/배치)면 최종 출력 1개만 생성, 중간 단계(디스플레이용) 생략
    bool  m_chunkMode;   // true면 입력을 청크(겹침 포함)로 나눠 처리 — 실시간 스트리밍 대응
    int   m_chunkRows;   // 청크당 입력 프로파일(행) 수
    int   m_overlapRows; // 청크 위·아래 겹침 행 수 (BFS 연결성 컨텍스트용)
public:
    DualExposureMergeTool(float matchTol, float reflTol, float tolX, float tolY,
                          int gapK, bool halfRes, bool noPreview,
                          bool chunkMode, int chunkRows, int overlapRows)
        : m_matchTol(matchTol), m_reflTol(reflTol), m_tolX(tolX), m_tolY(tolY),
          m_gapK(gapK), m_halfRes(halfRes), m_noPreview(noPreview),
          m_chunkMode(chunkMode), m_chunkRows(std::max(2,chunkRows)), m_overlapRows(std::max(0,overlapRows)) {}
    std::string name() const override { return "ExposureMerge2"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->hasZMap())
            return { ToolStatus::Fail, "이중노출 머지: ZMap 입력이 필요합니다" };
        const auto& zm = *input->zmap;
        const int w = zm.width, h = zm.height;
        if (h < 2) return { ToolStatus::Fail, "이중노출 머지: 이미지 높이가 너무 작습니다" };
        const int n = h / 2;                        // 전체 pair(출력행) 수
        const float NaN = std::numeric_limits<float>::quiet_NaN();
        auto at = [&](int r, int c){ return zm.data[(size_t)r*w + c]; };

        // ── 코어 머지: pair 범위 [pr0,pr1)를 처리해 filtered(bn×w) 반환 ──────────────
        //   ①홀짝분리 ②오프셋보정 ③저노출우선머지 ④연속성필터. 인덱스는 블록 로컬(0..bn),
        //   입력은 전역 행 at(2*(pr0+r))에서 읽는다. 청크 모드는 이 함수를 겹침 포함 블록마다 호출.
        //   outLowC/outHigh/outMerged 포인터를 주면 디스플레이용 중간단계도 반환(전체모드 전용).
        auto computeFiltered = [&](int pr0, int pr1, long& removedOut, float& offsetOut,
                                   std::vector<float>* outLowC, std::vector<float>* outHigh,
                                   std::vector<float>* outMerged, float forcedOffset) -> std::vector<float> {
            const int bn = pr1 - pr0;
            const size_t BN = (size_t)bn * w;
            // ① 홀짝 분리 (짝수행=저노출 가정) — 행 단위 병렬
            std::vector<float> low(BN), high(BN);
            cv::parallel_for_(cv::Range(0, bn), [&](const cv::Range& rg) {
                for (int r = rg.start; r < rg.end; ++r) { const int gr = pr0 + r;
                    for (int c = 0; c < w; ++c) { size_t i=(size_t)r*w+c; low[i]=at(2*gr,c); high[i]=at(2*gr+1,c); } }
            });
            // ② 오프셋 보정: 겹침 일치 픽셀 (low-high) 중앙값(stride-4 서브샘플)만큼 저노출 이동.
            //    forcedOffset이 유효하면(청크 모드) 전역 오프셋을 그대로 사용 — 청크별 편차 방지.
            float offset;
            if (!std::isnan(forcedOffset)) {
                offset = forcedOffset;
            } else {
                std::vector<float> diffs; diffs.reserve(BN/4 + 1);
                for (size_t i = 0; i < BN; i += 4)
                    if (!std::isnan(low[i]) && !std::isnan(high[i]) && std::fabs(low[i]-high[i]) <= m_matchTol)
                        diffs.push_back(low[i]-high[i]);
                offset = 0.f;
                if (!diffs.empty()) { size_t mid=diffs.size()/2; std::nth_element(diffs.begin(),diffs.begin()+mid,diffs.end()); offset=diffs[mid]; }
            }
            offsetOut = offset;
            std::vector<float> lowC = std::move(low);
            cv::parallel_for_(cv::Range(0, bn), [&](const cv::Range& rg) {
                for (size_t i=(size_t)rg.start*w; i<(size_t)rg.end*w; ++i) if (!std::isnan(lowC[i])) lowC[i] -= offset;
            });
            // ③ 기본 머지: 저노출 유효 → 저노출(보정), 없으면 장노출.
            std::vector<float> merged(BN);
            std::vector<uint8_t> mvalid(BN);
            cv::parallel_for_(cv::Range(0, bn), [&](const cv::Range& rg) {
                for (size_t i=(size_t)rg.start*w; i<(size_t)rg.end*w; ++i) {
                    float m = !std::isnan(lowC[i]) ? lowC[i] : (!std::isnan(high[i]) ? high[i] : NaN);
                    merged[i] = m; mvalid[i] = std::isnan(m) ? 0 : 1;
                }
            });
            // ④ 연속성(영역성장) 필터: 씨앗 = 저노출 유효 && 겹침 일치(신뢰 픽셀).
            std::vector<uint8_t> visited(BN, 0);
            for (size_t i = 0; i < BN; ++i)
                if (!std::isnan(lowC[i]) && !std::isnan(high[i]) && std::fabs(lowC[i]-high[i]) <= m_matchTol) visited[i]=1;
            // 경계 씨앗만 큐에 (내부 씨앗은 뻗을 데 없음 → 결과 동일, 헛 pop 제거)
            std::vector<int> q; q.reserve(BN/2 + 1);
            for (int r = 0; r < bn; ++r) for (int c = 0; c < w; ++c) {
                size_t i=(size_t)r*w+c;
                if (!visited[i]) continue;
                if ((c>0 && !visited[i-1]) || (c<w-1 && !visited[i+1]) ||
                    (r>0 && !visited[i-w]) || (r<bn-1 && !visited[i+w])) q.push_back((int)i);
            }
            auto tryRay = [&](int r, int c, float z, int dr, int dc, float tol) {
                for (int k = 1; k <= m_gapK+1; ++k) {
                    int nr=r+dr*k, nc=c+dc*k;
                    if (nr<0||nr>=bn||nc<0||nc>=w) break;
                    size_t ni=(size_t)nr*w+nc;
                    if (visited[ni]) break;
                    if (!mvalid[ni]) continue;                  // NaN 갭: 계속 진행
                    if (std::fabs(merged[ni]-z) <= tol*k) { visited[ni]=1; q.push_back((int)ni); }
                    break;                                      // 유효 픽셀 만나면 종료
                }
            };
            while (!q.empty()) {
                int idx=q.back(); q.pop_back();
                int r=idx/w, c=idx%w; float z=merged[idx];
                tryRay(r,c,z, 0,-1,m_tolX); tryRay(r,c,z, 0,1,m_tolX);
                tryRay(r,c,z,-1, 0,m_tolY); tryRay(r,c,z, 1,0,m_tolY);
            }
            std::vector<float> filtered = merged;               // merged는 아래서 outMerged로 넘길 수 있어 복사 유지
            std::atomic<long> removedA{0};
            cv::parallel_for_(cv::Range(0, bn), [&](const cv::Range& rg) {
                long loc = 0;
                for (size_t i=(size_t)rg.start*w; i<(size_t)rg.end*w; ++i)
                    if (std::isnan(lowC[i]) && !std::isnan(high[i]) && !visited[i]) { filtered[i]=NaN; ++loc; }
                removedA += loc;
            });
            removedOut += removedA.load();
            if (outMerged) *outMerged = std::move(merged);
            if (outLowC)   *outLowC   = std::move(lowC);
            if (outHigh)   *outHigh   = std::move(high);
            return filtered;
        };

        // ── 전체 이미지 vs 청크 실행 ─────────────────────────────────────────────
        long removed = 0; float offset = 0.f;
        std::vector<float> lowCFull, highFull, mergedFull;   // 디스플레이 단계용(전체모드 + !noPreview)
        std::vector<float> filtered;
        if (!m_chunkMode) {
            // 청크 미사용: 기존처럼 전체 이미지에 대해 한 번에 연산.
            const bool wantStages = !m_noPreview;
            filtered = computeFiltered(0, n, removed, offset,
                wantStages ? &lowCFull : nullptr, wantStages ? &highFull : nullptr, wantStages ? &mergedFull : nullptr, NaN);
        } else {
            // 청크 모드: 코어 청크를 위·아래 겹침만큼 확장해 처리하고, 코어 행만 출력에 기록.
            //   겹침은 BFS 연속성 컨텍스트를 청크 경계 너머까지 확보해 이음매 결함을 방지.
            filtered.assign((size_t)n*w, NaN);
            const int chunkPairs = std::max(1, m_chunkRows/2);   // 입력행/2 = pair(출력행)
            const int ov         = std::max(0, m_overlapRows/2); // 겹침도 pair 단위
            // 오프셋은 두 노출의 전역 캘리브레이션 성질 → 전체 이미지에서 1회 산출해 모든 청크가 공유.
            //   (전체 모드와 동일한 flat stride-4 샘플링으로 값 일치 보장)
            float gOffset = 0.f;
            {
                std::vector<float> d; d.reserve((size_t)n*w/4 + 1);
                for (size_t i = 0; i < (size_t)n*w; i += 4) {
                    int r = (int)(i / w), c = (int)(i % w);
                    float lo = at(2*r, c), hi = at(2*r+1, c);
                    if (!std::isnan(lo) && !std::isnan(hi) && std::fabs(lo-hi) <= m_matchTol) d.push_back(lo-hi);
                }
                if (!d.empty()) { size_t mid=d.size()/2; std::nth_element(d.begin(),d.begin()+mid,d.end()); gOffset=d[mid]; }
            }
            offset = gOffset;
            int nChunks = 0;
            for (int p0 = 0; p0 < n; p0 += chunkPairs) {
                const int p1 = std::min(n, p0 + chunkPairs);       // 코어 [p0,p1)
                const int e0 = std::max(0, p0 - ov), e1 = std::min(n, p1 + ov);   // 확장 [e0,e1)
                float ofs = 0.f;
                auto blk = computeFiltered(e0, e1, removed, ofs, nullptr, nullptr, nullptr, gOffset);
                for (int r = p0; r < p1; ++r)                      // 코어 행만 기록(겹침 여백은 버림)
                    std::copy(&blk[(size_t)(r-e0)*w], &blk[(size_t)(r-e0)*w+w], &filtered[(size_t)r*w]);
                ++nChunks;
            }
            VISION_LOG_INFO("ExposureMerge2[청크]: {}개 청크(코어 {}행+겹침 {}행), 제거 {} px", nChunks, m_chunkRows, m_overlapRows, removed);
        }

        // ⑤ 출력 ZMap: 반해상도(n행, Y피치×2). halfRes=false면 각 행을 2배 복제해 원본 높이.
        auto makeOut = [&](std::vector<float> src) {   // by-value: 호출측에서 move로 넘겨 복사/할당 제거
            auto z = std::make_shared<ZMap>();
            z->width=w; z->xResMm=zm.xResMm; z->zResMm=zm.zResMm; z->zZeroCount=zm.zZeroCount;
            if (m_halfRes) {
                z->height=n; z->yResMm=zm.yResMm*2.f; z->data = std::move(src);
            } else {
                z->height=2*n; z->yResMm=zm.yResMm;
                z->data.resize((size_t)2*n*w);   // 모든 행을 아래 복사가 덮으므로 NaN 초기화 불필요
                cv::parallel_for_(cv::Range(0, n), [&](const cv::Range& rg) {
                    for (int r=rg.start;r<rg.end;++r) {
                        std::copy(&src[(size_t)r*w], &src[(size_t)r*w+w], &z->data[(size_t)(2*r)*w]);
                        std::copy(&src[(size_t)r*w], &src[(size_t)r*w+w], &z->data[(size_t)(2*r+1)*w]);
                    }
                });
            }
            return z;
        };
        // 실제 출력 = 최종 머지(리플렉션 제거). 항상 이것만 다운스트림으로 넘긴다.
        auto zFinal = makeOut(std::move(filtered));

        auto data = std::make_shared<VisionData>();
        data->zmap = zFinal;
        data->sourceId = input->sourceId;
        // 중간 단계는 결과창 드롭다운(디스플레이) 전용 — 전체모드 && !noPreview 일 때만(청크 모드는 최종만).
        if (!m_chunkMode && !m_noPreview && !mergedFull.empty()) {
            auto zMerged=makeOut(std::move(mergedFull)), zLow=makeOut(std::move(lowCFull)), zHigh=makeOut(std::move(highFull));
            data->stages = std::make_shared<std::vector<std::pair<std::string, ZMapPtr>>>();
            data->stages->push_back({ "1. 머지(리플렉션 제거)", zFinal });
            data->stages->push_back({ "2. 기본 머지",           zMerged });
            data->stages->push_back({ "3. 저노출(오프셋 보정)", zLow });
            data->stages->push_back({ "4. 장노출",             zHigh });
        }
        if (!m_chunkMode)
            VISION_LOG_INFO("ExposureMerge2: offset={:.1f}cnt, fill 리플렉션 제거 {} px (matchTol={}, tolX={}, tolY={})",
                            offset, removed, m_matchTol, m_tolX, m_tolY);
        return { ToolStatus::Ok, "", data };
    }
};

// 파일명 앞에 HHMMSS_ prefix를 붙여 반환 (예: output.png → 143022_output.png)
// 현재 시각을 HHMMSSmmm(시분초밀리초) 문자열로. 저장 파일명 접두사로 써서
// 폴더검사(초당 여러 장, 워커 병렬)에서도 파일명 충돌을 사실상 없앤다.
static std::string msStamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const long long ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[8];
    std::strftime(buf, sizeof(buf), "%H%M%S", &tm);
    std::string mm = std::to_string(ms);
    while (mm.size() < 3) mm = "0" + mm;
    return std::string(buf) + mm;   // HHMMSSmmm
}

// 저장 경로 생성: <folder>/HHMMSSmmm_<name>.<ext>
//   name = filename(지정 시), 비어있으면 소스 파일명(stem), 그것도 없으면 "output".
//   ext  = format(앞의 '.'은 무시). 밀리초 접두사로 병렬 저장에서도 겹치지 않음.
static std::string buildSavePath(const std::string& folder, const std::string& filename,
                                 const std::string& format, const std::string& sourceId) {
    namespace fs = std::filesystem;
    std::string stem = !filename.empty() ? filename
                     : (!sourceId.empty() ? fs::path(sourceId).stem().string() : std::string("output"));
    std::string ext = format;
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    if (ext.empty()) ext = "png";
    return (fs::path(folder) / (msStamp() + "_" + stem + "." + ext)).string();
}

// ── ImageSaver: 입력(ZMap 또는 Image2D)을 파일로 저장 (OpenCV cv::imwrite) ──────
//   ZMap → 16-bit(png/tif) 또는 8-bit(그 외, min-max 정규화). Image2D → 그대로.
class ImageSaverTool : public IAlgorithmTool {
    std::string m_folder, m_filename, m_format;
public:
    ImageSaverTool(std::string folder, std::string filename, std::string format)
        : m_folder(std::move(folder)), m_filename(std::move(filename)), m_format(std::move(format)) {}
    std::string name() const override { return "ImageSaver"; }

    ToolResult execute(VisionDataPtr input) override {
        if (m_folder.empty()) return { ToolStatus::Fail, "ImageSaver: 저장 폴더가 설정되지 않았습니다" };
        if (!input)           return { ToolStatus::Fail, "ImageSaver: 입력이 없습니다" };

        const std::string savePath = buildSavePath(m_folder, m_filename, m_format, input->sourceId);
        std::string ext = m_format;
        for (auto& ch : ext) ch = (char)std::tolower(ch);
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        const bool ext16 = (ext == "png" || ext == "tif" || ext == "tiff");

        try {
            if (input->hasImage()) {                       // 2D 이미지 그대로 저장
                const auto& im = *input->image;
                cv::Mat m(im.height, im.width, CV_8UC(im.channels), (void*)im.data.data());
                cv::Mat out = m;
                if (im.channels == 3) cv::cvtColor(m, out, cv::COLOR_RGB2BGR);
                else if (im.channels == 4) cv::cvtColor(m, out, cv::COLOR_RGBA2BGRA);
                if (!cv::imwrite(savePath, out)) return { ToolStatus::Fail, "ImageSaver: 저장 실패: " + savePath };
            }
            else if (input->hasZMap()) {                   // ZMap → 16bit(png/tif) 또는 8bit
                const ZMap& zm = *input->zmap;
                const size_t N = (size_t)zm.width * zm.height;
                if (ext16) {
                    cv::Mat m16(zm.height, zm.width, CV_16U);
                    uint16_t* d = (uint16_t*)m16.data;
                    for (size_t i = 0; i < N; ++i) {
                        float v = zm.data[i];
                        d[i] = std::isnan(v) ? 0 : (uint16_t)std::clamp(v, 0.f, 65535.f);  // 무효=0 (ZMapLoader 규약)
                    }
                    if (!cv::imwrite(savePath, m16)) return { ToolStatus::Fail, "ImageSaver: 저장 실패: " + savePath };
                } else {
                    float lo = 1e30f, hi = -1e30f;
                    for (size_t i = 0; i < N; ++i) { float v = zm.data[i]; if (!std::isnan(v)) { lo = std::min(lo,v); hi = std::max(hi,v); } }
                    float span = std::max(1e-6f, hi - lo);
                    cv::Mat m8(zm.height, zm.width, CV_8U);
                    for (size_t i = 0; i < N; ++i) {
                        float v = zm.data[i];
                        m8.data[i] = std::isnan(v) ? 0 : (uchar)std::clamp((v-lo)/span*255.f, 0.f, 255.f);
                    }
                    if (!cv::imwrite(savePath, m8)) return { ToolStatus::Fail, "ImageSaver: 저장 실패: " + savePath };
                }
            }
            else return { ToolStatus::Fail, "ImageSaver: 저장할 ZMap/이미지가 입력에 없습니다" };
        } catch (const std::exception& e) {
            return { ToolStatus::Fail, std::string("ImageSaver: ") + e.what() };
        }
        VISION_LOG_INFO("ImageSaver: 저장됨 → {}", savePath);
        return { ToolStatus::Ok, "", input };   // 통과 (탭 노드)
    }
};

// ── ZMapToCloud: ZMap(높이맵) → PointCloud3D. 유효 픽셀마다 (x,y,z)mm 점 생성. ──
//   x = (col-originCol)*xRes, y = (row-originRow)*yRes, z = (raw-zZero)*zRes (mm)
//   step으로 서브샘플(대용량 클라우드 감축). NaN(무효) 픽셀은 건너뜀.
class ZMapToCloudTool : public IAlgorithmTool {
    int m_step;
public:
    explicit ZMapToCloudTool(int step) : m_step(std::max(1, step)) {}
    std::string name() const override { return "ZMapToCloud"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->hasZMap())
            return { ToolStatus::Fail, "ZMap→Cloud: ZMap 입력이 필요합니다" };
        const ZMap& zm = *input->zmap;
        auto cloud = std::make_shared<PointCloud3D>();
        cloud->frameId = input->sourceId;
        cloud->points.reserve((size_t)(zm.width / m_step + 1) * (zm.height / m_step + 1));
        for (int row = 0; row < zm.height; row += m_step)
            for (int col = 0; col < zm.width; col += m_step) {
                if (!zm.valid(col, row)) continue;   // NaN 제외
                cloud->points.push_back({ zm.xMm(col), zm.yMm(row), zm.zMm(col, row) });
            }
        // 타입화 출력: 클라우드만 전달(다운스트림 저장/처리용). 결과창 이미지는 입력 zmap으로 폴백.
        auto out = std::make_shared<VisionData>();
        out->cloud    = cloud;
        out->sourceId = input->sourceId;
        VISION_LOG_INFO("ZMapToCloud: {} points (step={}, {}x{})",
                        cloud->points.size(), m_step, zm.width, zm.height);
        return { ToolStatus::Ok, "", out };
    }
};

// ── CloudSaver: PointCloud3D → 파일 저장. .xyz(텍스트) / .ply(ascii, 기본). ────
//   파일명 앞에 타임스탬프를 붙여 폴더검사 시 덮어쓰기 방지(ImageSaver와 동일 규약).
class CloudSaverTool : public IAlgorithmTool {
    std::string m_folder, m_filename, m_format;
public:
    CloudSaverTool(std::string folder, std::string filename, std::string format)
        : m_folder(std::move(folder)), m_filename(std::move(filename)), m_format(std::move(format)) {}
    std::string name() const override { return "CloudSaver"; }

    ToolResult execute(VisionDataPtr input) override {
        if (m_folder.empty())             return { ToolStatus::Fail, "CloudSaver: 저장 폴더가 설정되지 않았습니다" };
        if (!input || !input->hasCloud()) return { ToolStatus::Fail, "CloudSaver: PointCloud 입력이 없습니다. ZMap→Cloud를 먼저 연결하세요." };

        const std::string savePath = buildSavePath(m_folder, m_filename, m_format, input->sourceId);
        std::string ext = m_format;
        for (auto& ch : ext) ch = (char)std::tolower(ch);
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

        const auto& pts = input->cloud->points;
        std::ofstream ofs(savePath, std::ios::binary);
        if (!ofs) return { ToolStatus::Fail, "CloudSaver: 파일을 열 수 없습니다: " + savePath };

        if (ext == "xyz") {                       // 단순 텍스트: "x y z" 한 줄씩
            ofs << std::fixed << std::setprecision(4);
            for (const auto& p : pts) ofs << p.x << " " << p.y << " " << p.z << "\n";
        } else {                                  // PLY ascii (기본, CloudCompare/MeshLab 호환)
            ofs << "ply\nformat ascii 1.0\n";
            ofs << "element vertex " << pts.size() << "\n";
            ofs << "property float x\nproperty float y\nproperty float z\n";
            ofs << "end_header\n";
            ofs << std::fixed << std::setprecision(4);
            for (const auto& p : pts) ofs << p.x << " " << p.y << " " << p.z << "\n";
        }
        if (!ofs.good()) return { ToolStatus::Fail, "CloudSaver: 저장 중 오류: " + savePath };
        VISION_LOG_INFO("CloudSaver: {} points → {}", pts.size(), savePath);
        return { ToolStatus::Ok, "", input };     // 통과 (싱크)
    }
};

// ── GapFill: ZMap의 결측(NaN) 픽셀을 보간해 메움. ───────────────────────────
//   가장 가까운 유효 픽셀까지 거리 ≤ maxGap 인 결측만 채우고, 그보다 큰 구멍(중앙)은
//   NaN으로 남긴다(검사에서 가짜 표면을 지어내지 않기 위함).
//   method: neighbor(반복 이웃) / laplace(PDE) / nearest(최근접) / idw(역거리) / linear(행·열 선형)
//   출력 단계: 1.메운 결과 / 2.원본 / 3.메운 영역(마스크)
class GapFillTool : public IAlgorithmTool {
public:
    enum class Method { Neighbor, Median, Laplace, Nearest, Idw, Linear, Anisotropic };
private:
    Method m_method;
    int    m_maxGap, m_minValid, m_idwRadius, m_outputStage;
    float  m_idwPower, m_edgeSigma;
    bool   m_noPreview;
public:
    GapFillTool(Method m, int maxGap, int minValid, int idwRadius, float idwPower, float edgeSigma, int outputStage, bool noPreview)
        : m_method(m), m_maxGap(std::max(1,maxGap)), m_minValid(std::max(1,minValid)),
          m_idwRadius(std::max(1,idwRadius)), m_outputStage(outputStage), m_idwPower(idwPower),
          m_edgeSigma(edgeSigma), m_noPreview(noPreview) {}
    std::string name() const override { return "GapFill"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->hasZMap()) return { ToolStatus::Fail, "GapFill: ZMap 입력이 필요합니다" };
        const ZMap& zm = *input->zmap;
        const int w = zm.width, h = zm.height;
        const size_t N = (size_t)w * h;
        const float NaN = std::numeric_limits<float>::quiet_NaN();
        const auto& src = zm.data;

        // 유효/구멍 마스크 + 각 픽셀→가장 가까운 유효까지 거리
        cv::Mat data(h, w, CV_32F, const_cast<float*>(src.data()));
        cv::Mat validMask = (data == data);                 // 255=유효, 0=NaN
        cv::Mat holeMask;  cv::bitwise_not(validMask, holeMask);
        cv::Mat dist;      cv::distanceTransform(holeMask, dist, cv::DIST_L2, 3);  // 구멍=가장 가까운 유효까지 거리
        const float* dp = dist.ptr<float>();

        // 채울 대상: NaN 이고 거리 ≤ maxGap
        std::vector<uint8_t> fillable(N, 0);
        long target = 0;
        for (size_t i = 0; i < N; ++i)
            if (std::isnan(src[i]) && dp[i] <= (float)m_maxGap) { fillable[i] = 1; ++target; }

        std::vector<float> out = src;   // 유효 픽셀은 그대로 유지

        auto atc = [&](int r, int c) -> float { return out[(size_t)r*w + c]; };

        if (m_method == Method::Neighbor) {
            std::vector<float> cur = out;
            for (int it = 0; it < m_maxGap; ++it) {
                std::vector<float> nxt = cur;
                bool changed = false;
                for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
                    size_t i = (size_t)r*w + c;
                    if (!fillable[i] || !std::isnan(cur[i])) continue;
                    double s = 0; int cnt = 0;
                    for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) {
                        if (!dr && !dc) continue;
                        int nr = r+dr, nc = c+dc; if (nr<0||nr>=h||nc<0||nc>=w) continue;
                        float v = cur[(size_t)nr*w+nc]; if (!std::isnan(v)) { s += v; ++cnt; }
                    }
                    if (cnt >= m_minValid) { nxt[i] = (float)(s/cnt); changed = true; }
                }
                cur.swap(nxt);
                if (!changed) break;
            }
            out.swap(cur);
        }
        else if (m_method == Method::Median) {
            // 반복 이웃 '중앙값' — 평균과 달리 한쪽 값을 택해 단차(엣지)를 보존
            std::vector<float> cur = out;
            for (int it = 0; it < m_maxGap; ++it) {
                std::vector<float> nxt = cur;
                bool changed = false;
                for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
                    size_t i = (size_t)r*w + c;
                    if (!fillable[i] || !std::isnan(cur[i])) continue;
                    float vals[8]; int cnt = 0;
                    for (int dr=-1;dr<=1;++dr) for (int dc=-1;dc<=1;++dc) {
                        if (!dr && !dc) continue;
                        int nr=r+dr, nc=c+dc; if(nr<0||nr>=h||nc<0||nc>=w) continue;
                        float v = cur[(size_t)nr*w+nc]; if(!std::isnan(v)) vals[cnt++]=v;
                    }
                    if (cnt >= m_minValid) {
                        std::nth_element(vals, vals+cnt/2, vals+cnt);
                        nxt[i] = vals[cnt/2]; changed = true;
                    }
                }
                cur.swap(nxt);
                if (!changed) break;
            }
            out.swap(cur);
        }
        else if (m_method == Method::Nearest) {
            std::vector<uint8_t> done(N, 0);
            std::deque<int> q;
            for (size_t i = 0; i < N; ++i) if (!std::isnan(src[i])) { done[i]=1; q.push_back((int)i); }
            const int d4r[4]={-1,1,0,0}, d4c[4]={0,0,-1,1};
            while (!q.empty()) {
                int i = q.front(); q.pop_front(); int r=i/w, c=i%w;
                for (int k=0;k<4;++k) {
                    int nr=r+d4r[k], nc=c+d4c[k]; if(nr<0||nr>=h||nc<0||nc>=w) continue;
                    size_t j=(size_t)nr*w+nc;
                    if (!done[j] && fillable[j]) { out[j]=out[i]; done[j]=1; q.push_back((int)j); }
                }
            }
        }
        else if (m_method == Method::Idw) {
            const int R = m_idwRadius;
            for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
                size_t i = (size_t)r*w + c; if (!fillable[i]) continue;
                double s = 0, ws = 0;
                for (int dr=-R; dr<=R; ++dr) for (int dc=-R; dc<=R; ++dc) {
                    int nr=r+dr, nc=c+dc; if(nr<0||nr>=h||nc<0||nc>=w) continue;
                    float v = src[(size_t)nr*w+nc]; if (std::isnan(v)) continue;
                    double d = std::sqrt((double)dr*dr + (double)dc*dc);
                    if (d < 1e-6 || d > R) continue;
                    double wt = 1.0 / std::pow(d, (double)m_idwPower);
                    s += wt*v; ws += wt;
                }
                if (ws > 0) out[i] = (float)(s/ws);
            }
        }
        else if (m_method == Method::Linear) {
            std::vector<float> rowF(N, NaN), colF(N, NaN);
            // 행 방향: 유효로 둘러싸인 NaN 구간을 양끝 값으로 선형보간
            for (int r = 0; r < h; ++r) {
                int c = 0;
                while (c < w) {
                    if (!std::isnan(src[(size_t)r*w+c])) { ++c; continue; }
                    int s0 = c; while (c < w && std::isnan(src[(size_t)r*w+c])) ++c;
                    int left = s0-1, right = c;
                    if (left >= 0 && right < w) {
                        float vl = src[(size_t)r*w+left], vr = src[(size_t)r*w+right];
                        for (int k=s0;k<right;++k){ float t=(float)(k-left)/(right-left); rowF[(size_t)r*w+k]=vl*(1-t)+vr*t; }
                    }
                }
            }
            // 열 방향
            for (int c = 0; c < w; ++c) {
                int r = 0;
                while (r < h) {
                    if (!std::isnan(src[(size_t)r*w+c])) { ++r; continue; }
                    int s0 = r; while (r < h && std::isnan(src[(size_t)r*w+c])) ++r;
                    int top = s0-1, bot = r;
                    if (top >= 0 && bot < h) {
                        float vt = src[(size_t)top*w+c], vb = src[(size_t)bot*w+c];
                        for (int k=s0;k<bot;++k){ float t=(float)(k-top)/(bot-top); colF[(size_t)k*w+c]=vt*(1-t)+vb*t; }
                    }
                }
            }
            for (size_t i=0;i<N;++i) {
                if (!fillable[i]) continue;
                bool hr=!std::isnan(rowF[i]), hc=!std::isnan(colF[i]);
                if (hr && hc) out[i]=(rowF[i]+colF[i])*0.5f;
                else if (hr)  out[i]=rowF[i];
                else if (hc)  out[i]=colF[i];
            }
        }
        else if (m_method == Method::Anisotropic) {
            // 엣지 보존 확산: nearest로 초기화(엣지 대략 배치) 후, 값 차이가 크면(엣지)
            // 그 방향으로는 섞지 않는 가중 확산으로 면 안쪽만 매끈하게. 단차 보존.
            std::vector<uint8_t> done(N, 0);
            std::deque<int> q;
            for (size_t i=0;i<N;++i) if(!std::isnan(src[i])){ done[i]=1; q.push_back((int)i); }
            const int a4r[4]={-1,1,0,0}, a4c[4]={0,0,-1,1};
            while(!q.empty()){
                int i=q.front(); q.pop_front(); int r=i/w,c=i%w;
                for(int k=0;k<4;++k){ int nr=r+a4r[k],nc=c+a4c[k]; if(nr<0||nr>=h||nc<0||nc>=w)continue;
                    size_t j=(size_t)nr*w+nc; if(!done[j]&&fillable[j]){ out[j]=out[i]; done[j]=1; q.push_back((int)j);} }
            }
            const float sig = std::max(1.f, m_edgeSigma);
            const float inv2s2 = 1.f / (2.f*sig*sig);
            const int maxIter = std::min(500, m_maxGap*6 + 30);
            for (int it=0; it<maxIter; ++it) {
                double maxDelta = 0;
                for (int r=0;r<h;++r) for(int c=0;c<w;++c){
                    size_t i=(size_t)r*w+c; if(!fillable[i]) continue;
                    float ci=out[i]; double s=0, ws=0;
                    auto acc=[&](int nr,int nc){ if(nr<0||nr>=h||nc<0||nc>=w)return; float v=out[(size_t)nr*w+nc];
                        if(std::isnan(v))return; float d=v-ci; float wt=std::exp(-d*d*inv2s2); s+=wt*v; ws+=wt; };
                    acc(r-1,c); acc(r+1,c); acc(r,c-1); acc(r,c+1);
                    if(ws>0){ float nv=(float)(s/ws); maxDelta=std::max(maxDelta,(double)std::fabs(nv-ci)); out[i]=nv; }
                }
                if (maxDelta < 1e-3) break;
            }
        }
        else {  // Laplace (Gauss-Seidel 반복)
            double meanV = 0; long vc = 0;
            for (size_t i=0;i<N;++i) if(!std::isnan(src[i])){ meanV+=src[i]; ++vc; }
            float init = vc ? (float)(meanV/vc) : 0.f;
            for (size_t i=0;i<N;++i) if (fillable[i]) out[i]=init;
            const int maxIter = std::min(3000, m_maxGap*m_maxGap*6 + 100);
            for (int it=0; it<maxIter; ++it) {
                double maxDelta = 0;
                for (int r=0;r<h;++r) for (int c=0;c<w;++c) {
                    size_t i=(size_t)r*w+c; if(!fillable[i]) continue;
                    double s=0; int cnt=0;
                    if (r>0)   { float v=atc(r-1,c); if(!std::isnan(v)){s+=v;++cnt;} }
                    if (r<h-1) { float v=atc(r+1,c); if(!std::isnan(v)){s+=v;++cnt;} }
                    if (c>0)   { float v=atc(r,c-1); if(!std::isnan(v)){s+=v;++cnt;} }
                    if (c<w-1) { float v=atc(r,c+1); if(!std::isnan(v)){s+=v;++cnt;} }
                    if (cnt>0) { float nv=(float)(s/cnt); maxDelta=std::max(maxDelta,(double)std::fabs(nv-out[i])); out[i]=nv; }
                }
                if (maxDelta < 1e-3) break;
            }
        }

        long filled = 0;
        for (size_t i=0;i<N;++i) if (fillable[i] && !std::isnan(out[i])) ++filled;

        // 출력 ZMap (메타 유지). 미리보기 생략 모드면 실제 출력 단계 하나만 만든다.
        const int si = std::clamp(m_outputStage, 0, 2);
        auto mk = [&](const std::vector<float>& d){ auto z=std::make_shared<ZMap>(zm); z->data=d; return z; };
        std::vector<float> maskData;
        if (si == 2 || !m_noPreview) {
            maskData.assign(N, NaN);
            for (size_t i=0;i<N;++i) maskData[i] = (fillable[i] && !std::isnan(out[i])) ? 1.f
                                               : (std::isnan(src[i]) ? NaN : 0.f);
        }
        ZMapPtr zFilled = (si==0 || !m_noPreview) ? mk(out) : nullptr;
        ZMapPtr zOrig   = (si==1 || !m_noPreview) ? mk(src) : nullptr;
        ZMapPtr zMask   = (si==2 || !m_noPreview) ? mk(maskData) : nullptr;

        auto vd = std::make_shared<VisionData>();
        vd->zmap = (si==0) ? zFilled : (si==1) ? zOrig : zMask;
        vd->sourceId = input->sourceId;
        if (!m_noPreview) {
            vd->stages = std::make_shared<std::vector<std::pair<std::string, ZMapPtr>>>();
            vd->stages->push_back({ "1. 메운 결과", zFilled });
            vd->stages->push_back({ "2. 원본",      zOrig });
            vd->stages->push_back({ "3. 메운 영역", zMask });
        }
        VISION_LOG_INFO("GapFill: 채움 {} / 대상 {} px (maxGap={})", filled, target, m_maxGap);
        return { ToolStatus::Ok, "", vd };
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
    const nlohmann::json& p,
    bool noPreview)
{
    if (type == "ZMapLoader") {
        return std::make_shared<ZMapLoaderTool>(
            p.value("path",    ""),
            p.value("folder",  ""),
            p.value("xResMm",  1.0f),
            p.value("yResMm",  1.0f),
            p.value("zResMm",  0.001f));
    }
    if (type == "RowStretch") {
        std::vector<RowStretchTool::Band> bands;
        if (p.contains("rois") && p["rois"].is_array())
            for (const auto& r : p["rois"])
                bands.push_back({ r.value("yPct", 0.f), r.value("hPct", 1.f), r.value("scale", 2) });
        return std::make_shared<RowStretchTool>(std::move(bands));
    }
    if (type == "ExposureMerge") {
        // 이중노출 분리: 저노출(확장)/장노출 중 outputStage로 선택 (0/1).
        // 기존 레시피 호환: outputStage 2~4는 clamp되어 장노출로 처리됨.
        return std::make_shared<ExposureMergeTool>(
            p.value("outputStage", 0),
            noPreview);
    }
    if (type == "ExposureMerge2") {
        return std::make_shared<DualExposureMergeTool>(
            p.value("matchTol",    20.0f),
            p.value("reflTol",     30.0f),
            p.value("tolX",        10.0f),
            p.value("tolY",        100.0f),
            p.value("gapK",        2),
            p.value("halfRes",     true),
            noPreview,    // 검사(배치)면 최종 출력 1개만 생성 → 중간단계(디스플레이) 생략
            p.value("chunkMode",   false),   // 청크 모드 off → 전체 이미지 연산(기존 동작)
            p.value("chunkRows",   1000),    // 청크당 입력 프로파일(행) 수 (겹침 부담 희석 위해 크게)
            p.value("overlapRows", 320));    // 청크 겹침 행 수 (리플렉션 제거 연결성 확보; 검증상 ≥320이면 전체모드와 동일)
    }
    if (type == "ImageLoader") {
        return std::make_shared<ImageLoaderTool>(p.value("path", ""));
    }
    if (type == "ImageSaver") {
        // folder(필수)+filename(선택)+format. 구버전 호환: path만 있으면 분해.
        std::string folder = p.value("folder", ""), filename = p.value("filename", ""), format = p.value("format", "png");
        if (folder.empty()) {
            std::string path = p.value("path", "");
            if (!path.empty()) {
                std::filesystem::path pp(path);
                folder = pp.parent_path().string(); filename = pp.stem().string();
                std::string e = pp.extension().string(); if (!e.empty() && e[0]=='.') e = e.substr(1);
                if (!e.empty()) format = e;
            }
        }
        return std::make_shared<ImageSaverTool>(folder, filename, format);
    }
    if (type == "ZMapToCloud") {
        return std::make_shared<ZMapToCloudTool>(p.value("step", 1));
    }
    if (type == "GapFill") {
        std::string ms = p.value("method", "neighbor");
        GapFillTool::Method m = GapFillTool::Method::Neighbor;
        if      (ms == "median")      m = GapFillTool::Method::Median;
        else if (ms == "laplace")     m = GapFillTool::Method::Laplace;
        else if (ms == "nearest")     m = GapFillTool::Method::Nearest;
        else if (ms == "idw")         m = GapFillTool::Method::Idw;
        else if (ms == "linear")      m = GapFillTool::Method::Linear;
        else if (ms == "anisotropic") m = GapFillTool::Method::Anisotropic;
        return std::make_shared<GapFillTool>(m,
            p.value("maxGap", 5),
            p.value("minValidNeighbors", 3),
            p.value("idwRadius", 8),
            p.value("idwPower", 2.0f),
            p.value("edgeSigma", 30.0f),
            p.value("outputStage", 0),
            noPreview);
    }
    if (type == "CloudSaver") {
        std::string folder = p.value("folder", ""), filename = p.value("filename", ""), format = p.value("format", "ply");
        if (folder.empty()) {
            std::string path = p.value("path", "");
            if (!path.empty()) {
                std::filesystem::path pp(path);
                folder = pp.parent_path().string(); filename = pp.stem().string();
                std::string e = pp.extension().string(); if (!e.empty() && e[0]=='.') e = e.substr(1);
                if (!e.empty()) format = e;
            }
        }
        return std::make_shared<CloudSaverTool>(folder, filename, format);
    }
    if (type == "NoiseFilter") {
        NoiseFilter::Params params;
        std::string ft = p.value("filterType", "median");
        if      (ft == "mean")      params.type = NoiseFilter::Type::Mean;
        else if (ft == "gaussian")  params.type = NoiseFilter::Type::Gaussian;
        else if (ft == "sor")       params.type = NoiseFilter::Type::SOR;
        else if (ft == "bilateral") params.type = NoiseFilter::Type::Bilateral;
        else                        params.type = NoiseFilter::Type::Median;
        params.kernelSizeX  = p.value("kernelSizeX",  p.value("kernelSize", 3));
        params.kernelSizeY  = p.value("kernelSizeY",  p.value("kernelSize", 3));
        params.stdRatio     = p.value("stdRatio",     2.0f);
        params.sigmaRangeMm = p.value("sigmaRangeMm", 0.02f);
        params.radius       = p.value("radius",       1.0f);
        params.minNeighbors = p.value("minNeighbors", 5);
        // rois: 사각형만 (비어있으면 전체 이미지에 필터 적용)
        if (p.contains("rois") && p["rois"].is_array()) {
            for (const auto& r : p["rois"]) {
                if (r.value("shape", std::string("rect")) != "rect") continue;
                NoiseFilter::RoiRect roi;
                roi.xPct = r.value("xPct", 0.f);
                roi.yPct = r.value("yPct", 0.f);
                roi.wPct = r.value("wPct", 1.f);
                roi.hPct = r.value("hPct", 1.f);
                params.rois.push_back(roi);
            }
        }
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

        // rois 배열: type=='mask'는 제외 영역, 그 외는 measure. shape=rect/circle/polygon.
        if (p.contains("rois") && p["rois"].is_array()) {
            for (const auto& r : p["rois"]) {
                HeightFromPlaneParams::ROI roi;
                roi.xPct = r.value("xPct", 0.f);
                roi.yPct = r.value("yPct", 0.f);
                roi.wPct = r.value("wPct", 1.f);
                roi.hPct = r.value("hPct", 1.f);
                const std::string shape = r.value("shape", std::string("rect"));
                roi.isCircle = (shape == "circle");
                if (shape == "polygon" && r.contains("points") && r["points"].is_array())
                    for (const auto& pt : r["points"])
                        roi.poly.push_back({ pt.value("x", 0.f), pt.value("y", 0.f) });
                if (r.value("type", std::string("measure")) == "mask")
                    params.maskRois.push_back(roi);
                else
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
        // rois 배열 → 각각 검색 영역 (xPct/yPct/wPct/hPct/angleDeg)
        if (p.contains("rois") && p["rois"].is_array()) {
            for (const auto& r : p["rois"]) {
                LineCenterParams::ROI roi;
                roi.xPct = r.value("xPct", 0.f);
                roi.yPct = r.value("yPct", 0.f);
                roi.wPct = r.value("wPct", 1.f);
                roi.hPct = r.value("hPct", 1.f);
                roi.angleDeg = r.value("angleDeg", 0.f);
                roi.polarity = (r.value("polarity", "d2l") == "l2d")
                             ? Polarity::LightToDark : Polarity::DarkToLight;
                params.rois.push_back(roi);
            }
        }
        std::string sdir = p.value("scanDir", "lr");
        if      (sdir == "rl") params.scanDir = ScanDir::Rl;
        else if (sdir == "tb") params.scanDir = ScanDir::Tb;
        else if (sdir == "bt") params.scanDir = ScanDir::Bt;
        else                   params.scanDir = ScanDir::Lr;
        params.threshold = p.value("threshold", 1.f);
        params.xRoi = p.value("xRoi", 0);
        params.yRoi = p.value("yRoi", 0);
        return std::make_shared<LineCenterTool>(params);
    }
    if (type == "Align") {
        return std::make_shared<AlignTool>();
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
