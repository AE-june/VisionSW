#include "HeightMapCache.h"
#include "HeightMap.h"

#include <limits>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <deque>
#include <filesystem>
#include <thread>
#include <atomic>
#include <fstream>
#include <system_error>

// stb for PNG/JPG loading
// STBI_WINDOWS_UTF8: stbi__fopen이 char* 경로를 시스템 ANSI 코드페이지가 아니라
// UTF-8로 해석해 _wfopen으로 열도록 함. 없으면 비-ASCII(한글 등) 경로의 파일을
// 전혀 못 읽는다(코드페이지에 없는 문자는 fopen 자체가 실패).
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
// OpenCV for TIFF decode (16-bit 포함)
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

static std::shared_ptr<HeightMap> loadTiffFromMemory(
        const uint8_t* data, size_t size, float xRes, float yRes, float zRes) {
    // OpenCV가 이미 의존성으로 존재 — imdecode로 TIFF(압축 포함) 처리
    cv::Mat buf(1, static_cast<int>(size), CV_8U, const_cast<uint8_t*>(data));
    cv::Mat img = cv::imdecode(buf, cv::IMREAD_ANYDEPTH | cv::IMREAD_ANYCOLOR);
    if (img.empty()) return nullptr;
    if (img.channels() > 1) cv::cvtColor(img, img, cv::COLOR_BGR2GRAY);

    auto hm = std::make_shared<HeightMap>();
    hm->width = img.cols; hm->height = img.rows;
    hm->xResMm = xRes; hm->yResMm = yRes; hm->zResMm = zRes;
    hm->data.resize(static_cast<size_t>(img.cols) * img.rows);

    if (img.depth() == CV_16U) {
        hm->zZeroCount = 32768.f;
        const uint16_t* src = reinterpret_cast<const uint16_t*>(img.data);
        for (size_t i = 0; i < hm->data.size(); ++i)
            hm->data[i] = (src[i] == 0) ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(src[i]);
    } else if (img.depth() == CV_8U) {
        hm->zZeroCount = 128.f;
        const uint8_t* src = img.data;
        for (size_t i = 0; i < hm->data.size(); ++i)
            hm->data[i] = (src[i] == 0) ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(src[i]);
    } else {
        return nullptr;
    }
    return hm;
}

std::shared_ptr<HeightMap> loadHeightMapFromFile(const std::string& path,
                                       float xRes, float yRes, float zRes) {
    namespace fs = std::filesystem;
    // Read via u8path so Korean/Unicode paths work on Windows (fopen uses ANSI otherwise)
    std::ifstream ifs(fs::u8path(path), std::ios::binary | std::ios::ate);
    if (!ifs) return nullptr;
    auto fileSize = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0);
    std::vector<stbi_uc> fileBuf(fileSize);
    ifs.read(reinterpret_cast<char*>(fileBuf.data()), static_cast<std::streamsize>(fileSize));
    ifs.close();

    // TIFF magic: II\x2A\x00 (LE) or MM\x00\x2A (BE)
    if (fileSize >= 4 &&
        ((fileBuf[0]=='I' && fileBuf[1]=='I' && fileBuf[2]==0x2A && fileBuf[3]==0x00) ||
         (fileBuf[0]=='M' && fileBuf[1]=='M' && fileBuf[2]==0x00 && fileBuf[3]==0x2A))) {
        return loadTiffFromMemory(fileBuf.data(), fileSize, xRes, yRes, zRes);
    }

    int w, h, ch;
    uint16_t* raw16 = stbi_load_16_from_memory(fileBuf.data(), static_cast<int>(fileSize), &w, &h, &ch, 1);
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
    unsigned char* raw8 = stbi_load_from_memory(fileBuf.data(), static_cast<int>(fileSize), &w, &h, &ch, 1);
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
            auto ext = e.path().extension().string();
            if (ext == ".png" || ext == ".tif" || ext == ".tiff") {
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

} // namespace vision
