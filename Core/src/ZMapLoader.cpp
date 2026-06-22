#include "IZMapLoader.h"
#include "Logger.h"
#include <fstream>
#include <stdexcept>

namespace vision {

ZMapPtr RawBinaryZMapLoader::load(const std::string& path,
                                   float xResMm, float yResMm, float zResMm) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("ZMap file not found: " + path);

    int32_t w = 0, h = 0;
    f.read(reinterpret_cast<char*>(&w), sizeof(w));
    f.read(reinterpret_cast<char*>(&h), sizeof(h));

    if (w <= 0 || h <= 0 || w > 65536 || h > 65536)
        throw std::runtime_error("Invalid ZMap dimensions in: " + path);

    auto map = std::make_shared<ZMap>();
    map->width   = w;
    map->height  = h;
    map->xResMm  = xResMm;
    map->yResMm  = yResMm;
    map->zResMm  = zResMm;
    map->data.resize(static_cast<size_t>(w) * h);

    f.read(reinterpret_cast<char*>(map->data.data()),
           static_cast<std::streamsize>(map->data.size() * sizeof(float)));

    if (!f) throw std::runtime_error("ZMap read failed (truncated?): " + path);

    VISION_LOG_INFO("ZMapLoader: loaded {}x{} from {}", w, h, path);
    return map;
}

bool saveZMapRaw(const ZMap& map, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    int32_t w = map.width, h = map.height;
    f.write(reinterpret_cast<const char*>(&w), sizeof(w));
    f.write(reinterpret_cast<const char*>(&h), sizeof(h));
    f.write(reinterpret_cast<const char*>(map.data.data()),
            static_cast<std::streamsize>(map.data.size() * sizeof(float)));

    return f.good();
}

} // namespace vision
