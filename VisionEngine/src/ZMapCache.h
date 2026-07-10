#pragma once
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <string>

namespace vision {

struct ZMap;

extern std::unordered_map<std::string, std::shared_ptr<ZMap>> g_zmapFileCache;
extern std::unordered_set<std::string> g_preloadedFolders;
extern std::mutex g_zmapFileCacheMtx;

std::shared_ptr<ZMap> loadZMapFromFile(const std::string& path, float xRes, float yRes, float zRes);

// Preload all PNGs in folder using multiple threads. Returns count of newly loaded files.
int preloadFolder(const std::string& folder, float xRes, float yRes, float zRes);

} // namespace vision
