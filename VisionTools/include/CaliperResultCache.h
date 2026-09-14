#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
namespace vision {
struct CaliperElemView { std::string type; bool valid=false; double sMm=0,zMm=0,slope=0,intercept=0,rmse=0,fromMm=0,toMm=0; };
struct CaliperMeasView { double value=0; std::string unit; bool hasDecision=false; bool pass=false; };
struct CaliperProfileResult { std::vector<CaliperElemView> elems; std::vector<CaliperMeasView> meas; };
class CaliperResultCache {
public:
  static CaliperResultCache& instance();
  void set(const std::string& nodeId, std::vector<CaliperProfileResult> r);
  bool get(const std::string& nodeId, int idx, CaliperProfileResult& out) const;
private:
  mutable std::mutex m_;
  std::unordered_map<std::string, std::vector<CaliperProfileResult>> map_;
};
}
