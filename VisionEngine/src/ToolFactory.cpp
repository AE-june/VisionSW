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
#include <thread>
#include <atomic>

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

// ── ExposureMerge: ZMap 입력(인터리브 홀짝 이중노출) → BFS 리플렉션 제거 + 머지 ─
//   ZMapLoader로 인터리브 Z PNG를 읽어 연결. I/L은 sourceId 경로에서 자동 파생.
//   짝/홀 행 = 저/장 노출. 겹침은 저노출 우선(리플 자동배제).
//   장노출에서 BFS 플러드필로 리플렉션 픽셀 제거 후 머지.
class ExposureMergeTool : public IAlgorithmTool {
public:
    struct ReflRoiPct { float xPct, yPct, wPct, hPct; };
private:
    bool  m_enableBfs, m_enableSor;
    int   m_sorK, m_outputStage;
    float m_sorRatio;
    std::vector<ReflRoiPct> m_reflRois;
    float m_seedTol, m_tolX, m_tolY;
    int   m_gapK;
    // true면 결과창에 쓸 단계별 미리보기가 필요 없다는 뜻 — 실제 출력에 쓰는
    // 단계 하나만 업샘플링해서 만들고 나머지 4개(각 수십~수백MB)는 만들지 않는다.
    // (폴더검사에서 이미지 한 장당 이 노드가 최대 5개의 원본 해상도 ZMap 사본을
    //  만들어내는 게 워커 여러 개 동시 실행 시 OOM의 주된 원인이었음)
    bool  m_skipStages;
public:
    ExposureMergeTool(bool enableBfs, bool enableSor, int sorK, float sorRatio,
                      std::vector<ReflRoiPct> reflRois, float seedTol, float tolX, float tolY, int gapK,
                      int outputStage, bool skipStages)
        : m_enableBfs(enableBfs), m_enableSor(enableSor),
          m_sorK(sorK), m_sorRatio(sorRatio),
          m_reflRois(std::move(reflRois)), m_seedTol(seedTol), m_tolX(tolX), m_tolY(tolY), m_gapK(gapK),
          m_outputStage(outputStage), m_skipStages(skipStages) {}
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
        const int si = std::clamp(m_outputStage, 0, 4);   // 실제 출력으로 쓸 단계 인덱스
        const float NaN = std::numeric_limits<float>::quiet_NaN();
        // 입력 ZMap 데이터(raw count float, 무효=NaN)를 그대로 사용
        auto zAt = [&](int row, int c) -> float { return zm.data[(size_t)row * w + c]; };

        // 짝수행 = 저노출로 가정 (I/L 파일 로드 없이 ZMap만 사용)
        const bool evenIsLow = true;
        auto loRow = [&](int r){ return evenIsLow ? 2*r   : 2*r+1; };
        auto hiRow = [&](int r){ return evenIsLow ? 2*r+1 : 2*r;   };

        // ① 홀짝 분리 → 저노출(low)/장노출(high) 배열 (n×w, 무효=NaN)
        std::vector<float> low((size_t)n*w), high((size_t)n*w), sub((size_t)n*w);
        for (int r = 0; r < n; ++r) for (int c = 0; c < w; ++c) {
            size_t i = (size_t)r*w + c;
            low[i]  = zAt(loRow(r), c);
            high[i] = zAt(hiRow(r), c);
        }
        lap("홀짝 분리");
        // ③ BFS 플러드필로 장노출 리플렉션 제거 (enableBfs AND ROI 설정 시에만 적용)
        std::vector<float> highClean = high;
        long bfsRemoved = 0;
        if (m_enableBfs && !m_reflRois.empty()) {
            // ROI를 n×w 픽셀 공간으로 변환 (percentage → pixel)
            struct PixRoi { int x0,y0,x1,y1; };
            std::vector<PixRoi> pixRois;
            for (auto& r : m_reflRois) {
                int rx = (int)(r.xPct * w), ry = (int)(r.yPct * n);
                int rw2 = (int)(r.wPct * w), rh2 = (int)(r.hPct * n);
                if (rw2 > 0 && rh2 > 0)
                    pixRois.push_back({ rx, ry, rx+rw2, ry+rh2 });
            }
            auto inRoi = [&](int r, int c) -> bool {
                for (auto& roi : pixRois) if (c>=roi.x0&&c<roi.x1&&r>=roi.y0&&r<roi.y1) return true;
                return false;
            };

            // 씨앗: 저/장 양쪽 유효이고 |차이| ≤ seedTol → 확실한 실제 표면
            std::vector<bool> visited((size_t)n*w, false);
            std::deque<int> q;
            for (int r = 0; r < n; ++r) for (int c = 0; c < w; ++c) {
                size_t i = (size_t)r*w+c;
                if (!std::isnan(low[i]) && !std::isnan(high[i]) &&
                    std::fabs(low[i]-high[i]) <= m_seedTol) {
                    visited[i] = true; q.push_back((int)i);
                }
            }

            // BFS: 축별 허용치 + gapK 갭 점프
            auto tryRay = [&](int r, int c, float z, int dr, int dc, float tolPerStep) {
                for (int k = 1; k <= m_gapK+1; ++k) {
                    int nr = r+dr*k, nc = c+dc*k;
                    if (nr<0||nr>=n||nc<0||nc>=w) break;
                    int ni = nr*w+nc;
                    if (visited[ni]) break;
                    if (std::isnan(high[ni])) continue;       // NaN 갭: 계속 진행
                    if (std::fabs(high[ni]-z) <= tolPerStep*k) { visited[ni]=true; q.push_back(ni); }
                    break;                                     // 유효 픽셀 발견: 수락 여부 무관 종료
                }
            };
            while (!q.empty()) {
                int idx = q.front(); q.pop_front();
                int r = idx/w, c = idx%w;
                float z = high[idx];
                tryRay(r,c,z, 0,-1,m_tolX); tryRay(r,c,z, 0,1,m_tolX);
                tryRay(r,c,z,-1, 0,m_tolY); tryRay(r,c,z, 1,0,m_tolY);
            }

            // ROI 내 미방문 유효 픽셀 = 리플렉션 → NaN
            for (int r = 0; r < n; ++r) for (int c = 0; c < w; ++c) {
                if (!inRoi(r,c)) continue;
                size_t i = (size_t)r*w+c;
                if (!visited[i] && !std::isnan(highClean[i])) { highClean[i]=NaN; ++bfsRemoved; }
            }
        }
        lap("BFS");
        // BFS까지 끝나면 원본 장노출(high)은 더 안 쓴다 — highClean이 그 정제본을 들고 있음.
        // (미리보기 생략 모드에서 이 단계가 출력으로 안 쓰이면 즉시 반납해 피크 메모리를 줄임)
        if (m_skipStages && si != 1) std::vector<float>().swap(high);

        // ④ 저노출 대입: 저노출 유효 픽셀은 전부 저노출값 사용, 없으면 리플렉션-제거된 장노출
        for (size_t i = 0; i < (size_t)n*w; ++i)
            sub[i] = !std::isnan(low[i]) ? low[i] : highClean[i];
        // low/highClean은 sub 계산에만 쓰였다 — 각자 출력 단계로 안 쓰이면 바로 반납.
        if (m_skipStages && si != 0) std::vector<float>().swap(low);
        if (m_skipStages && si != 2) std::vector<float>().swap(highClean);
        // ⑤ 위 결과(sub)에 NaN 인지 SOR: 창 내 유효이웃 평균±(stdRatio×표준편차) 밖이면 무효화
        std::vector<float> sor = sub;
        long sorRemoved = 0;
        if (m_enableSor && m_sorK >= 3) {
            const int half = m_sorK/2;
            for (int r = 0; r < n; ++r) for (int c = 0; c < w; ++c) {
                float z = sub[(size_t)r*w+c];
                if (std::isnan(z)) continue;
                int r0=std::max(0,r-half),r1=std::min(n-1,r+half),c0=std::max(0,c-half),c1=std::min(w-1,c+half);
                double s=0,s2=0; int cnt=0;
                for (int rr=r0;rr<=r1;++rr) for (int cc=c0;cc<=c1;++cc){ float v=sub[(size_t)rr*w+cc]; if(std::isnan(v))continue; s+=v; s2+=(double)v*v; ++cnt; }
                if (cnt<3) continue;
                double mean=s/cnt, var=std::max(0.0,s2/cnt-mean*mean), sd=std::sqrt(var);
                if (std::fabs(z-mean) > m_sorRatio*sd) { sor[(size_t)r*w+c]=NaN; ++sorRemoved; }
            }
        }

        lap("SOR");
        // sor는 sub의 사본이라 sub 자체는 이제 출력 단계로 안 쓰이면 반납 가능.
        if (m_skipStages && si != 3) std::vector<float>().swap(sub);

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

        ZMapPtr zLow, zHigh, zHighClean, zSub, zSor;
        if (m_skipStages) {
            // 결과창에 뿌릴 단계별 미리보기가 필요 없으면, 실제 출력으로 쓸
            // 단계 하나만 업샘플링 — 나머지 4개(단계당 최대 수백MB)는 만들지 않는다.
            switch (si) {
                case 0: zLow       = makeZLow(low);        break;
                case 1: zHigh      = makeZ(high);          break;
                case 2: zHighClean = makeZ(highClean);     break;
                case 3: zSub       = makeZ(sub);           break;
                case 4: zSor       = makeZ(sor);           break;
            }
            lap("makeZ(선택 1단계)");
        } else {
            zLow=makeZLow(low); lap("makeZLow(8x)");
            zHigh=makeZ(high); zHighClean=makeZ(highClean); zSub=makeZ(sub); zSor=makeZ(sor);
            lap("makeZ×4(2x)");
        }

        const ZMapPtr stageZmaps[] = { zLow, zHigh, zHighClean, zSub, zSor };

        auto data = std::make_shared<VisionData>();
        data->zmap = stageZmaps[si];
        data->sourceId = input->sourceId;
        if (!m_skipStages) {
            data->stages = std::make_shared<std::vector<std::pair<std::string, ZMapPtr>>>();
            data->stages->push_back({ "1. 저노출",           zLow });
            data->stages->push_back({ "2. 장노출",           zHigh });
            data->stages->push_back({ "3. 장노출 리플렉션 제거", zHighClean });
            data->stages->push_back({ "4. 저노출 대입 장노출", zSub });
            data->stages->push_back({ "5. SOR 적용",         zSor });
        }
        VISION_LOG_INFO("ExposureMerge: {}x{} (BFS제거 {} px, SOR제거 {} px)",
                        w, h, bfsRemoved, sorRemoved);
        return { ToolStatus::Ok, "", data };
    }
};

// 파일명 앞에 HHMMSS_ prefix를 붙여 반환 (예: output.png → 143022_output.png)
static std::string timeStampedPath(const std::string& path) {
    namespace fs = std::filesystem;
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[8];
    std::strftime(buf, sizeof(buf), "%H%M%S", &tm);
    fs::path p(path);
    return (p.parent_path() / (std::string(buf) + "_" + p.filename().string())).string();
}

// ── ImageSaver: 입력(ZMap 또는 Image2D)을 파일로 저장 (OpenCV cv::imwrite) ──────
//   ZMap → 16-bit(png/tif) 또는 8-bit(그 외, min-max 정규화). Image2D → 그대로.
class ImageSaverTool : public IAlgorithmTool {
    std::string m_path;
public:
    explicit ImageSaverTool(std::string path) : m_path(std::move(path)) {}
    std::string name() const override { return "ImageSaver"; }

    ToolResult execute(VisionDataPtr input) override {
        if (m_path.empty()) return { ToolStatus::Fail, "ImageSaver: 저장 경로가 설정되지 않았습니다" };
        if (!input)         return { ToolStatus::Fail, "ImageSaver: 입력이 없습니다" };

        const std::string savePath = timeStampedPath(m_path);
        std::string ext;
        { size_t p = savePath.rfind('.'); if (p != std::string::npos) { ext = savePath.substr(p+1); for (auto& ch : ext) ch = (char)std::tolower(ch); } }
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
    if (type == "ExposureMerge") {
        std::vector<ExposureMergeTool::ReflRoiPct> rois;
        for (auto& roi : p.value("reflRois", nlohmann::json::array())) {
            if (roi.value("shape", "rect") == "rect" && roi.contains("xPct"))
                rois.push_back({ roi.value("xPct",0.f), roi.value("yPct",0.f),
                                 roi.value("wPct",0.f), roi.value("hPct",0.f) });
        }
        return std::make_shared<ExposureMergeTool>(
            p.value("enableBfs",   true),
            p.value("enableSor",   true),
            p.value("sorKernel",   5),
            p.value("sorRatio",    2.0f),
            std::move(rois),
            p.value("seedTol",     100.0f),
            p.value("tolX",        10.0f),
            p.value("tolY",        100.0f),
            p.value("gapK",        2),
            p.value("outputStage", 4),
            noPreview);
    }
    if (type == "ImageLoader") {
        return std::make_shared<ImageLoaderTool>(p.value("path", ""));
    }
    if (type == "ImageSaver") {
        return std::make_shared<ImageSaverTool>(p.value("path", ""));
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
