#include "ToolFactory.h"
#include "HeightMapCache.h"
#include "ExposureMergeCore.h"
#include "JsonBridge.h"
#include "ImageEncoder.h"
#include "Logger.h"
#include "ThicknessMeasure.h"
#include "PlaneFitTool.h"
#include "RefHeightTool.h"
#include "HeightFromPlaneTool.h"
#include "CsvWriterTool.h"
#include "LineCenterTool.h"
#include "AlignTool.h"
#include "RegionMeasureTool.h"
#include <crow.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;
using namespace vision;

// 부모(electron) 프로세스 감시: 부모가 사라지면(정상/강제 종료 모두) 엔진도 스스로 종료.
//  → electron이 kill 되어 종료 핸들러(stopEngine)가 못 돌아도 엔진이 고아로 남지 않음.
static void startParentWatchdog(unsigned long parentPid) {
    if (parentPid == 0) return;
#ifdef _WIN32
    std::thread([parentPid]() {
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(parentPid));
        if (!h) { std::exit(0); return; }        // 부모가 이미 없음
        WaitForSingleObject(h, INFINITE);        // 부모 종료(어떤 방식이든) 시 신호
        CloseHandle(h);
        std::cout << "[VisionEngine] parent(" << parentPid << ") exited → shutting down\n";
        std::exit(0);
    }).detach();
#endif
}

// ── Topological sort (Kahn's algorithm) ─────────────────────────────────

struct Edge { std::string source, target; };

static std::vector<std::string> topoSort(
    const std::vector<std::string>& nodeIds,
    const std::vector<Edge>& edges)
{
    std::unordered_map<std::string, int> inDeg;
    std::unordered_map<std::string, std::vector<std::string>> adj;

    for (const auto& id : nodeIds) { inDeg[id] = 0; adj[id] = {}; }
    for (const auto& e : edges) {
        adj[e.source].push_back(e.target);
        inDeg[e.target]++;
    }

    std::queue<std::string> q;
    for (const auto& [id, deg] : inDeg)
        if (deg == 0) q.push(id);

    std::vector<std::string> order;
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        order.push_back(cur);
        for (const auto& nb : adj[cur])
            if (--inDeg[nb] == 0) q.push(nb);
    }
    return order;
}

// ── 노드 결과 캐시 ─────────────────────────────────────────────────────────
//  개별 노드 실행 시, 파라미터가 바뀌지 않은 상류 노드는 재실행하지 않고 캐시 재사용.
//  (단일 사용자 가정 — 접근은 g_cacheMtx로 직렬화)
struct CachedNode { VisionDataPtr output; std::size_t paramHash; };
static std::unordered_map<std::string, CachedNode> g_nodeCache;
static std::mutex g_cacheMtx;

// ── Pipeline execution ───────────────────────────────────────────────────

static json runPipeline(const json& msg, crow::websocket::connection* conn) {
    // conn==nullptr 이면 헤드리스 실행 — 이벤트 전송/프리뷰 인코딩 생략, 결과 json만 반환.
    auto emit = [&](const std::string& s){ if (conn) conn->send_text(s); };
    const bool useCache = msg.value("useCache", false);
    // 배치 검사 등 화면 표시가 필요 없을 때 미리보기(PNG 인코딩+z스캔) 생략 → 대폭 가속
    const bool noPreview = msg.value("noPreview", false);
    // 개별 노드 실행 시 이 노드는 캐시 무시하고 항상 재실행 (상류만 캐시 재사용)
    const std::string forceNode = msg.value("forceNode", std::string());
    // 배치(폴더검사) 모드 — 이번 실행에 쓴 HeightMapLoader 원본 파일을 끝나고 캐시에서 즉시 비움
    // (여러 이미지를 순회하는 워커 프로세스가 g_heightmapFileCache를 무한정 쌓아두지 않도록)
    const bool batchMode = msg.value("batch", false);
    std::lock_guard<std::mutex> cacheLock(g_cacheMtx);
    // Parse nodes
    struct NodeSpec {
        std::string id, type;
        json params;
    };
    std::vector<NodeSpec> nodeSpecs;
    std::unordered_map<std::string, int> nodeIdx;
    std::vector<std::string> heightmapPathsUsed;

    for (const auto& n : msg.at("nodes")) {
        NodeSpec ns;
        ns.id     = n.at("id").get<std::string>();
        ns.type   = n.at("type").get<std::string>();
        ns.params = n.value("params", json::object());
        if (ns.type == "HeightMapLoader") {
            auto p = ns.params.value("path", std::string());
            if (!p.empty()) heightmapPathsUsed.push_back(p);
        }
        nodeIdx[ns.id] = static_cast<int>(nodeSpecs.size());
        nodeSpecs.push_back(std::move(ns));
    }

    // Parse edges
    std::vector<Edge> edges;
    if (msg.contains("edges")) {
        for (const auto& e : msg.at("edges")) {
            edges.push_back({
                e.at("source").get<std::string>(),
                e.at("target").get<std::string>()
            });
        }
    }

    // Topological order
    std::vector<std::string> ids;
    for (const auto& ns : nodeSpecs) ids.push_back(ns.id);
    auto order = topoSort(ids, edges);

    // Which nodes' outputs feed each node (다중 입력 지원)
    std::unordered_map<std::string, std::vector<std::string>> inputsFrom;
    for (const auto& e : edges) inputsFrom[e.target].push_back(e.source);

    // Node outputs (이번 실행 범위) + 재실행 여부(dirty) 추적
    std::unordered_map<std::string, VisionDataPtr> outputs;
    std::unordered_map<std::string, bool> dirty;

    bool pipelinePass = true;
    json results = json::array();

    // Send start event
    const auto pipeStart = std::chrono::steady_clock::now();
    emit(json{{"event","start"}}.dump());

    for (const auto& nodeId : order) {
        if (nodeIdx.find(nodeId) == nodeIdx.end()) continue;
        const auto& ns = nodeSpecs[nodeIdx.at(nodeId)];

        // 캐시 키: 파라미터 해시 + 상류 dirty 여부
        const std::size_t ph = std::hash<std::string>{}(ns.params.dump());
        bool upstreamDirty = false;
        if (inputsFrom.count(nodeId))
            for (const auto& src : inputsFrom.at(nodeId))
                if (dirty.count(src) && dirty[src]) { upstreamDirty = true; break; }

        if (useCache && !upstreamDirty && nodeId != forceNode) {
            auto cit = g_nodeCache.find(nodeId);
            if (cit != g_nodeCache.end() && cit->second.paramHash == ph && cit->second.output) {
                // 캐시 적중 — 재실행/재전송 없이 출력만 재사용
                outputs[nodeId] = cit->second.output;
                dirty[nodeId] = false;
                continue;
            }
        }
        dirty[nodeId] = true;   // 재실행됨 → 하류도 dirty

        // Log tool start
        emit(json{
            {"event","log"},{"level","info"},
            {"msg", "Running " + ns.type + " [" + ns.id + "]"}
        }.dump());

        auto tool = ToolFactory::create(ns.type, ns.params, noPreview);
        if (!tool) {
            emit(json{
                {"event","log"},{"level","error"},
                {"msg", "Unknown tool type: " + ns.type}
            }.dump());
            pipelinePass = false;
            continue;
        }

        // Get input data — 여러 입력 엣지의 출력을 하나로 병합
        // (예: HeightFromPlane은 HeightMap 소스 + Plane 소스를 함께 받음)
        VisionDataPtr inputData = nullptr;
        if (inputsFrom.count(nodeId)) {
            auto merged = std::make_shared<VisionData>();
            bool any = false;
            for (const auto& src : inputsFrom.at(nodeId)) {
                auto it = outputs.find(src);
                if (it == outputs.end() || !it->second) continue;
                const auto& o = it->second;
                any = true;
                if (o->heightmap    && !merged->heightmap)    merged->heightmap    = o->heightmap;
                if (o->image   && !merged->image)   merged->image   = o->image;
                if (o->cloud   && !merged->cloud)   merged->cloud   = o->cloud;
                if (o->region  && !merged->region)  merged->region  = o->region;
                if (o->plane   && !merged->plane)   merged->plane   = o->plane;
                if (o->heights) {   // 여러 입력(예: RefHeight 평균값 + HeightMeasure 측정값들)을 한 행으로 이어붙임
                    if (!merged->heights) merged->heights = std::make_shared<std::vector<double>>();
                    merged->heights->insert(merged->heights->end(), o->heights->begin(), o->heights->end());
                }
                if (o->points) {   // 여러 입력의 기준점들을 모두 이어붙임
                    if (!merged->points) merged->points = std::make_shared<std::vector<RefPoint>>();
                    merged->points->insert(merged->points->end(), o->points->begin(), o->points->end());
                }
                if (o->origin  && !merged->origin)  merged->origin  = o->origin;
                if (merged->sourceId.empty()) merged->sourceId = o->sourceId;
            }
            if (any) inputData = merged;
        }

        auto tExec0 = std::chrono::steady_clock::now();
        auto result = tool->execute(inputData);
        auto tExec1 = std::chrono::steady_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(tExec1 - tExec0).count();
        VISION_LOG_INFO("[pipeline] {} [{:.1f} ms]", ns.type, elapsedMs);
        if (result.output) {
            outputs[nodeId] = result.output;
            g_nodeCache[nodeId] = { result.output, ph };   // 캐시 갱신
        }

        bool ok = (result.status == ToolStatus::Ok);
        if (!ok) pipelinePass = false;

        // 표시용 heightmap: 출력에 heightmap 있으면 그걸, 없으면(타입화 출력) 입력 heightmap으로 폴백.
        // → 분석 노드(PlaneFit/HeightMeasure 등)도 자기가 다룬 '입력' 이미지를 결과창에 표시.
        const HeightMap* dispZ = (result.output && result.output->hasHeightMap()) ? result.output->heightmap.get()
                          : (inputData && inputData->hasHeightMap()) ? inputData->heightmap.get() : nullptr;

        // Build per-tool result
        json jr;
        jr["event"]     = "result";
        jr["id"]        = nodeId;
        jr["tool"]      = ns.type;
        jr["ok"]        = ok;
        jr["msg"]       = result.message;
        jr["elapsedMs"] = elapsedMs;

        // 모든 노드: 출력(표시) HeightMap의 치수+좌표원점을 함께 보고 → 하류 ROI 에디터가
        //  '전파된 원점' 기준으로 ROI를 상대저장하게 한다(중간에 머지/필터가 껴도 원점 유지).
        //  (예전엔 Align 결과에만 offCol/offRow가 있어, 중간 노드가 끼면 UI 원점이 0이 됐음)
        if (dispZ) {
            jr["imgW"]      = dispZ->width;
            jr["imgH"]      = dispZ->height;
            jr["originCol"] = dispZ->originCol;   // 전파된 좌표원점(모든 노드) — Align의 offCol/offRow와 별개
            jr["originRow"] = dispZ->originRow;
        }

        // Attach measurements for known tool types
        if (ns.type == "PlaneFit") {
            auto* m = dynamic_cast<PlaneFitTool*>(tool.get());
            if (m && m->lastResult().valid) {
                const auto& r = m->lastResult();
                jr["planeA"]        = r.a;
                jr["planeB"]        = r.b;
                jr["planeC"]        = r.c;
                jr["rmse"]          = r.rmse;
                jr["tiltDeg"]       = r.tiltDeg;
                jr["refPointCount"] = r.refPointCount;
                jr["inlierCount"]   = r.inlierCount;
                json pts = json::array();
                for (const auto& p : r.cloudPoints)
                    pts.push_back({ p[0], p[1], p[2] });
                jr["cloud"] = pts;
            }
        }
        if (ns.type == "RefHeight") {
            auto* m = dynamic_cast<RefHeightTool*>(tool.get());
            if (m && m->lastResult().valid) {
                const auto& r = m->lastResult();
                jr["avgHeightMm"]   = r.avgHeightMm;
                jr["sampleCount"]   = r.sampleCount;
                jr["rejectedCount"] = r.rejectedCount;
            }
        }
        if ((ns.type == "HeightMapToCloud" || ns.type == "ExposureMergeCloud") && result.output && result.output->cloud) {
            // 3D 미리보기용 서브샘플(최대 ~50k점) — 저장 파일은 전체 해상도(영향 없음)
            const auto& cpts = result.output->cloud->points;
            const size_t cap = 50000;
            const size_t stride = cpts.size() > cap ? (cpts.size() + cap - 1) / cap : 1;
            json pts = json::array();
            for (size_t i = 0; i < cpts.size(); i += stride)
                pts.push_back({ cpts[i].x, cpts[i].y, cpts[i].z });
            jr["cloud"] = pts;
            jr["cloudTotal"] = static_cast<long long>(cpts.size());
        }
        if (ns.type == "HeightMeasure") {
            auto* m = dynamic_cast<HeightFromPlaneTool*>(tool.get());
            if (m && m->lastResult().valid) {
                const auto& r = m->lastResult();
                jr["allPass"]  = r.allPass;
                json measures = json::array();
                for (const auto& hm : r.measures) {
                    measures.push_back({
                        {"cx", hm.cx}, {"cy", hm.cy}, {"z", hm.z},
                        {"distance", hm.distance},
                        {"pointCount", hm.pointCount},
                        {"pass", hm.pass}
                    });
                }
                jr["measures"] = measures;
                if (dispZ) { jr["imgW"] = dispZ->width; jr["imgH"] = dispZ->height; }
                if (!r.allPass) pipelinePass = false;
            }
        }
        if (ns.type == "LineCenter") {
            auto* m = dynamic_cast<LineCenterTool*>(tool.get());
            if (m && m->lastResult().valid) {
                json arr = json::array();
                for (const auto& l : m->lastResult().lines) {
                    arr.push_back({
                        {"cx", l.cx}, {"cy", l.cy},
                        {"cxMm", l.cxMm}, {"cyMm", l.cyMm},
                        {"angleDeg", l.angleDeg},
                        {"roiIndex", l.roiIndex}, {"pointCount", l.pointCount}
                    });
                }
                jr["lines"] = arr;
                if (dispZ) { jr["imgW"] = dispZ->width; jr["imgH"] = dispZ->height; }
            }
        }
        if (ns.type == "Align") {
            auto* m = dynamic_cast<AlignTool*>(tool.get());
            if (m && m->lastResult().valid) {
                const auto& r = m->lastResult();
                jr["offCol"] = r.offCol;
                jr["offRow"] = r.offRow;
                jr["offXMm"] = r.offXMm;
                jr["offYMm"] = r.offYMm;
                if (dispZ) { jr["imgW"] = dispZ->width; jr["imgH"] = dispZ->height; }
            }
        }
        if (ns.type == "CsvWriter") {
            auto* m = dynamic_cast<CsvWriterTool*>(tool.get());
            if (m) {
                const auto& r = m->lastResult();
                jr["saved"]   = r.saved;
                jr["columns"] = r.columns;
            }
        }
        if (ns.type == "ThicknessMeasure") {
            auto* m = dynamic_cast<ThicknessMeasure*>(tool.get());
            if (m) {
                const auto& r = m->lastResult();
                jr["thicknessMm"] = r.thicknessMm;
                jr["minMm"]       = r.minMm;
                jr["maxMm"]       = r.maxMm;
                jr["pass"]        = r.pass;
                if (!r.pass) pipelinePass = false;
            }
        }

        if (ns.type == "RegionMeasure") {
            auto* m = dynamic_cast<RegionMeasureTool*>(tool.get());
            if (m && m->lastResult().valid) {
                const auto& r = m->lastResult();
                jr["regionAreaPx"] = r.areaPx;
                if (r.hasHeight) {
                    jr["regionAreaMm2"] = r.areaMm2;
                    jr["regionMeanZmm"] = r.meanZmm;
                }
                jr["regionCxMm"] = r.cxMm;
                jr["regionCyMm"] = r.cyMm;
            }
        }

        // 프리뷰(base64 PNG). 출력에 이미지 없으면 Region(마스크) → 입력 heightmap 순으로 폴백. noPreview면 생략.
        if (!noPreview) {
            if (result.output && result.output->hasImage())
                jr["preview"] = imageToBase64(*result.output->image);
            else if (result.output && result.output->hasRegion()) {
                // Region(마스크) 프리뷰 — 입력 heightmap 폴백보다 우선(Threshold/CreateROI 출력)
                const auto& rgn = *result.output->region;
                jr["preview"] = regionToBase64(rgn);
                jr["zMin"] = 0.f; jr["zMax"] = 255.f;
                jr["imgW"] = rgn.width; jr["imgH"] = rgn.height;
            }
            else if (dispZ) {
                // heightmapToBase64가 정규화하며 구한 z범위를 그대로 받음 (중복 스캔 제거)
                float zMin = 0, zMax = 0; bool hasRange = false;
                jr["preview"] = heightmapToBase64(*dispZ, &zMin, &zMax, &hasRange);
                if (hasRange) { jr["zMin"] = zMin; jr["zMax"] = zMax; }
                jr["xResMm"] = dispZ->xResMm;
                jr["yResMm"] = dispZ->yResMm;
            }
        }

        // 단계별 미리보기(선택) — 결과창 드롭다운용. 각 단계 HeightMap을 개별 인코딩.
        if (result.output && result.output->stages && !noPreview) {
            json stages = json::array();
            for (const auto& st : *result.output->stages) {
                if (!st.second) continue;
                float zMin = 0, zMax = 0; bool hasRange = false;
                json s;
                s["name"]    = st.first;
                s["preview"] = heightmapToBase64(*st.second, &zMin, &zMax, &hasRange);
                if (hasRange) { s["zMin"] = zMin; s["zMax"] = zMax; }
                s["xResMm"]  = st.second->xResMm;
                s["yResMm"]  = st.second->yResMm;
                stages.push_back(s);
            }
            jr["stages"] = stages;
        }

        results.push_back(jr);
        emit(jr.dump());
    }

    if (batchMode && !heightmapPathsUsed.empty()) {
        std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
        for (const auto& p : heightmapPathsUsed) g_heightmapFileCache.erase(p);
    }

    const double totalMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pipeStart).count();
    emit(json{
        {"event","log"},{"level","info"},
        {"msg", "전체 실행시간: " + std::to_string(static_cast<long long>(totalMs + 0.5)) + " ms"}
    }.dump());

    json done;
    done["event"]   = "done";
    done["pass"]    = pipelinePass;
    done["totalMs"] = totalMs;
    done["results"] = results;
    return done;
}

// ── [검증용] 반복성 분석 헤드리스 러너 (feat/repeatability 브랜치) ──────────────
//   레시피를 폴더의 모든 HeightMap에 적용해 HeightMeasure 영역별 높이/PlaneFit 파라미터를
//   수집하고, 영역별 반복성(σ, range)을 산출한다.
static double vstd(const std::vector<double>& v, double& mean) {
    if (v.empty()) { mean = 0; return 0; }
    double s = 0; for (double x : v) s += x; mean = s / v.size();
    double a = 0; for (double x : v) a += (x - mean) * (x - mean);
    return std::sqrt(a / v.size());
}
static int repeatAnalyze(const std::string& recipePath, const std::string& folder, const std::string& outCsv) {
    namespace fs = std::filesystem;
    std::ifstream rf(recipePath);
    if (!rf) { std::cerr << "recipe open fail: " << recipePath << "\n"; return 1; }
    json recipe; try { rf >> recipe; } catch (const std::exception& e) { std::cerr << "recipe parse: " << e.what() << "\n"; return 1; }

    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(folder)) {
        auto ext = e.path().extension().string();
        if (ext == ".png" || ext == ".PNG") files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::cerr << "no png in " << folder << "\n"; return 1; }
    std::cout << "[repeat-analyze] recipe=" << recipePath << " files=" << files.size() << "\n";

    std::ofstream csv(outCsv);
    std::vector<std::vector<double>> dist, npts;   // [region][sample]
    std::vector<double> planeA, planeB, planeC, rmseV, tiltV;
    bool header = false;

    for (size_t fi = 0; fi < files.size(); ++fi) {
        json msg = recipe;
        msg["cmd"] = "run"; msg["noPreview"] = true; msg["useCache"] = false; msg["batch"] = true;
        for (auto& n : msg["nodes"])
            if (n.value("type", "") == "HeightMapLoader") { n["params"]["path"] = files[fi]; n["params"]["mode"] = "file"; }
        json done = runPipeline(msg, nullptr);

        std::vector<double> d, np;
        for (const auto& r : done["results"]) {
            if (r.value("tool", "") == "HeightMeasure" && r.contains("measures")) {
                for (const auto& m : r["measures"]) { d.push_back(m.value("distance", 0.0)); np.push_back(m.value("pointCount", 0.0)); }
            }
            if (r.value("tool", "") == "PlaneFit") {
                planeA.push_back(r.value("planeA", 0.0)); planeB.push_back(r.value("planeB", 0.0));
                planeC.push_back(r.value("planeC", 0.0)); rmseV.push_back(r.value("rmse", 0.0)); tiltV.push_back(r.value("tiltDeg", 0.0));
            }
        }
        if (!header) {
            dist.resize(d.size()); npts.resize(d.size());
            csv << "file";
            for (size_t k = 0; k < d.size(); ++k) csv << ",d" << (k + 1);
            for (size_t k = 0; k < d.size(); ++k) csv << ",n" << (k + 1);
            csv << ",planeA,planeB,planeC,rmse,tiltDeg\n"; header = true;
        }
        csv << fs::path(files[fi]).filename().string();
        for (size_t k = 0; k < d.size(); ++k) { csv << "," << d[k]; if (k < dist.size()) dist[k].push_back(d[k]); }
        for (size_t k = 0; k < np.size(); ++k) { csv << "," << np[k]; if (k < npts.size()) npts[k].push_back(np[k]); }
        csv << "," << (planeA.empty()?0:planeA.back()) << "," << (planeB.empty()?0:planeB.back())
            << "," << (planeC.empty()?0:planeC.back()) << "," << (rmseV.empty()?0:rmseV.back())
            << "," << (tiltV.empty()?0:tiltV.back()) << "\n";
        if ((fi + 1) % 10 == 0) std::cout << "  " << (fi + 1) << "/" << files.size() << "\n";
    }
    csv.close();

    std::cout << "\n=== 영역별 반복성 (mm) ===\n";
    std::cout << "region   mean       sigma      range(max-min)   ptCount(mean/σ)\n";
    double sumSig = 0, maxRange = 0;
    for (size_t k = 0; k < dist.size(); ++k) {
        double mean, sig = vstd(dist[k], mean);
        double mn = *std::min_element(dist[k].begin(), dist[k].end());
        double mx = *std::max_element(dist[k].begin(), dist[k].end());
        double nmean, nsig = vstd(npts[k], nmean);
        std::printf("  %2zu   %9.5f  %9.6f  %12.6f     %.0f/%.0f\n", k + 1, mean, sig, mx - mn, nmean, nsig);
        sumSig += sig; if (mx - mn > maxRange) maxRange = mx - mn;
    }
    std::cout << "-- avg sigma=" << (dist.empty()?0:sumSig/dist.size()) << "  worst range=" << maxRange << "\n";
    double pam, pas = vstd(planeA, pam), pbm, pbs = vstd(planeB, pbm), pcm, pcs = vstd(planeC, pcm), tm, ts = vstd(tiltV, tm);
    std::printf("plane: a σ=%.6g  b σ=%.6g  c σ=%.6g  tilt mean=%.4f σ=%.4f\n", pas, pbs, pcs, tm, ts);
    std::cout << "CSV: " << outCsv << "\n";
    return 0;
}

// ── 3노출 청크 머지: 오버랩 스윕 측정 ────────────────────────────────────────
//  전체모드 캐스케이드 결과를 기준(ref)으로, 오버랩별 청크 캐스케이드 결과와 픽셀 diff.
//  ofs1/ofs2를 전체모드에서 한 번 구해 청크에도 강제 → 순수하게 "청크 경계 연속성" 오차만 격리 측정.
//  파라미터는 merge.json 기준(matchTol20/reflTol30/tolX10/tolY100/gapK2/removeReflection).
static int mergeChunkSweep(const std::string& imagePath, int chunkRowsInput, const std::string& outCsv, int overlapOutOverride = -1) {
    using namespace vision;
    const float matchTol=20.f, reflTol=30.f, tolX=10.f, tolY=100.f;
    const int   gapK=2; const bool removeReflection=true;
    const float xRes=0.0063f, yRes=0.033f, zRes=0.00105f;
    const float NaN = std::numeric_limits<float>::quiet_NaN();

    auto zm = loadHeightMapFromFile(imagePath, xRes, yRes, zRes);
    if (!zm || zm->empty()) { std::cerr << "load fail: " << imagePath << "\n"; return 1; }
    const int w = zm->width, h = zm->height, n = h/3;
    const size_t BN = (size_t)n*w;
    std::cout << "[sweep] " << std::filesystem::path(imagePath).filename().string()
              << " " << w << "x" << h << " -> n=" << n
              << "  chunkRows(in)=" << chunkRowsInput << " (out=" << chunkRowsInput/3 << ")\n";

    // 저/중/장 분리 (행 r -> 전역행 3r,3r+1,3r+2). 람다로 빼서 타이밍 안에서도 재실행 가능.
    std::vector<float> lo(BN), mid(BN), hi(BN);
    auto splitFull=[&](){
        for (int r=0;r<n;++r) for (int c=0;c<w;++c){ size_t i=(size_t)r*w+c;
            lo[i]=zm->data[(size_t)(3*r)*w+c]; mid[i]=zm->data[(size_t)(3*r+1)*w+c]; hi[i]=zm->data[(size_t)(3*r+2)*w+c]; }
    };
    // 확장블록 [e0,e1)의 저/중/장을 zm->data에서 직접 추출(청크 행분리 = 블록 단위).
    auto splitBlock=[&](int e0,int e1, std::vector<float>&loB, std::vector<float>&midB, std::vector<float>&hiB){
        int bn=e1-e0; loB.resize((size_t)bn*w); midB.resize((size_t)bn*w); hiB.resize((size_t)bn*w);
        for (int rr=0;rr<bn;++rr){ int gr=e0+rr; for(int c=0;c<w;++c){ size_t bi=(size_t)rr*w+c;
            loB[bi]=zm->data[(size_t)(3*gr)*w+c]; midB[bi]=zm->data[(size_t)(3*gr+1)*w+c]; hiB[bi]=zm->data[(size_t)(3*gr+2)*w+c]; } }
    };
    splitFull();

    auto medianDiff=[&](const std::vector<float>&A,const std::vector<float>&B){
        std::vector<float> d; d.reserve(BN/4+1);
        for(size_t i=0;i<BN;i+=4) if(!std::isnan(A[i])&&!std::isnan(B[i])&&std::fabs(A[i]-B[i])<=matchTol) d.push_back(A[i]-B[i]);
        if(d.empty())return 0.f; size_t m=d.size()/2; std::nth_element(d.begin(),d.begin()+m,d.end()); return d[m];
    };
    // 블록 [e0,e1)에 대해 한 스테이지 실행 -> source 적용 Z (블록 로컬 크기 반환)
    auto stageBlock=[&](const float* low,const float* high,int e0,int e1,float off){
        int bn=e1-e0; std::vector<uint8_t> src;
        exposureMergeDecision(low+(size_t)e0*w, high+(size_t)e0*w, w, bn, matchTol,tolX,tolY,gapK, off, src, nullptr, removeReflection, reflTol);
        std::vector<float> out((size_t)bn*w);
        for(size_t i=0;i<(size_t)bn*w;++i){ uint8_t s=src[i]; out[i]= s==1? low[(size_t)e0*w+i]-off : (s==2? high[(size_t)e0*w+i] : NaN); }
        return out;
    };

    // 전체모드 기준(reference): ofs1 -> mergedA(full) -> ofs2 -> finalZ(full)
    using clk = std::chrono::steady_clock;
    const int WARM = 1, REP = 7;   // 워밍업 + 반복 측정(min/avg로 노이즈 제거)
    const float ofs1 = medianDiff(lo, mid);
    std::vector<float> mergedA = stageBlock(lo.data(), mid.data(), 0, n, ofs1);
    const float ofs2 = medianDiff(mergedA, hi);
    std::vector<float> ref = stageBlock(mergedA.data(), hi.data(), 0, n, ofs2);
    // n행 src를 3n행으로 복제(halfRes=false 출력 재구성). 실제 makeOut과 동일한 parallel_for 복사.
    auto tripleRows=[&](const std::vector<float>& src, int rows, std::vector<float>& out){
        out.resize((size_t)3*rows*w);
        cv::parallel_for_(cv::Range(0,rows), [&](const cv::Range& rg){
            for(int r=rg.start;r<rg.end;++r) for(int s=0;s<3;++s)
                std::copy(&src[(size_t)r*w], &src[(size_t)r*w+w], &out[(size_t)(3*r+s)*w]);
        });
    };
    // 전체모드 반복 측정 (행분리 + 오프셋 + 머지 + 리플제거; halfRes on/off 각각)
    double fullMin=1e18, fullSum=0;          // halfRes=true (n행 출력)
    double fullTMin=1e18, fullTSum=0;        // halfRes=false (3n행 복제 포함)
    std::vector<float> out3;
    for (int it=0; it<WARM+REP; ++it) {
        auto t0 = clk::now();
        splitFull();                                        // ① 행분리
        float o1 = medianDiff(lo, mid);                     // ② 오프셋1
        std::vector<float> mA = stageBlock(lo.data(), mid.data(), 0, n, o1);   // ③ 머지1+리플제거
        float o2 = medianDiff(mA, hi);                      // ④ 오프셋2
        std::vector<float> fz = stageBlock(mA.data(), hi.data(), 0, n, o2);    // ⑤ 머지2+리플제거
        auto tHalf = clk::now();
        tripleRows(fz, n, out3);                             // ⑥ 3n행 복제(halfRes=false)
        auto tFull = clk::now();
        double msH = std::chrono::duration<double,std::milli>(tHalf-t0).count();
        double msF = std::chrono::duration<double,std::milli>(tFull-t0).count();
        if (it>=WARM){ fullMin=std::min(fullMin,msH); fullSum+=msH; fullTMin=std::min(fullTMin,msF); fullTSum+=msF; }
    }
    double msFull = fullMin;   // 대표값 = 최소(가장 방해 적은 실행)
    std::cout << "[sweep] ofs1=" << ofs1 << " ofs2=" << ofs2 << "  (offset 고정, 양 모드 공유)\n";
    std::printf("[sweep] 전체모드 halfRes=ON  (n행 출력)      : min %.2f ms, avg %.2f ms\n", fullMin, fullSum/REP);
    std::printf("[sweep] 전체모드 halfRes=OFF (3n행 복제 포함): min %.2f ms, avg %.2f ms  (복제비용 +%.2f ms)\n", fullTMin, fullTSum/REP, fullTMin-fullMin);
    // 검증: 청크 모드가 실제로 쓰는 pre-filter ofs2가 전체모드 ofs2와 같은지 (같아야 전역 시프트 없음)
    { std::vector<float> mApre(BN);
      for (size_t i=0;i<BN;++i) mApre[i] = !std::isnan(lo[i]) ? lo[i]-ofs1 : (!std::isnan(mid[i]) ? mid[i] : NaN);
      float ofs2pre = medianDiff(mApre, hi);
      std::cout << "[sweep] ofs2(pre-filter, 청크가 쓰는 값)=" << ofs2pre << " vs ofs2(full)=" << ofs2
                << (ofs2pre==ofs2 ? "  → MATCH (근사 안전)" : "  → DIFFER (전역 시프트 주의)") << "\n"; }

    const int chunkOut = std::max(1, chunkRowsInput/3);
    const int ovDefault[] = {0,5,10,20,40,80,160,320};   // 출력행 단위 오버랩
    const int ovSingle[]  = {overlapOutOverride};
    const int* ovList = overlapOutOverride>=0 ? ovSingle : ovDefault;
    const int  ovCount = overlapOutOverride>=0 ? 1 : (int)(sizeof(ovDefault)/sizeof(ovDefault[0]));

    std::ofstream csv(outCsv);
    csv << "overlap_out,overlap_in,nDiff,nNanMismatch,maxAbsCount,maxAbsMm,pctDiff,maxSeamDist,msChunk,msFull\n";
    std::printf("\n ovOut(ovIn)   nDiff        maxAbs(cnt)  maxAbs(mm)   %%diff      maxSeamDist\n");
    std::printf(" ------------- ------------ ------------ ------------ ---------- -----------\n");
    for (int oi = 0; oi < ovCount; ++oi) { int ov = ovList[oi];
        std::vector<float> chunk(BN, NaN);
        double totOnMin=1e18, totOnSum=0, totOffMin=1e18, totOffSum=0;   // 청크모드 전체: halfRes on/off
        double perMin=1e18, perMax=0, perSum=0; long perCnt=0;           // 풀 청크 1개(halfRes ON, 복제 제외)
        double perTMin=1e18, perTSum=0;                                  // 풀 청크 1개(halfRes OFF, 복제 포함)
        int nChunks=0, nFull=0;
        std::vector<float> loB, midB, hiB, mApre(BN), o3;
        for (int it=0; it<WARM+REP; ++it) {
            nChunks=0; nFull=0;
            // 전역 오프셋 up-front (1회): 전체 분리 → ofs1 → pre-filter ofs2 근사
            auto tOff0 = clk::now();
            splitFull();
            float co1 = medianDiff(lo, mid);
            for(size_t i=0;i<BN;++i) mApre[i] = !std::isnan(lo[i]) ? lo[i]-co1 : (!std::isnan(mid[i]) ? mid[i] : NaN);
            float co2 = medianDiff(mApre, hi);
            double offMs = std::chrono::duration<double,std::milli>(clk::now()-tOff0).count();
            double iterMerge=0, iterTriple=0;   // 이 반복의 청크 머지 합 / 복제 합
            for (int p0=0;p0<n;p0+=chunkOut){
                auto tc0 = clk::now();
                int p1=std::min(n,p0+chunkOut);
                int e0=std::max(0,p0-ov), e1=std::min(n,p1+ov); int bn=e1-e0;
                splitBlock(e0,e1, loB,midB,hiB);            // 청크 행분리(블록 단위)
                // stage1 (저+중) 블록
                std::vector<uint8_t> s1;
                exposureMergeDecision(loB.data(), midB.data(), w, bn, matchTol,tolX,tolY,gapK, co1, s1, nullptr, removeReflection, reflTol);
                std::vector<float> mA((size_t)bn*w);
                for(size_t i=0;i<(size_t)bn*w;++i){ uint8_t s=s1[i]; mA[i]= s==1? loB[i]-co1 : (s==2? midB[i] : NaN); }
                // stage2 (저·중 + 장) 블록
                std::vector<uint8_t> s2;
                exposureMergeDecision(mA.data(), hiB.data(), w, bn, matchTol,tolX,tolY,gapK, co2, s2, nullptr, removeReflection, reflTol);
                for(int r=p0;r<p1;++r) for(int c=0;c<w;++c){ size_t li=(size_t)(r-e0)*w+c; uint8_t s=s2[li];
                    chunk[(size_t)r*w+c]= s==1? mA[li]-co2 : (s==2? hiB[li] : NaN); }
                double cms = std::chrono::duration<double,std::milli>(clk::now()-tc0).count();
                // 코어 행 3배 복제(halfRes=false 출력)
                int rows=p1-p0; auto tt0=clk::now();
                o3.resize((size_t)3*rows*w);
                cv::parallel_for_(cv::Range(0,rows),[&](const cv::Range& rg){
                    for(int r=rg.start;r<rg.end;++r){ const float* srow=&chunk[(size_t)(p0+r)*w];
                        for(int s=0;s<3;++s) std::copy(srow,srow+w,&o3[(size_t)(3*r+s)*w]); } });
                double tms = std::chrono::duration<double,std::milli>(clk::now()-tt0).count();
                ++nChunks; iterMerge+=cms; iterTriple+=tms;
                if (it>=WARM && rows>=chunkOut){ ++nFull; ++perCnt;
                    perSum+=cms; perMin=std::min(perMin,cms); perMax=std::max(perMax,cms);
                    perTSum+=cms+tms; perTMin=std::min(perTMin,cms+tms); }
            }
            if (it>=WARM){
                double on=offMs+iterMerge, off=offMs+iterMerge+iterTriple;
                totOnMin=std::min(totOnMin,on); totOnSum+=on;
                totOffMin=std::min(totOffMin,off); totOffSum+=off;
            }
        }
        double msChunk = totOnMin;
        std::printf("[sweep] 청크모드 전체(%d청크, 풀 %d):  halfRes=ON  min %.2f ms / halfRes=OFF(3n복제) min %.2f ms  (복제 +%.2f ms)\n",
                    nChunks, nFull, totOnMin, totOffMin, totOffMin-totOnMin);
        std::printf("[sweep] 청크 1개 halfRes=ON : min %.2f, avg %.2f, max %.2f ms\n", perMin, perCnt?perSum/perCnt:0.0, perMax);
        std::printf("[sweep] 청크 1개 halfRes=OFF: min %.2f, avg %.2f ms (복제 포함)\n", perTMin, perCnt?perTSum/perCnt:0.0);
        std::printf("[sweep] (참고) 오버랩 %d출력행, 표본 %ld\n", ov, perCnt);
        // diff vs ref
        long nDiff=0,nNan=0; double maxAbs=0; int maxSeamDist=0;
        for(int r=0;r<n;++r){ int seam=std::min(r%chunkOut, chunkOut-(r%chunkOut));
          for(int c=0;c<w;++c){ size_t i=(size_t)r*w+c; float a=ref[i],b=chunk[i];
            bool na=std::isnan(a), nb=std::isnan(b), diff=false;
            if(na!=nb){ nNan++; diff=true; }
            else if(!na){ double d=std::fabs((double)a-(double)b); if(d>0){ if(d>maxAbs)maxAbs=d; diff=true; } }
            if(diff){ nDiff++; if(seam>maxSeamDist)maxSeamDist=seam; } } }
        double pct=100.0*(double)nDiff/(double)BN;
        std::printf(" %5d(%5d)  %11ld  %11.1f  %11.5f  %8.4f%%  %10d   %8.1f ms (full %.1f ms)\n",
                    ov, ov*3, nDiff, maxAbs, maxAbs*zRes, pct, maxSeamDist, msChunk, msFull);
        csv << ov << "," << ov*3 << "," << nDiff << "," << nNan << "," << maxAbs << ","
            << (maxAbs*zRes) << "," << pct << "," << maxSeamDist << "," << msChunk << "," << msFull << "\n";
    }
    csv.close();
    std::cout << "\nCSV: " << outCsv << "\n"
              << "해석: nDiff=전체모드와 다른 픽셀 수, maxSeamDist=diff가 청크경계에서 떨어진 최대거리(작을수록 경계국소).\n";
    return 0;
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    vision::LoggerInit();

    int port = 9000;
    unsigned long parentPid = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        }
        if (arg == "--parent-pid" && i + 1 < argc) {
            parentPid = std::strtoul(argv[++i], nullptr, 10);
        }
        if (arg == "--repeat-analyze" && i + 3 < argc) {
            return repeatAnalyze(argv[i + 1], argv[i + 2], argv[i + 3]);   // <recipe> <folder> <out.csv>
        }
        if (arg == "--merge-chunk-sweep" && i + 3 < argc) {
            int ovOut = (i + 4 < argc) ? std::atoi(argv[i + 4]) : -1;                    // 선택: 특정 오버랩(출력행)만
            return mergeChunkSweep(argv[i + 1], std::atoi(argv[i + 2]), argv[i + 3], ovOut);   // <image> <chunkRowsInput> <out.csv> [overlapOut]
        }
    }
    startParentWatchdog(parentPid);   // 부모 종료 시 엔진 자동 종료 (고아 방지)
    std::cout << "[VisionEngine] Starting on ws://localhost:" << port << "\n";

    crow::SimpleApp app;
    std::mutex connMtx;
    std::unordered_set<crow::websocket::connection*> clients;

    CROW_WEBSOCKET_ROUTE(app, "/")
        .onopen([&](crow::websocket::connection& conn) {
            std::lock_guard<std::mutex> lk(connMtx);
            clients.insert(&conn);
            std::cout << "[VisionEngine] Client connected\n";
            conn.send_text(json{{"event","ready"}}.dump());
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t /*code*/) {
            std::lock_guard<std::mutex> lk(connMtx);
            clients.erase(&conn);
            { std::lock_guard<std::mutex> ck(g_cacheMtx); g_nodeCache.clear(); }
            std::cout << "[VisionEngine] Client disconnected: " << reason << "\n";
        })
        .onmessage([&](crow::websocket::connection& conn,
                       const std::string& data, bool /*is_binary*/) {
            try {
                auto msg = json::parse(data);
                std::string cmd = msg.value("cmd", "");

                if (cmd == "ping") {
                    conn.send_text(json{{"event","pong"}}.dump());
                    return;
                }
                if (cmd == "run") {
                    auto done = runPipeline(msg, &conn);
                    conn.send_text(done.dump());
                    return;
                }
                if (cmd == "prefetch") {
                    // 폴더검사 워커풀용 — 지정한 파일 1장을 백그라운드 스레드에서 미리 로드해
                    // g_heightmapFileCache에 채워둠. 현재 run 처리 중에도 non-blocking으로 동작하도록
                    // 커넥션 핸들러를 바로 리턴하고 detach된 스레드가 실제 로딩을 담당.
                    std::string path = msg.value("path", "");
                    if (!path.empty()) {
                        float xRes = msg.value("xResMm", 1.0f);
                        float yRes = msg.value("yResMm", 1.0f);
                        float zRes = msg.value("zResMm", 0.001f);
                        std::thread([path, xRes, yRes, zRes]() {
                            {
                                std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
                                if (g_heightmapFileCache.count(path)) return;   // 이미 있음
                            }
                            auto heightmap = vision::loadHeightMapFromFile(path, xRes, yRes, zRes);
                            if (!heightmap) return;
                            std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
                            vision::heightmapCachePut(path, heightmap);
                        }).detach();
                    }
                    return;
                }
                if (cmd == "preload") {
                    std::string folder = msg.value("folder", "");
                    if (!folder.empty()) {
                        float xRes = msg.value("xResMm", 1.0f);
                        float yRes = msg.value("yResMm", 1.0f);
                        float zRes = msg.value("zResMm", 0.001f);
                        int loaded = vision::preloadFolder(folder, xRes, yRes, zRes);
                        VISION_LOG_INFO("preload: {} files cached from {}", loaded, folder);
                        conn.send_text(json{{"event","preloadDone"},{"loaded",loaded}}.dump());
                    }
                    return;
                }
                conn.send_text(json{{"event","error"},{"msg","unknown cmd: " + cmd}}.dump());
            } catch (const std::exception& e) {
                conn.send_text(json{{"event","error"},{"msg", e.what()}}.dump());
            }
        });

    app.port(port).multithreaded().run();
    return 0;
}
