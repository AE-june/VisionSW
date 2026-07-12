#include "ToolFactory.h"
#include "ZMapCache.h"
#include "JsonBridge.h"
#include "ImageEncoder.h"
#include "Logger.h"
#include "ThicknessMeasure.h"
#include "PlaneFitTool.h"
#include "HeightFromPlaneTool.h"
#include "CsvWriterTool.h"
#include "LineCenterTool.h"
#include "AlignTool.h"
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

using json = nlohmann::json;
using namespace vision;

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
    // 배치(폴더검사) 모드 — 이번 실행에 쓴 ZMapLoader 원본 파일을 끝나고 캐시에서 즉시 비움
    // (여러 이미지를 순회하는 워커 프로세스가 g_zmapFileCache를 무한정 쌓아두지 않도록)
    const bool batchMode = msg.value("batch", false);
    std::lock_guard<std::mutex> cacheLock(g_cacheMtx);
    // Parse nodes
    struct NodeSpec {
        std::string id, type;
        json params;
    };
    std::vector<NodeSpec> nodeSpecs;
    std::unordered_map<std::string, int> nodeIdx;
    std::vector<std::string> zmapPathsUsed;

    for (const auto& n : msg.at("nodes")) {
        NodeSpec ns;
        ns.id     = n.at("id").get<std::string>();
        ns.type   = n.at("type").get<std::string>();
        ns.params = n.value("params", json::object());
        if (ns.type == "ZMapLoader") {
            auto p = ns.params.value("path", std::string());
            if (!p.empty()) zmapPathsUsed.push_back(p);
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
        // (예: HeightFromPlane은 ZMap 소스 + Plane 소스를 함께 받음)
        VisionDataPtr inputData = nullptr;
        if (inputsFrom.count(nodeId)) {
            auto merged = std::make_shared<VisionData>();
            bool any = false;
            for (const auto& src : inputsFrom.at(nodeId)) {
                auto it = outputs.find(src);
                if (it == outputs.end() || !it->second) continue;
                const auto& o = it->second;
                any = true;
                if (o->zmap    && !merged->zmap)    merged->zmap    = o->zmap;
                if (o->image   && !merged->image)   merged->image   = o->image;
                if (o->cloud   && !merged->cloud)   merged->cloud   = o->cloud;
                if (o->plane   && !merged->plane)   merged->plane   = o->plane;
                if (o->heights && !merged->heights) merged->heights = o->heights;
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

        // 표시용 zmap: 출력에 zmap 있으면 그걸, 없으면(타입화 출력) 입력 zmap으로 폴백.
        // → 분석 노드(PlaneFit/HeightMeasure 등)도 자기가 다룬 '입력' 이미지를 결과창에 표시.
        const ZMap* dispZ = (result.output && result.output->hasZMap()) ? result.output->zmap.get()
                          : (inputData && inputData->hasZMap()) ? inputData->zmap.get() : nullptr;

        // Build per-tool result
        json jr;
        jr["event"]     = "result";
        jr["id"]        = nodeId;
        jr["tool"]      = ns.type;
        jr["ok"]        = ok;
        jr["msg"]       = result.message;
        jr["elapsedMs"] = elapsedMs;

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
        if (ns.type == "ZMapToCloud" && result.output && result.output->cloud) {
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

        // 프리뷰(base64 PNG). 출력에 이미지 없으면 입력 zmap으로 폴백(dispZ). noPreview면 생략(배치 가속).
        if (!noPreview) {
            if (result.output && result.output->hasImage())
                jr["preview"] = imageToBase64(*result.output->image);
            else if (dispZ) {
                // zmapToBase64가 정규화하며 구한 z범위를 그대로 받음 (중복 스캔 제거)
                float zMin = 0, zMax = 0; bool hasRange = false;
                jr["preview"] = zmapToBase64(*dispZ, &zMin, &zMax, &hasRange);
                if (hasRange) { jr["zMin"] = zMin; jr["zMax"] = zMax; }
                jr["xResMm"] = dispZ->xResMm;
                jr["yResMm"] = dispZ->yResMm;
            }
        }

        // 단계별 미리보기(선택) — 결과창 드롭다운용. 각 단계 ZMap을 개별 인코딩.
        if (result.output && result.output->stages && !noPreview) {
            json stages = json::array();
            for (const auto& st : *result.output->stages) {
                if (!st.second) continue;
                float zMin = 0, zMax = 0; bool hasRange = false;
                json s;
                s["name"]    = st.first;
                s["preview"] = zmapToBase64(*st.second, &zMin, &zMax, &hasRange);
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

    if (batchMode && !zmapPathsUsed.empty()) {
        std::lock_guard<std::mutex> lk(g_zmapFileCacheMtx);
        for (const auto& p : zmapPathsUsed) g_zmapFileCache.erase(p);
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
//   레시피를 폴더의 모든 ZMap에 적용해 HeightMeasure 영역별 높이/PlaneFit 파라미터를
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
            if (n.value("type", "") == "ZMapLoader") { n["params"]["path"] = files[fi]; n["params"]["mode"] = "file"; }
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

// ── Main ─────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    vision::LoggerInit();

    int port = 9000;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        }
        if (arg == "--repeat-analyze" && i + 3 < argc) {
            return repeatAnalyze(argv[i + 1], argv[i + 2], argv[i + 3]);   // <recipe> <folder> <out.csv>
        }
    }
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
                    // g_zmapFileCache에 채워둠. 현재 run 처리 중에도 non-blocking으로 동작하도록
                    // 커넥션 핸들러를 바로 리턴하고 detach된 스레드가 실제 로딩을 담당.
                    std::string path = msg.value("path", "");
                    if (!path.empty()) {
                        float xRes = msg.value("xResMm", 1.0f);
                        float yRes = msg.value("yResMm", 1.0f);
                        float zRes = msg.value("zResMm", 0.001f);
                        std::thread([path, xRes, yRes, zRes]() {
                            {
                                std::lock_guard<std::mutex> lk(g_zmapFileCacheMtx);
                                if (g_zmapFileCache.count(path)) return;   // 이미 있음
                            }
                            auto zmap = vision::loadZMapFromFile(path, xRes, yRes, zRes);
                            if (!zmap) return;
                            std::lock_guard<std::mutex> lk(g_zmapFileCacheMtx);
                            g_zmapFileCache[path] = zmap;
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
