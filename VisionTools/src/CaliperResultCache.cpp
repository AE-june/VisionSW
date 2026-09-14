#include "CaliperResultCache.h"

namespace vision {

CaliperResultCache& CaliperResultCache::instance() {
    static CaliperResultCache inst;
    return inst;
}

void CaliperResultCache::set(const std::string& nodeId, std::vector<CaliperProfileResult> r) {
    if (nodeId.empty()) return;
    std::lock_guard<std::mutex> lk(m_);
    map_[nodeId] = std::move(r);
}

bool CaliperResultCache::get(const std::string& nodeId, int idx, CaliperProfileResult& out) const {
    std::lock_guard<std::mutex> lk(m_);
    auto it = map_.find(nodeId);
    if (it == map_.end() || idx < 0 || idx >= (int)it->second.size())
        return false;
    out = it->second[idx];
    return true;
}

} // namespace vision
