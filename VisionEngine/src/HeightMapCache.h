#pragma once
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <string>

namespace vision {

struct HeightMap;

extern std::unordered_map<std::string, std::shared_ptr<HeightMap>> g_heightmapFileCache;
extern std::unordered_set<std::string> g_preloadedFolders;
extern std::mutex g_heightmapFileCacheMtx;

std::shared_ptr<HeightMap> loadHeightMapFromFile(const std::string& path, float xRes, float yRes, float zRes);

// path→HeightMap 캐시에 넣되 상한(최근 N장) 초과 시 오래된 것 축출. g_heightmapFileCacheMtx 보유 상태에서 호출.
void heightmapCachePut(const std::string& path, const std::shared_ptr<HeightMap>& zm);

// Preload all PNGs in folder using multiple threads. Returns count of newly loaded files.
int preloadFolder(const std::string& folder, float xRes, float yRes, float zRes);

} // namespace vision
