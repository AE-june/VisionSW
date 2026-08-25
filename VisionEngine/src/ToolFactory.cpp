#include "ToolFactory.h"
#include "HeightMapCache.h"
#include "NoiseFilter.h"
#include "ProfileToCloudTool.h"
#include "PlaneFitTool.h"
#include "LineFitTool.h"
#include "HeightMapToCloudTool.h"
#include "CloudLoaderTool.h"
#include "CloudMergeTool.h"
#include "CloudToZmapTool.h"
#include "CloudSorFilterTool.h"
#include "CloudToProfilesTool.h"
#include "RowStretchTool.h"
#include "GapFillTool.h"
#include "CsvWriterTool.h"
#include "LineCenterTool.h"
#include "AlignTool.h"
#include "ThresholdTool.h"
#include "CreateRoiTool.h"
#include "ReduceDomainTool.h"
#include "RegionMeasureTool.h"
#include "ValidRegionTool.h"
#include "LevelTool.h"
#include "SurfaceCropTool.h"
#include "SurfaceResampleTool.h"
#include "SurfaceSubtractTool.h"
#include "NotchMeasureTool.h"
#include "NotchMeasureV2Tool.h"
#include "ExtractProfileTool.h"
#include "ProfileFeatureTool.h"
#include "CompareTool.h"
#include "CombineDecisionTool.h"
#include "ExposureMergeCore.h"
#include "HeightMapSidecar.h"
#include "IHeightMapLoader.h"
#include "VisionData.h"
#include "HeightMap.h"
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
// STBI_WINDOWS_UTF8: stbi__fopen이 char* 경로를 시스템 ANSI 코드페이지가 아니라
// UTF-8로 해석해 _wfopen으로 열도록 함. 없으면 비-ASCII(한글 등) 경로의 파일을
// 전혀 못 읽는다(코드페이지에 없는 문자는 fopen 자체가 실패).
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// OpenCV for saving (16-bit PNG/TIFF + 일반 포맷)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace vision {

// ── HeightMap 파일 글로벌 캐시 (폴더검사 시 반복 IO 제거) ──────────────────────
std::unordered_map<std::string, std::shared_ptr<HeightMap>> g_heightmapFileCache;
std::unordered_set<std::string> g_preloadedFolders;
std::mutex g_heightmapFileCacheMtx;

// 파일 캐시 상한 — 최근 N장만 유지(폴더 브라우징·연속 로드로 메모리 무한 누적 방지).
//  삽입 순서(FIFO)로 오래된 것 축출. 사용 중(shared_ptr 참조)인 HeightMap은 map에서 빠져도 안전히 유지됨.
//  반드시 g_heightmapFileCacheMtx를 보유한 상태에서 호출할 것.
static const size_t HEIGHTMAP_CACHE_CAP = 8;
static std::deque<std::string> g_heightmapCacheOrder;
void heightmapCachePut(const std::string& path, const std::shared_ptr<HeightMap>& zm) {
    if (g_heightmapFileCache.find(path) == g_heightmapFileCache.end()) g_heightmapCacheOrder.push_back(path);
    g_heightmapFileCache[path] = zm;
    while (g_heightmapCacheOrder.size() > HEIGHTMAP_CACHE_CAP) {
        std::string old = g_heightmapCacheOrder.front();
        g_heightmapCacheOrder.pop_front();
        if (old != path) g_heightmapFileCache.erase(old);   // 방금 넣은 건 축출 안 함
    }
}

std::shared_ptr<HeightMap> loadHeightMapFromFile(const std::string& path,
                                       float xRes, float yRes, float zRes) {
    int w, h, ch;
    uint16_t* raw16 = stbi_load_16(path.c_str(), &w, &h, &ch, 1);
    if (raw16) {
        auto heightmap = std::make_shared<HeightMap>();
        heightmap->width=w; heightmap->height=h;
        heightmap->xResMm=xRes; heightmap->yResMm=yRes; heightmap->zResMm=zRes;
        heightmap->zZeroCount=32768.f;
        heightmap->data.resize((size_t)w*h);
        for (int i=0;i<w*h;++i)
            heightmap->data[i] = raw16[i]==0 ? std::numeric_limits<float>::quiet_NaN()
                                        : static_cast<float>(raw16[i]);
        stbi_image_free(raw16);
        return heightmap;
    }
    unsigned char* raw8 = stbi_load(path.c_str(), &w, &h, &ch, 1);
    if (!raw8) return nullptr;
    auto heightmap = std::make_shared<HeightMap>();
    heightmap->width=w; heightmap->height=h;
    heightmap->xResMm=xRes; heightmap->yResMm=yRes; heightmap->zResMm=zRes;
    heightmap->zZeroCount=128.f;
    heightmap->data.resize((size_t)w*h);
    for (int i=0;i<w*h;++i)
        heightmap->data[i] = raw8[i]==0 ? std::numeric_limits<float>::quiet_NaN()
                                   : static_cast<float>(raw8[i]);
    stbi_image_free(raw8);
    return heightmap;
}

int preloadFolder(const std::string& folder, float xRes, float yRes, float zRes) {
    namespace fs = std::filesystem;
    // Collect files not yet cached
    std::vector<std::string> toLoad;
    {
        std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
        if (g_preloadedFolders.count(folder)) return 0;
        g_preloadedFolders.insert(folder);
        std::error_code ec;
        // folder는 UTF-8 문자열 — u8path로 넣어야 한글 등 비-ASCII 경로를 찾을 수 있고,
        // u8string으로 꺼내야 나중에 stbi_load(UTF-8 가정)로 다시 넘길 때 왕복이 맞는다.
        for (auto& e : fs::directory_iterator(fs::u8path(folder), ec)) {
            if (e.path().extension() == ".png") {
                std::string fp = e.path().u8string();
                if (!g_heightmapFileCache.count(fp))
                    toLoad.push_back(fp);
            }
        }
    }
    if (toLoad.empty()) return 0;

    // Load in parallel using hardware concurrency
    const int nThreads = static_cast<int>(std::thread::hardware_concurrency());
    const int n = static_cast<int>(toLoad.size());
    std::vector<std::pair<std::string, std::shared_ptr<HeightMap>>> results(n);
    std::atomic<int> idx{0};

    auto worker = [&]() {
        int i;
        while ((i = idx.fetch_add(1)) < n) {
            results[i] = { toLoad[i], loadHeightMapFromFile(toLoad[i], xRes, yRes, zRes) };
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(nThreads);
    for (int t = 0; t < nThreads; ++t)
        threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    int loaded = 0;
    {
        std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
        for (auto& [path, zm] : results)
            if (zm) { heightmapCachePut(path, zm); ++loaded; }
    }
    return loaded;
}

// ── Loader tools (defined here, used by ToolFactory) ─────────────────────

class HeightMapLoaderTool : public IAlgorithmTool {
    std::string m_path;
    std::string m_folder;
    float m_xResMm, m_yResMm, m_zResMm;
public:
    // 폴더 전체 프리로드는 여기서 하지 않음 — 인터랙티브 편집은 ParamPanel이 폴더 선택 시
    // 명시적으로 "preload" 커맨드를 보내고, 폴더검사(배치)는 워커별로 필요한 파일만
    // "prefetch" 커맨드로 미리 당겨오므로 생성자에서 폴더 전체를 긁으면 워커마다 중복 로드됨.
    HeightMapLoaderTool(std::string path, std::string folder, float xRes, float yRes, float zRes)
        : m_path(std::move(path)), m_folder(std::move(folder)),
          m_xResMm(xRes), m_yResMm(yRes), m_zResMm(zRes)
    {}
    std::string name() const override { return "HeightMapLoader"; }

    ToolResult execute(VisionDataPtr) override {
        if (m_path.empty())
            return { ToolStatus::Fail, "HeightMapLoader: 파일 경로가 설정되지 않았습니다" };

        // 캐시 확인
        {
            std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
            auto it = g_heightmapFileCache.find(m_path);
            if (it != g_heightmapFileCache.end()) {
                // 캐시엔 픽셀 데이터만 신뢰 — 분해능은 로더 파라미터가 최신이므로 캐시 히트 시에도 재적용.
                // (경로만으로 캐시돼 처음 로드 분해능이 박히던 문제 수정: 분해능 변경이 이제 즉시 반영)
                it->second->xResMm = m_xResMm;
                it->second->yResMm = m_yResMm;
                it->second->zResMm = m_zResMm;
                auto data = std::make_shared<VisionData>();
                data->setHeightMap(it->second);
                data->sourceId = m_path;
                VISION_LOG_INFO("HeightMapLoader: cache hit {} (res x{} y{} z{})", m_path, m_xResMm, m_yResMm, m_zResMm);
                return { ToolStatus::Ok, "", data };
            }
        }

        // 캐시 미스 → 파일 로드 후 캐시 저장
        auto heightmap = loadHeightMapFromFile(m_path, m_xResMm, m_yResMm, m_zResMm);
        if (!heightmap)
            return { ToolStatus::Fail, "HeightMapLoader: 파일을 읽을 수 없습니다: " + m_path };

        // 사이드카 JSON이 있으면 메타데이터를 덮어씀 (HeightMapSaver가 함께 저장한 것)
        if (auto meta = readSidecar(m_path))
            applySidecar(*heightmap, *meta);

        {
            std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
            heightmapCachePut(m_path, heightmap);
        }
        VISION_LOG_INFO("HeightMapLoader: {}x{} loaded from {}", heightmap->width, heightmap->height, m_path);
        auto data = std::make_shared<VisionData>();
        data->setHeightMap(heightmap);
        data->sourceId = m_path;
        return { ToolStatus::Ok, "", data };
    }
};

// ── ExposureSplit (다중노출 분리): 인터리브된 다중노출 HeightMap을 노출별로 행 분리.
//   splitCount=2: 짝/홀 행 = 저/장 노출. splitCount=3: r%3=0/1/2 = 저/중/장 노출.
//   행 = r*splitCount + phase. 행확장/보간 없이 각 노출을 n(=h/splitCount)행 그대로 출력.
//   출력: outputStage로 노출 하나 선택. (머지/리플렉션 제거는 ExposureMerge2/3 노드가 담당)
// A5-6: outputStage 파라미터 제거 → stages에 전부 싣고 UI가 고른다.
//   주 출력은 항상 stage 0(저노출). noPreview 시 stages 생략(배치 가속).
class ExposureMergeTool : public IAlgorithmTool {
    int   m_splitCount;
    bool  m_noPreview;
public:
    ExposureMergeTool(int splitCount, bool noPreview)
        : m_splitCount(std::clamp(splitCount, 2, 3)), m_noPreview(noPreview) {}
    std::string name() const override { return "ExposureMerge"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->inHeightMap(0))
            return { ToolStatus::Fail, "ExposureSplit: HeightMap 입력이 필요합니다" };

        const auto& zm = *input->inHeightMap(0);
        const int w = zm.width, h = zm.height;
        const int sc = m_splitCount;
        if (h < sc) return { ToolStatus::Fail, "ExposureSplit: 이미지 높이가 분할 수보다 작습니다" };

        const int n = h / sc;

        auto extract = [&](int phase) {
            std::vector<float> half((size_t)n * w);
            for (int r = 0; r < n; ++r)
                for (int c = 0; c < w; ++c)
                    half[(size_t)r*w + c] = zm.data[(size_t)(r*sc + phase)*w + c];
            return half;
        };
        auto makeZRaw = [&](std::vector<float> half) {
            auto z = std::make_shared<HeightMap>();
            z->width=w; z->height=n;
            z->xResMm=zm.xResMm; z->yResMm=zm.yResMm;
            z->zResMm=zm.zResMm; z->zZeroCount=zm.zZeroCount;
            z->originCol=zm.originCol; z->originRow=zm.originRow;
            z->data=std::move(half);
            return z;
        };

        static const char* const label2[] = { "1. 저노출", "2. 장노출" };
        static const char* const label3[] = { "1. 저노출", "2. 중노출", "3. 장노출" };
        const char* const* labels = (sc == 3) ? label3 : label2;

        auto data = std::make_shared<VisionData>();
        data->sourceId = input->sourceId;
        // 주 출력 = 항상 stage 0 (저노출)
        auto z0 = makeZRaw(extract(0));
        data->setHeightMap(z0);
        if (!m_noPreview) {
            data->stages = std::make_shared<std::vector<std::pair<std::string, HeightMapPtr>>>();
            data->stages->push_back({ labels[0], z0 });
            for (int p = 1; p < sc; ++p)
                data->stages->push_back({ labels[p], makeZRaw(extract(p)) });
        }
        VISION_LOG_INFO("ExposureSplit: {}x{} → {}분할, 노출당 {}행", w, h, sc, n);
        return { ToolStatus::Ok, "", data };
    }
};

// RowStretchTool → VisionTools/RowStretchTool.{h,cpp} 로 이동 (A2)

// ── DualExposureMerge (이중노출 머지, 재구현): 인터리브 홀짝 → 오프셋보정 →
//    저노출우선 머지 → 연속성(영역성장) 필터로 fill 리플렉션 제거 → 반해상도 출력.
//    규칙: 겹침은 저노출 우선(리플 자동배제), fill은 신뢰 씨앗에서 연결성으로 검증.
//    [증분1] ① 연속성 주력. ② 신뢰표면편차+I/LLT 게이팅, I중앙값 홀짝판별, 자동보정은 추후.
// A5-1: halfRes=true → 새 프레임 정의(nodeId가 있을 때).
// A5-2: chunkMode/chunkRows/overlapRows 레시피에서 제거 → 엔진 자동 판단(n > 4096 시 청크).
class DualExposureMergeTool : public IAlgorithmTool {
    float m_matchTol;
    float m_reflTol;
    float m_tolX, m_tolY;
    int   m_gapK;
    bool  m_halfRes;
    bool  m_noPreview;
    std::string m_nodeId;  // A5-1: 새 프레임 정의용 (비어있으면 프레임 정의 안 함)
public:
    DualExposureMergeTool(float matchTol, float reflTol, float tolX, float tolY,
                          int gapK, bool halfRes, bool noPreview, std::string nodeId)
        : m_matchTol(matchTol), m_reflTol(reflTol), m_tolX(tolX), m_tolY(tolY),
          m_gapK(gapK), m_halfRes(halfRes), m_noPreview(noPreview), m_nodeId(std::move(nodeId)) {}
    std::string name() const override { return "ExposureMerge2"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->inHeightMap(0))
            return { ToolStatus::Fail, "이중노출 머지: HeightMap 입력이 필요합니다" };
        const auto& zm = *input->inHeightMap(0);
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
            // ②③④ 공유 코어: 오프셋 → 저노출우선 → 연속성 BFS → 셀별 source(0제거/1저/2고)
            //   seedTol=m_reflTol(<0이면 코어가 matchTol로 폴백 → 기존 동작). offSamples로 오프셋 표본수 확인.
            std::vector<uint8_t> source;
            int offSamples = -1;
            float offset = exposureMergeDecision(low.data(), high.data(), w, bn, m_matchTol, m_tolX, m_tolY, m_gapK,
                                                 forcedOffset, source, nullptr, true, m_reflTol, &offSamples);
            if (offSamples == 0)
                VISION_LOG_INFO("ExposureMerge2: 경고 — 겹침 일치 표본 0개 → 오프셋 보정 건너뜀(offset=0). matchTol을 키우거나 노출 정렬을 확인하세요.");
            offsetOut = offset;
            // source → 최종 Z: 저=low-offset, 고=high, 제거=NaN. (제거된 fill 리플렉션 카운트)
            std::vector<float> filtered(BN);
            std::atomic<long> removedA{0};
            cv::parallel_for_(cv::Range(0, bn), [&](const cv::Range& rg) {
                long loc = 0;
                for (size_t i=(size_t)rg.start*w; i<(size_t)rg.end*w; ++i) {
                    uint8_t s = source[i];
                    if      (s == 1) filtered[i] = low[i] - offset;
                    else if (s == 2) filtered[i] = high[i];
                    else { filtered[i] = NaN; if (!std::isnan(high[i])) ++loc; }
                }
                removedA += loc;
            });
            removedOut += removedA.load();
            // 디스플레이 중간단계 재구성(전체모드 + !noPreview): lowC=low-offset, merged=저우선(리플제거 전), high
            if (outLowC) {
                std::vector<float> lc(BN);
                for (size_t i=0;i<BN;++i) lc[i] = std::isnan(low[i]) ? NaN : low[i]-offset;
                *outLowC = std::move(lc);
            }
            if (outMerged) {
                std::vector<float> mg(BN);
                for (size_t i=0;i<BN;++i) { float lc=std::isnan(low[i])?NaN:low[i]-offset; mg[i]=!std::isnan(lc)?lc:(!std::isnan(high[i])?high[i]:NaN); }
                *outMerged = std::move(mg);
            }
            if (outHigh) *outHigh = high;
            return filtered;
        };

        // ── 전체 이미지 vs 청크 실행 ─────────────────────────────────────────────
        // A5-2: chunkMode 자동 판단. chunkRows/overlapRows는 고정 상수.
        const bool chunkMode = (n > 4096);
        const int chunkRowsAuto = 1000, overlapRowsAuto = 320;
        long removed = 0; float offset = 0.f;
        std::vector<float> lowCFull, highFull, mergedFull;   // 디스플레이 단계용(전체모드 + !noPreview)
        std::vector<float> filtered;
        if (!chunkMode) {
            // 청크 미사용: 기존처럼 전체 이미지에 대해 한 번에 연산.
            const bool wantStages = !m_noPreview;
            filtered = computeFiltered(0, n, removed, offset,
                wantStages ? &lowCFull : nullptr, wantStages ? &highFull : nullptr, wantStages ? &mergedFull : nullptr, NaN);
        } else {
            // 청크 모드: 코어 청크를 위·아래 겹침만큼 확장해 처리하고, 코어 행만 출력에 기록.
            //   겹침은 BFS 연속성 컨텍스트를 청크 경계 너머까지 확보해 이음매 결함을 방지.
            filtered.assign((size_t)n*w, NaN);
            const int chunkPairs = std::max(1, chunkRowsAuto/2);
            const int ov         = std::max(0, overlapRowsAuto/2);
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
                else VISION_LOG_INFO("ExposureMerge2[청크]: 경고 — 겹침 일치 표본 0개 → 오프셋 보정 건너뜀(offset=0). matchTol/노출 정렬 확인.");
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
            VISION_LOG_INFO("ExposureMerge2[청크]: {}개 청크(코어 {}행+겹침 {}행), 제거 {} px", nChunks, chunkRowsAuto, overlapRowsAuto, removed);
        }

        // ⑤ 출력 HeightMap: 반해상도(n행, Y피치×2). halfRes=false면 각 행을 2배 복제해 원본 높이.
        auto makeOut = [&](std::vector<float> src) {   // by-value: 호출측에서 move로 넘겨 복사/할당 제거
            auto z = std::make_shared<HeightMap>();
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
        data->setHeightMap(zFinal);
        data->sourceId = input->sourceId;
        data->frames = input->frames;
        // A5-1: halfRes=true 시 새 프레임 정의 (yResMm×2 해상도 변경을 프레임 트리에 기록)
        if (m_halfRes && !m_nodeId.empty()) {
            Frame f; f.id = "hm:" + m_nodeId; f.toParent = Transform2D::identity();
            data->definedFrames.push_back(f);
            if (data->frames) data->frames->define(f);
            zFinal->frameId = f.id;
        }
        // 중간 단계는 결과창 드롭다운(디스플레이) 전용 — 전체모드 && !noPreview 일 때만(청크 모드는 최종만).
        if (!chunkMode && !m_noPreview && !mergedFull.empty()) {
            auto zMerged=makeOut(std::move(mergedFull)), zLow=makeOut(std::move(lowCFull)), zHigh=makeOut(std::move(highFull));
            data->stages = std::make_shared<std::vector<std::pair<std::string, HeightMapPtr>>>();
            data->stages->push_back({ "1. 머지(리플렉션 제거)", zFinal });
            data->stages->push_back({ "2. 기본 머지",           zMerged });
            data->stages->push_back({ "3. 저노출(오프셋 보정)", zLow });
            data->stages->push_back({ "4. 장노출",             zHigh });
        }
        if (!chunkMode)
            VISION_LOG_INFO("ExposureMerge2: offset={:.1f}cnt, fill 리플렉션 제거 {} px (matchTol={}, tolX={}, tolY={})",
                            offset, removed, m_matchTol, m_tolX, m_tolY);
        return { ToolStatus::Ok, "", data };
    }
};

// ── TripleExposureMerge (3노출 머지): 인터리브 저/중/장(행 r%3=0/1/2) → 공유 결정 코어를
//    캐스케이드로 2번 적용. 우선순위 저>중>장, 각 단계 오프셋 보정 + 연속성 BFS 리플렉션 제거.
// A5-1: halfRes=true → 새 프레임 정의(nodeId가 있을 때).
// A5-2: chunkMode/chunkRows/overlapRows 레시피에서 제거 → 엔진 자동 판단(n > 4096 시 청크).
class TripleExposureMergeTool : public IAlgorithmTool {
    float m_matchTol;
    float m_reflTol;
    float m_tolX, m_tolY;
    int   m_gapK;
    bool  m_halfRes;
    bool  m_removeReflection;
    bool  m_noPreview;
    int   m_bands;
    std::string m_nodeId;  // A5-1: 새 프레임 정의용
public:
    TripleExposureMergeTool(float matchTol, float reflTol, float tolX, float tolY,
                            int gapK, bool halfRes, bool removeReflection, bool noPreview, int bands,
                            std::string nodeId)
        : m_matchTol(matchTol), m_reflTol(reflTol), m_tolX(tolX), m_tolY(tolY),
          m_gapK(gapK), m_halfRes(halfRes), m_removeReflection(removeReflection),
          m_noPreview(noPreview), m_bands(bands), m_nodeId(std::move(nodeId)) {}
    std::string name() const override { return "ExposureMerge3"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->inHeightMap(0))
            return { ToolStatus::Fail, "3노출 머지: HeightMap 입력이 필요합니다" };
        const auto& zm = *input->inHeightMap(0);
        const int w = zm.width, h = zm.height;
        if (h < 3) return { ToolStatus::Fail, "3노출 머지: 이미지 높이가 너무 작습니다(≥3행)" };
        const int n = h / 3;                        // 3중 프로파일당 출력행 수
        const size_t BN = (size_t)n * w;
        const float NaN = std::numeric_limits<float>::quiet_NaN();
        auto at = [&](int r, int c){ return zm.data[(size_t)r*w + c]; };

        // 세부 계측(동작 불변): 각 구간 소요시간을 로그로.
        using clk = std::chrono::steady_clock;
        auto t0 = clk::now();
        auto lap = [&](const char* tag) {
            auto ms = std::chrono::duration<double,std::milli>(clk::now()-t0).count();
            VISION_LOG_INFO("[ExposureMerge3] {}: {:.1f} ms", tag, ms);
            t0 = clk::now();
        };

        // ① 저/중/장 분리 (행 r → 전역행 3r, 3r+1, 3r+2)
        std::vector<float> lo(BN), mid(BN), hi(BN);
        cv::parallel_for_(cv::Range(0, n), [&](const cv::Range& rg) {
            for (int r = rg.start; r < rg.end; ++r)
                for (int c = 0; c < w; ++c) {
                    size_t i = (size_t)r*w + c;
                    lo[i] = at(3*r, c); mid[i] = at(3*r+1, c); hi[i] = at(3*r+2, c);
                }
        });
        lap("① 저/중/장 분리");

        // 전역 오프셋(median(A−B), stride-4 서브샘플) — 결정 코어 내부와 동일 산식.
        //  밴드 병렬에서도 모든 밴드가 이 값을 forcedOffset으로 공유해야 전체-1회 연산과 비트 동일.
        auto globalOffset = [&](const std::vector<float>& A, const std::vector<float>& B) {
            std::vector<float> d; d.reserve(BN/4 + 1);
            for (size_t i = 0; i < BN; i += 4)
                if (!std::isnan(A[i]) && !std::isnan(B[i]) && std::fabs(A[i]-B[i]) <= m_matchTol) d.push_back(A[i]-B[i]);
            float o = 0.f;
            if (!d.empty()) { size_t m = d.size()/2; std::nth_element(d.begin(), d.begin()+m, d.end()); o = d[m]; }
            else VISION_LOG_INFO("ExposureMerge3: 경고 — 겹침 일치 표본 0개 → 오프셋 보정 건너뜀(offset=0). matchTol/노출 정렬 확인.");
            return o;
        };

        // 캐스케이드 한 단계를 행-밴드로 병렬 실행. offset은 전역에서 강제(forced).
        //  각 밴드는 위·아래 겹침(OV행)까지 확장해 결정하고 코어 행만 out에 기록 →
        //  겹침이 리플렉션 최장 streak를 덮으면 전체-1회 결과와 동일(2노출 청크검증 근거).
        const int OV = 160;   // 출력행 단위 겹침(2노출 검증 입력 320행 = 출력 160행)
        const int nBands = (m_bands > 0) ? m_bands
                                         : std::max(1, std::min(std::max(1, cv::getNumThreads()), n));
        // 밴드 슬롯별 작업 버퍼 — 결정 호출마다 새 할당하던 것을 재사용(힙 경합 제거). 단계 간에도 재사용.
        std::vector<ExposureMergeScratch> scratch(nBands);
        std::vector<std::vector<uint8_t>>  srcBufs(nBands);
        auto runStage = [&](const std::vector<float>& low, const std::vector<float>& high, float offset, int bandsReq) {
            std::vector<float> out(BN);
            const int bands = std::max(1, std::min(bandsReq, n));
            cv::parallel_for_(cv::Range(0, bands), [&](const cv::Range& rg) {
                for (int b = rg.start; b < rg.end; ++b) {
                    const int p0 = (int)((long long)n * b / bands);
                    const int p1 = (int)((long long)n * (b+1) / bands);
                    if (p0 >= p1) continue;
                    const int e0 = std::max(0, p0 - OV), e1 = std::min(n, p1 + OV);
                    const int bn = e1 - e0;
                    // 확장 밴드 [e0,e1)를 복사 없이 전체 배열의 포인터로 직접 처리. source/스크래치는 밴드 슬롯 재사용.
                    std::vector<uint8_t>& src = srcBufs[b];
                    exposureMergeDecision(low.data()+(size_t)e0*w, high.data()+(size_t)e0*w, w, bn,
                                          m_matchTol, m_tolX, m_tolY, m_gapK, offset, src, &scratch[b],
                                          m_removeReflection, m_reflTol);
                    // source → Z, 코어 행 [p0,p1)만 out에 기록(겹침 여백 버림). 승자 값은 전체 배열에서 직접.
                    for (int r = p0; r < p1; ++r) {
                        const size_t so = (size_t)(r - e0) * w, dst = (size_t)r * w;
                        for (int c = 0; c < w; ++c) {
                            uint8_t s = src[so + c];
                            out[dst + c] = (s == 1) ? low[dst + c] - offset : (s == 2 ? high[dst + c] : NaN);
                        }
                    }
                }
            });
            return out;
        };

        // ② 오프셋1(저↔중): 원시 저/중 stride-4 median. 양 모드 동일.
        const float ofs1 = globalOffset(lo, mid);

        // A5-2: chunkMode 자동 판단. overlapRows EM3은 180.
        const bool chunkMode = (n > 4096);
        const int chunkRowsAuto = 1000, overlapRowsAuto = 180;
        std::vector<float> mergedA;   // 비청크 프리뷰 단계용(청크 모드에선 미materialize)
        std::vector<float> finalZ;
        float ofs2 = 0.f;
        if (!chunkMode) {
            // 전체 밴드병렬: 1단계 → mergedA(full) → 오프셋2 → 2단계 (기존 경로, 불변)
            mergedA = runStage(lo, mid, ofs1, nBands);
            lap("② 1단계(저+중) 밴드병렬");
            ofs2 = globalOffset(mergedA, hi);
            finalZ = runStage(mergedA, hi, ofs2, nBands);
            lap("③ 2단계(저·중+장) 밴드병렬");
        } else {
            // 청크 모드: mergedA 전체 미보관. 오프셋2를 pre-filter stride-4 근사로 up-front 산출.
            //  기여 픽셀(hi와 일치하는 곳)은 리플렉션 필터 전/후 동일 → 전체모드 ofs2와 사실상 일치.
            { std::vector<float> d; d.reserve(BN/4+1);
              for (size_t i=0;i<BN;i+=4){ float mA = !std::isnan(lo[i]) ? lo[i]-ofs1 : (!std::isnan(mid[i]) ? mid[i] : NaN);
                if (!std::isnan(mA) && !std::isnan(hi[i]) && std::fabs(mA-hi[i])<=m_matchTol) d.push_back(mA-hi[i]); }
              if (!d.empty()){ size_t m=d.size()/2; std::nth_element(d.begin(),d.begin()+m,d.end()); ofs2=d[m]; }
              else VISION_LOG_INFO("ExposureMerge3[청크]: 경고 — ofs2 겹침 표본 0개 → offset=0.");
            }
            // 청크 [p0,p1)를 위·아래 ov행 확장해 두 단계를 블록 내에서 수행, 코어 행만 기록.
            //  ov는 두 단계 BFS 전파를 덮어야 함(실측: 40 출력행이면 전체모드와 0px, 기본 60출력행 마진).
            auto computeTripleFiltered = [&](int e0, int e1) {
                const int bn = e1 - e0;
                std::vector<uint8_t> s1;
                exposureMergeDecision(lo.data()+(size_t)e0*w, mid.data()+(size_t)e0*w, w, bn,
                                      m_matchTol, m_tolX, m_tolY, m_gapK, ofs1, s1, nullptr, m_removeReflection, m_reflTol);
                std::vector<float> mA((size_t)bn*w);
                for (size_t i=0;i<(size_t)bn*w;++i){ uint8_t s=s1[i]; mA[i]= s==1? lo[(size_t)e0*w+i]-ofs1 : (s==2? mid[(size_t)e0*w+i] : NaN); }
                std::vector<uint8_t> s2;
                exposureMergeDecision(mA.data(), hi.data()+(size_t)e0*w, w, bn,
                                      m_matchTol, m_tolX, m_tolY, m_gapK, ofs2, s2, nullptr, m_removeReflection, m_reflTol);
                std::vector<float> fB((size_t)bn*w);
                for (size_t i=0;i<(size_t)bn*w;++i){ uint8_t s=s2[i]; fB[i]= s==1? mA[i]-ofs2 : (s==2? hi[(size_t)e0*w+i] : NaN); }
                return fB;
            };
            finalZ.assign(BN, NaN);
            const int chunkOut = std::max(1, chunkRowsAuto/3);
            const int ov       = std::max(0, overlapRowsAuto/3);
            int nChunks = 0;
            for (int p0=0; p0<n; p0+=chunkOut) {
                const int p1 = std::min(n, p0+chunkOut);
                const int e0 = std::max(0, p0-ov), e1 = std::min(n, p1+ov);
                auto fB = computeTripleFiltered(e0, e1);
                for (int r=p0;r<p1;++r) std::copy(&fB[(size_t)(r-e0)*w], &fB[(size_t)(r-e0)*w+w], &finalZ[(size_t)r*w]);
                ++nChunks;
            }
            VISION_LOG_INFO("ExposureMerge3[청크]: {}개 청크(코어 {}입력행+겹침 {}입력행), ofs1={:.1f} ofs2={:.1f}",
                            nChunks, chunkRowsAuto, overlapRowsAuto, ofs1, ofs2);
            lap("②③ 청크 캐스케이드");
        }

        // ④ 출력 HeightMap: halfRes면 n행·Y피치×3. 끄면 각 행을 3배 복제해 원본 높이(3n행).
        auto makeOut = [&](std::vector<float> src) {   // by-value: 최종은 move로 넘겨 복사 제거
            auto z = std::make_shared<HeightMap>();
            z->width=w; z->xResMm=zm.xResMm; z->zResMm=zm.zResMm; z->zZeroCount=zm.zZeroCount;
            z->originCol=zm.originCol; z->originRow=zm.originRow;
            if (m_halfRes) {
                z->height=n; z->yResMm=zm.yResMm*3.f; z->data = std::move(src);
            } else {
                z->height=3*n; z->yResMm=zm.yResMm;
                z->data.resize((size_t)3*n*w);
                cv::parallel_for_(cv::Range(0, n), [&](const cv::Range& rg) {
                    for (int r=rg.start;r<rg.end;++r)
                        for (int s=0;s<3;++s)
                            std::copy(&src[(size_t)r*w], &src[(size_t)r*w+w], &z->data[(size_t)(3*r+s)*w]);
                });
            }
            return z;
        };
        auto zFinal = makeOut(std::move(finalZ));

        auto data = std::make_shared<VisionData>();
        data->setHeightMap(zFinal);
        data->sourceId = input->sourceId;
        data->frames = input->frames;
        // A5-1: halfRes=true 시 새 프레임 정의
        if (m_halfRes && !m_nodeId.empty()) {
            Frame f; f.id = "hm:" + m_nodeId; f.toParent = Transform2D::identity();
            data->definedFrames.push_back(f);
            if (data->frames) data->frames->define(f);
            zFinal->frameId = f.id;
        }
        // 중간 단계는 결과창 드롭다운(디스플레이) 전용 — !noPreview && 비청크 일 때만(청크는 mergedA 미보관).
        if (!m_noPreview && !chunkMode) {
            // 스테이지용 버퍼는 이후 미사용 → move로 넘겨 복사 제거(인터랙티브 미리보기 비용 절감).
            auto zMerged = makeOut(std::move(mergedA)), zLo = makeOut(std::move(lo)),
                 zMid = makeOut(std::move(mid)), zHi = makeOut(std::move(hi));
            data->stages = std::make_shared<std::vector<std::pair<std::string, HeightMapPtr>>>();
            data->stages->push_back({ "1. 머지(리플렉션 제거)", zFinal });
            data->stages->push_back({ "2. 저·중 머지",          zMerged });
            data->stages->push_back({ "3. 저노출",             zLo });
            data->stages->push_back({ "4. 중간노출",           zMid });
            data->stages->push_back({ "5. 장노출",             zHi });
        }
        lap("④ 출력 HeightMap + 스테이지");
        VISION_LOG_INFO("ExposureMerge3: {}x{} → {}행, offset1={:.1f} offset2={:.1f}cnt (matchTol={}, tolX={}, tolY={})",
                        w, h, n, ofs1, ofs2, m_matchTol, m_tolX, m_tolY);
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
                     : (!sourceId.empty() ? fs::u8path(sourceId).stem().u8string() : std::string("output"));
    std::string ext = format;
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    if (ext.empty()) ext = "png";
    return (fs::u8path(folder) / (msStamp() + "_" + stem + "." + ext)).u8string();
}

// ── HeightMapSaver: HeightMap을 파일로 저장 + 사이드카 JSON 메타 ──────────
//   HeightMap 입력만 허용. 16-bit(png/tif) 또는 8-bit(그 외, min-max 정규화).
//   사이드카: <savePath>.meta.json — xResMm/yResMm/zResMm/zZeroCount/originCol/Row 등.
//   HeightMapLoader가 사이드카를 인식해 왕복 불변량이 성립한다.
class HeightMapSaverTool : public IAlgorithmTool {
    std::string m_folder, m_filename, m_format;
    bool m_saveMeta;
public:
    HeightMapSaverTool(std::string folder, std::string filename, std::string format, bool saveMeta = true)
        : m_folder(std::move(folder)), m_filename(std::move(filename)), m_format(std::move(format)), m_saveMeta(saveMeta) {}
    std::string name() const override { return "HeightMapSaver"; }

    ToolResult execute(VisionDataPtr input) override {
        if (m_folder.empty()) return { ToolStatus::Fail, "HeightMapSaver: 저장 폴더가 설정되지 않았습니다" };
        if (!input)           return { ToolStatus::Fail, "HeightMapSaver: 입력이 없습니다" };
        if (!input->inHeightMap(0)) return { ToolStatus::Fail, "HeightMapSaver: HeightMap 입력이 없습니다" };

        const HeightMap& zm = *input->inHeightMap(0);
        const std::string savePath = buildSavePath(m_folder, m_filename, m_format, input->sourceId);
        std::string ext = m_format;
        for (auto& ch : ext) ch = (char)std::tolower(ch);
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        const bool ext16 = (ext == "png" || ext == "tif" || ext == "tiff");
        const size_t N = (size_t)zm.width * zm.height;

        try {
            if (ext16) {
                cv::Mat m16(zm.height, zm.width, CV_16U);
                uint16_t* d = (uint16_t*)m16.data;
                for (size_t i = 0; i < N; ++i) {
                    float v = zm.data[i];
                    d[i] = std::isnan(v) ? 0 : (uint16_t)std::clamp(v, 0.f, 65535.f);  // 무효=0
                }
                if (!cv::imwrite(savePath, m16)) return { ToolStatus::Fail, "HeightMapSaver: 저장 실패: " + savePath };
            } else {
                float lo = 1e30f, hi = -1e30f;
                for (size_t i = 0; i < N; ++i) { float v = zm.data[i]; if (!std::isnan(v)) { lo = std::min(lo,v); hi = std::max(hi,v); } }
                float span = std::max(1e-6f, hi - lo);
                cv::Mat m8(zm.height, zm.width, CV_8U);
                for (size_t i = 0; i < N; ++i) {
                    float v = zm.data[i];
                    m8.data[i] = std::isnan(v) ? 0 : (uchar)std::clamp((v-lo)/span*255.f, 0.f, 255.f);
                }
                if (!cv::imwrite(savePath, m8)) return { ToolStatus::Fail, "HeightMapSaver: 저장 실패: " + savePath };
            }
        } catch (const std::exception& e) {
            return { ToolStatus::Fail, std::string("HeightMapSaver: ") + e.what() };
        }

        // 사이드카 메타 저장 — HeightMapLoader가 읽어 왕복 불변량 달성(선택)
        if (m_saveMeta) writeSidecar(savePath, zm);

        VISION_LOG_INFO("HeightMapSaver: 저장됨 → {}", savePath);
        return { ToolStatus::Ok, "", input };   // 통과 (탭 노드)
    }
};

// ── CloudSaver: PointCloud3D → 파일 저장. .xyz/.asc(텍스트) / .bin / .ply(binary_little_endian, 기본). ────
//   파일명 앞에 타임스탬프를 붙여 폴더검사 시 덮어쓰기 방지(ImageSaver와 동일 규약).
class CloudSaverTool : public IAlgorithmTool {
    std::string m_folder, m_filename, m_format;
public:
    CloudSaverTool(std::string folder, std::string filename, std::string format)
        : m_folder(std::move(folder)), m_filename(std::move(filename)), m_format(std::move(format)) {}
    std::string name() const override { return "CloudSaver"; }

    ToolResult execute(VisionDataPtr input) override {
        if (m_folder.empty())              return { ToolStatus::Fail, "CloudSaver: 저장 폴더가 설정되지 않았습니다" };
        if (!input || !input->inCloud(0)) return { ToolStatus::Fail, "CloudSaver: PointCloud 입력이 없습니다. HeightMap→Cloud를 먼저 연결하세요." };

        const std::string savePath = buildSavePath(m_folder, m_filename, m_format, input->sourceId);
        std::string ext = m_format;
        for (auto& ch : ext) ch = (char)std::tolower(ch);
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

        const auto& pts = input->inCloud(0)->points;
        std::ofstream ofs(std::filesystem::u8path(savePath), std::ios::binary);
        if (!ofs) return { ToolStatus::Fail, "CloudSaver: 파일을 열 수 없습니다: " + savePath };

        if (ext == "bin") {                       // 생 바이너리: float32 x,y,z 연속(헤더 없음). 점수=파일크기/12.
            // Point3f = {float x,y,z} 연속(12B)이라 배열 통째로 1회 write → 텍스트 변환 없이 최고속.
            ofs.write(reinterpret_cast<const char*>(pts.data()),
                      (std::streamsize)pts.size() * sizeof(Point3f));
        } else if (ext == "xyz" || ext == "asc") { // 단순 텍스트: "x y z" 한 줄씩 (.asc는 .xyz와 동일 포맷)
            ofs << std::fixed << std::setprecision(4);
            for (const auto& p : pts) ofs << p.x << " " << p.y << " " << p.z << "\n";
        } else {                                  // PLY binary_little_endian (기본, CloudCompare/MeshLab 호환·고속)
            ofs << "ply\nformat binary_little_endian 1.0\n";
            ofs << "element vertex " << pts.size() << "\n";
            ofs << "property float x\nproperty float y\nproperty float z\n";
            ofs << "end_header\n";
            // 헤더 뒤 raw float32 x,y,z 연속(Point3f=12B 그대로). x64는 리틀엔디안 → 1회 write.
            ofs.write(reinterpret_cast<const char*>(pts.data()),
                      (std::streamsize)pts.size() * sizeof(Point3f));
        }
        if (!ofs.good()) return { ToolStatus::Fail, "CloudSaver: 저장 중 오류: " + savePath };
        VISION_LOG_INFO("CloudSaver: {} points → {}", pts.size(), savePath);
        return { ToolStatus::Ok, "", input };     // 통과 (싱크)
    }
};

// ── Collect: 여러 포트 입력의 measurements/decisions/overlays를 하나로 수집. ─────
//   inputs[0..N-1]에 들어온 모든 측정값·판정을 합쳐 단일 VisionData로 출력.
//   첫 번째 비-null HeightMap/Cloud를 주 출력으로 사용.
class CollectTool : public IAlgorithmTool {
public:
    std::string name() const override { return "Collect"; }
    ToolResult execute(VisionDataPtr input) override {
        if (!input) return { ToolStatus::Fail, "Collect: 입력이 없습니다" };
        auto out = std::make_shared<VisionData>();
        out->sourceId = input->sourceId;
        out->frames   = input->frames;
        for (const auto& inp : input->inputs) {
            if (!inp) continue;
            if (!out->heightmap0() && inp->heightmap0())
                out->setHeightMap(inp->heightmap0());
            if (!out->cloud0() && inp->cloud0())
                out->setCloud(inp->cloud0());
            for (const auto& m  : inp->measurements) out->measurements.push_back(m);
            for (const auto& d  : inp->decisions)    out->decisions.push_back(d);
            for (const auto& ov : inp->overlays)      out->overlays.push_back(ov);
        }
        return { ToolStatus::Ok, "", out };
    }
};

// GapFillTool → VisionTools/GapFillTool.{h,cpp} 로 이동 (A2)
#if 0
class GapFillTool : public IAlgorithmTool {
public:
    enum class Method { Neighbor, Median, Laplace, Nearest, Idw, Linear, Anisotropic };
private:
    Method m_method;
    int    m_maxGap, m_minValid, m_idwRadius;
    float  m_idwPower, m_edgeSigma;
    bool   m_noPreview;
public:
    // A5-6: outputStage 제거 → 주 출력은 항상 stage 0(메운 결과), UI가 stages에서 선택.
    GapFillTool(Method m, int maxGap, int minValid, int idwRadius, float idwPower, float edgeSigma, bool noPreview)
        : m_method(m), m_maxGap(std::max(1,maxGap)), m_minValid(std::max(1,minValid)),
          m_idwRadius(std::max(1,idwRadius)), m_idwPower(idwPower),
          m_edgeSigma(edgeSigma), m_noPreview(noPreview) {}
    std::string name() const override { return "GapFill"; }

    ToolResult execute(VisionDataPtr input) override {
        if (!input || !input->inHeightMap(0)) return { ToolStatus::Fail, "GapFill: HeightMap 입력이 필요합니다" };
        const HeightMap& zm = *input->inHeightMap(0);
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

        // 출력 HeightMap (메타 유지). 주 출력은 항상 메운 결과(stage 0). UI가 stages에서 선택.
        auto mk = [&](const std::vector<float>& d){ auto z=std::make_shared<HeightMap>(zm); z->data=d; return z; };
        auto zFilled = mk(out);

        auto vd = std::make_shared<VisionData>();
        vd->setHeightMap(zFilled);
        vd->sourceId = input->sourceId;
        vd->frames = input->frames;
        if (!m_noPreview) {
            std::vector<float> maskData(N, NaN);
            for (size_t i=0;i<N;++i) maskData[i] = (fillable[i] && !std::isnan(out[i])) ? 1.f
                                               : (std::isnan(src[i]) ? NaN : 0.f);
            auto zOrig = mk(src), zMask = mk(maskData);
            vd->stages = std::make_shared<std::vector<std::pair<std::string, HeightMapPtr>>>();
            vd->stages->push_back({ "1. 메운 결과", zFilled });
            vd->stages->push_back({ "2. 원본",      zOrig });
            vd->stages->push_back({ "3. 메운 영역", zMask });
        }
        VISION_LOG_INFO("GapFill: 채움 {} / 대상 {} px (maxGap={})", filled, target, m_maxGap);
        return { ToolStatus::Ok, "", vd };
    }
};
#endif

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
    if (type == "HeightMapLoader") {
        return std::make_shared<HeightMapLoaderTool>(
            p.value("path",    ""),
            p.value("folder",  ""),
            p.value("xResMm",  1.0f),
            p.value("yResMm",  1.0f),
            p.value("zResMm",  0.001f));
    }
    if (type == "RowStretch") {
        return std::make_shared<RowStretchTool>(p.value("scale", 2));
    }
    if (type == "ExposureMerge") {
        return std::make_shared<ExposureMergeTool>(
            p.value("splitCount", 2),
            noPreview);
    }
    if (type == "ExposureMerge2") {
        return std::make_shared<DualExposureMergeTool>(
            p.value("matchTol", 20.0f),
            p.value("reflTol",  -1.0f),
            p.value("tolX",     10.0f),
            p.value("tolY",     100.0f),
            p.value("gapK",     2),
            p.value("halfRes",  true),
            noPreview,
            p.value("_nodeId",  std::string()));
    }
    if (type == "ExposureMerge3") {
        return std::make_shared<TripleExposureMergeTool>(
            p.value("matchTol", 20.0f),
            p.value("reflTol",  30.0f),
            p.value("tolX",     10.0f),
            p.value("tolY",     100.0f),
            p.value("gapK",     2),
            p.value("halfRes",  true),
            p.value("removeReflection", true),
            noPreview,
            p.value("mergeBands", 0),
            p.value("_nodeId",  std::string()));
    }
    if (type == "HeightMapSaver") {
        // folder(필수)+filename(선택)+format. 구버전 호환: path만 있으면 분해.
        std::string folder = p.value("folder", ""), filename = p.value("filename", ""), format = p.value("format", "png");
        if (folder.empty()) {
            std::string path = p.value("path", "");
            if (!path.empty()) {
                std::filesystem::path pp = std::filesystem::u8path(path);
                folder = pp.parent_path().u8string(); filename = pp.stem().u8string();
                std::string e = pp.extension().string(); if (!e.empty() && e[0]=='.') e = e.substr(1);
                if (!e.empty()) format = e;
            }
        }
        bool saveMeta = p.value("saveMeta", true);
        return std::make_shared<HeightMapSaverTool>(folder, filename, format, saveMeta);
    }
    if (type == "HeightMapToCloud") {
        return std::make_shared<HeightMapToCloudTool>(p.value("step", 1));
    }
    if (type == "CloudLoader") {
        return std::make_shared<CloudLoaderTool>(p.value("path", std::string()), p.value("swapXY", false));
    }
    if (type == "CloudMerge") {
        CloudMergeParams mp;
        mp.matrixPath = p.value("matrixPath", std::string());
        return std::make_shared<CloudMergeTool>(mp);
    }
    if (type == "CloudSorFilter") {
        CloudSorFilterParams sp;
        sp.cellSizeMm = p.value("cellSizeMm", 0.02);
        sp.kNeighbors = p.value("kNeighbors", 8);
        sp.stdRatio   = p.value("stdRatio",   2.0);
        return std::make_shared<CloudSorFilterTool>(sp);
    }
    if (type == "CloudToZmap") {
        CloudToZmapParams zp;
        zp.lateralResMm   = p.value("lateralResMm",   0.0063);
        zp.transportResMm = p.value("transportResMm", 0.008);
        zp.verticalResMm  = p.value("verticalResMm",  0.00105);
        zp.zZeroCount     = p.value("zZeroCount",     32768.0);
        zp.agg            = p.value("agg",            std::string("median"));
        return std::make_shared<CloudToZmapTool>(zp);
    }
    if (type == "CloudToProfiles") {
        CloudToProfilesTool::Params cp;
        cp.scanAxis   = (p.value("scanAxis", std::string("x")) == "y")
                        ? CloudToProfilesTool::Axis::Y : CloudToProfilesTool::Axis::X;
        cp.scanStepMm = p.value("scanStepMm", 0.1);
        cp.latStepMm  = p.value("latStepMm", 0.1);
        cp.minPoints  = p.value("minPoints", 1);
        std::string rd = p.value("reduce", std::string("none"));
        if      (rd == "max")  cp.reduce = CloudToProfilesTool::Reduce::Max;
        else if (rd == "min")  cp.reduce = CloudToProfilesTool::Reduce::Min;
        else if (rd == "mean") cp.reduce = CloudToProfilesTool::Reduce::Mean;
        else                   cp.reduce = CloudToProfilesTool::Reduce::None;
        return std::make_shared<CloudToProfilesTool>(cp);
    }
    if (type == "ProfileToCloud") {
        ProfileToCloudTool::Params params;
        params.transportResMm = p.value("transportResMm", 0.0);
        return std::make_shared<ProfileToCloudTool>(params);
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
            noPreview);
    }
    if (type == "CloudSaver") {
        std::string folder = p.value("folder", ""), filename = p.value("filename", ""), format = p.value("format", "ply");
        if (folder.empty()) {
            std::string path = p.value("path", "");
            if (!path.empty()) {
                std::filesystem::path pp = std::filesystem::u8path(path);
                folder = pp.parent_path().u8string(); filename = pp.stem().u8string();
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
        return std::make_shared<NoiseFilter>(params);
    }
    if (type == "PlaneFit") {
        PlaneFitParams params;

        std::string algo = p.value("algorithm", "LeastSquares");
        if      (algo == "RANSAC") params.algorithm = PlaneFitParams::Algorithm::RANSAC;
        else if (algo == "SVD")    params.algorithm = PlaneFitParams::Algorithm::SVD;
        else                       params.algorithm = PlaneFitParams::Algorithm::LeastSquares;

        params.ransacThresholdMm = p.value("ransacThreshold",  0.05f);
        params.ransacIterations  = p.value("ransacIterations",  200);
        params.maxCloudPoints    = p.value("maxCloudPoints",    200000);

        return std::make_shared<PlaneFitTool>(params);
    }
    if (type == "LineFit") {
        LineFitParams params;
        std::string feat = p.value("feature", "ridge");
        if      (feat == "valley") params.feature = LineFeature::Valley;
        else if (feat == "edge")   params.feature = LineFeature::Edge;
        else                       params.feature = LineFeature::Ridge;
        std::string sdir = p.value("scanDir", "lr");
        if      (sdir == "rl") params.scanDir = LineScanDir::Rl;
        else if (sdir == "tb") params.scanDir = LineScanDir::Tb;
        else if (sdir == "bt") params.scanDir = LineScanDir::Bt;
        else                   params.scanDir = LineScanDir::Lr;
        params.threshold  = p.value("threshold", 0.f);
        params.risingEdge = p.value("risingEdge", true);
        params.fitMethod  = (p.value("fitMethod", std::string("leastSquares")) == "ransac")
                            ? LineFitMethod::Ransac : LineFitMethod::LeastSquares;
        params.ransacTolMm = p.value("ransacTolMm", 0.5f);
        params.ransacIters = p.value("ransacIters", 100);
        return std::make_shared<LineFitTool>(params);
    }
    if (type == "LineCenter") {
        LineCenterParams params;
        std::string sdir = p.value("scanDir", "lr");
        if      (sdir == "rl") params.scanDir = ScanDir::Rl;
        else if (sdir == "tb") params.scanDir = ScanDir::Tb;
        else if (sdir == "bt") params.scanDir = ScanDir::Bt;
        else                   params.scanDir = ScanDir::Lr;
        std::string pol = p.value("polarity", "d2l");
        params.polarity   = (pol == "l2d") ? Polarity::LightToDark : Polarity::DarkToLight;
        params.threshold  = p.value("threshold", 1.f);
        return std::make_shared<LineCenterTool>(params);
    }
    if (type == "Align") {
        AlignParams ap;
        ap.useX = p.value("useX", true);
        ap.useY = p.value("useY", true);
        return std::make_shared<AlignTool>(ap);
    }
    if (type == "CsvWriter") {
        CsvWriterParams params;
        params.path = p.value("path", "");
        params.label = p.value("label", "");
        params.addTimestamp = p.value("addTimestamp", false);
        return std::make_shared<CsvWriterTool>(params);
    }

    if (type == "Threshold") {
        ThresholdParams params;
        params.channel      = p.value("channel", 0);
        params.thresholdMm  = p.value("thresholdMm", 0.f);
        params.thresholdRaw = p.value("thresholdRaw", 0.f);
        params.keepAbove    = p.value("keepAbove", true);
        params.mode = (p.value("thresholdMode", std::string("mm")) == "raw")
                      ? ThresholdParams::Mode::Raw : ThresholdParams::Mode::Mm;
        return std::make_shared<ThresholdTool>(params);
    }
    if (type == "ValidRegion") {
        ValidRegionParams params;
        params.channel = p.value("channel", 0);
        params.invert  = p.value("invert", false);
        return std::make_shared<ValidRegionTool>(params);
    }
    if (type == "Level") {
        LevelParams params;
        params.mode        = p.value("mode",        std::string("distance"));
        params.keepInvalid = p.value("keepInvalid", true);
        params.offsetMm    = p.value("offsetMm",    0.0);
        return std::make_shared<LevelTool>(params);
    }
    if (type == "SurfaceCrop") {
        SurfaceCropParams params;
        params.mode       = p.value("mode",       std::string("rect"));
        params.outsideNaN = p.value("outsideNaN", true);
        params.nodeId     = p.value("_nodeId",    std::string());
        if (p.contains("rect") && p["rect"].is_object()) {
            const auto& r = p["rect"];
            params.rect_x = r.value("x", 0);
            params.rect_y = r.value("y", 0);
            params.rect_w = r.value("w", 0);
            params.rect_h = r.value("h", 0);
        }
        return std::make_shared<SurfaceCropTool>(params);
    }
    if (type == "SurfaceResample") {
        SurfaceResampleParams params;
        params.mode         = p.value("mode",         std::string("factor"));
        params.factor       = p.value("factor",       2);
        params.targetXResMm = p.value("targetXResMm", 0.f);
        params.targetYResMm = p.value("targetYResMm", 0.f);
        params.method       = p.value("method",       std::string("decimate"));
        params.nodeId       = p.value("_nodeId",      std::string());
        return std::make_shared<SurfaceResampleTool>(params);
    }
    if (type == "CreateROI") {
        CreateRoiParams params;
        if (p.contains("rois") && p["rois"].is_array()) {
            for (const auto& r : p["rois"]) {
                CreateRoiParams::ROI roi;
                roi.id   = r.value("id",   std::string());
                roi.xPct = r.value("xPct", 0.f);
                roi.yPct = r.value("yPct", 0.f);
                roi.wPct = r.value("wPct", 1.f);
                roi.hPct = r.value("hPct", 1.f);
                roi.angleDeg = r.value("angleDeg", 0.f);
                const std::string shape = r.value("shape", std::string("rect"));
                roi.isCircle = (shape == "circle");
                if (shape == "polygon" && r.contains("points") && r["points"].is_array())
                    for (const auto& pt : r["points"])
                        roi.poly.push_back({ pt.value("x", 0.f), pt.value("y", 0.f) });
                params.rois.push_back(roi);
            }
        }
        // 라인 밴드 모드 파라미터
        params.bandWidthMm  = p.value("bandWidthMm",  5.f);
        params.bandOffsetMm = p.value("bandOffsetMm", 3.f);
        params.bandLengthMm = p.value("bandLengthMm", 10.f);
        const std::string side = p.value("bandSide", std::string("both"));
        if      (side == "left")  params.bandSide = CreateRoiParams::BandSide::Left;
        else if (side == "right") params.bandSide = CreateRoiParams::BandSide::Right;
        else                      params.bandSide = CreateRoiParams::BandSide::Both;
        const std::string lmode = p.value("bandLenMode", std::string("line"));
        params.bandLenMode = (lmode == "fixed") ? CreateRoiParams::BandLen::Fixed
                                                : CreateRoiParams::BandLen::Line;
        return std::make_shared<CreateRoiTool>(params);
    }
    if (type == "ReduceDomain") {
        ReduceDomainParams params;
        params.invert = p.value("invert", false);
        return std::make_shared<ReduceDomainTool>(params);
    }
    if (type == "RegionMeasure") {
        RegionMeasureParams params;
        params.aggregation = p.value("aggregation", std::string("Mean"));
        params.highTailPct = p.value("highTailPct", 20.0);
        params.percentile  = p.value("percentile",  50.0);
        return std::make_shared<RegionMeasureTool>(params);
    }

    if (type == "Collect") {
        return std::make_shared<CollectTool>();
    }
    if (type == "SurfaceSubtract") {
        SurfaceSubtractParams params;
        params.absolute  = p.value("absolute",  false);
        params.nanPolicy = p.value("nanPolicy", std::string("propagate"));
        params.nodeId    = p.value("nodeId",    std::string(""));
        return std::make_shared<SurfaceSubtractTool>(params);
    }
    if (type == "ExtractProfile") {
        ExtractProfileParams params;
        params.mode    = p.value("mode",    std::string("axisX"));
        params.index   = p.value("index",   0);
        params.span    = p.value("span",    1);
        params.repeat  = p.value("repeat",  1);
        params.channel = p.value("channel", 0);
        params.p0x     = p.value("p0x",     0.0);
        params.p0y     = p.value("p0y",     0.0);
        params.p1x     = p.value("p1x",     0.0);
        params.p1y     = p.value("p1y",     0.0);
        params.unit    = p.value("unit",    std::string("mm"));
        params.count   = p.value("count",   0);
        params.interp  = p.value("interp",  std::string("bilinear"));
        params.nodeId  = p.value("nodeId",  std::string(""));
        return std::make_shared<ExtractProfileTool>(params);
    }
    if (type == "ProfileFeature") {
        ProfileFeatureParams params;
        params.kind         = p.value("kind",         std::string("maxZ"));
        params.searchFromMm = p.value("searchFromMm", 0.0);
        params.searchToMm   = p.value("searchToMm",   0.0);
        params.nth          = p.value("nth",           0);
        params.percentile   = p.value("percentile",    50.0);
        params.edgeDir      = p.value("edgeDir",       std::string("any"));
        params.edgeThresholdMm = p.value("edgeThresholdMm", 0.05);
        params.smoothWindow = p.value("smoothWindow",  3);
        return std::make_shared<ProfileFeatureTool>(params);
    }

    if (type == "Compare") {
        CompareParams params;
        params.target    = p.value("target",    std::string(""));
        params.mode      = p.value("mode",      std::string("tolerance"));
        params.nominal   = p.value("nominal",   0.0);
        params.tolerance = p.value("tolerance", 0.05);
        params.min       = p.value("min",       0.0);
        params.max       = p.value("max",       0.0);
        return std::make_shared<CompareTool>(params);
    }

    if (type == "CombineDecision") {
        CombineDecisionParams params;
        params.mode  = p.value("mode",  std::string("all"));
        params.count = p.value("count", 1);
        params.name  = p.value("name",  std::string("combined"));
        return std::make_shared<CombineDecisionTool>(params);
    }

    if (type == "NotchMeasure") {
        NotchMeasureParams np;
        np.transportResMm  = p.value("transportResMm",  0.003998);
        np.lateralPitchMm  = p.value("lateralPitchMm",  0.0063);
        np.landFitIters    = p.value("landFitIters",     4);
        np.landTolUm       = p.value("landTolUm",        30.0);
        np.notchTrigUm     = p.value("notchTrigUm",     -150.0);
        np.notchMaxGapUm   = p.value("notchMaxGapUm",   50.0);
        np.notchMinCols    = p.value("notchMinCols",     20);
        np.method          = p.value("method",           std::string("flat"));
        np.floorWinUm      = p.value("floorWinUm",       150.0);
        np.floorMinPts     = p.value("floorMinPts",      12);
        np.smoothCols      = p.value("smoothCols",       3);
        np.slopeDrop       = p.value("slopeDrop",        0.35);
        np.cornerSearchUm  = p.value("cornerSearchUm",   500.0);
        np.floorAgg        = p.value("floorAgg",         std::string("median"));
        np.floorTolUm      = p.value("floorTolUm",       40.0);
        np.dupMergeUm      = p.value("dupMergeUm",       10.0);
        np.avgProfiles     = p.value("avgProfiles",      1);
        np.avgMethod       = p.value("avgMethod",        std::string("mean"));
        return std::make_shared<NotchMeasureTool>(np);
    }
    if (type == "NotchMeasureV2") {
        NotchMeasureV2Params np;
        np.lateralResMm      = p.value("lateralResMm",      0.0063);
        np.transportResMm    = p.value("transportResMm",    0.008);
        np.avgProfiles       = p.value("avgProfiles",       1);
        np.avgMethod         = p.value("avgMethod",          std::string("mean"));
        np.landFitIters      = p.value("landFitIters",      4);
        np.notchTrigUm       = p.value("notchTrigUm",       -150.0);
        np.notchMaxGapUm     = p.value("notchMaxGapUm",     50.0);
        np.notchMinCols      = p.value("notchMinCols",      20);
        np.method             = p.value("method",             std::string("flat"));
        np.floorAgg           = p.value("floorAgg",           std::string("median"));
        np.floorWinUm        = p.value("floorWinUm",        150.0);
        np.floorMinPts       = p.value("floorMinPts",       12);
        np.floorSearchFrac   = p.value("floorSearchFrac",   1.0);
        np.smoothCols        = p.value("smoothCols",        3);
        np.slopeDrop         = p.value("slopeDrop",         0.35);
        np.cornerSearchUm    = p.value("cornerSearchUm",    500.0);
        np.landFlatFilter     = p.value("landFlatFilter",     false);
        np.landTolUm          = p.value("landTolUm",          30.0);
        np.landAgg            = p.value("landAgg",            std::string("median"));
        np.landMaxDistMm      = p.value("landMaxDistMm",      0.0);
        np.floorStabilizeHalf         = p.value("floorStabilizeHalf",         25);
        np.floorStabilizeCenterTolUm  = p.value("floorStabilizeCenterTolUm",  50.0);
        np.floorStabilizeZTolUm       = p.value("floorStabilizeZTolUm",       60.0);
        np.floorTolUm          = p.value("floorTolUm",          40.0);
        np.landMarginMm        = p.value("landMarginMm",        0.020);
        np.edgeSlopeTolUmPerMm = p.value("edgeSlopeTolUmPerMm", 30.0);
        np.edgeSlopeWindowPts  = p.value("edgeSlopeWindowPts",  5);
        return std::make_shared<NotchMeasureV2Tool>(np);
    }

    VISION_LOG_WARN("ToolFactory: unknown tool type '{}'", type);
    return nullptr;
}

} // namespace vision
