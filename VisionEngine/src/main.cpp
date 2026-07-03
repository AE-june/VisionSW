#include "ToolFactory.h"
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
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <memory>
#include <iostream>
#include <chrono>

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

static json runPipeline(const json& msg, crow::websocket::connection& conn) {
    const bool useCache = msg.value("useCache", false);
    // 배치 검사 등 화면 표시가 필요 없을 때 미리보기(PNG 인코딩+z스캔) 생략 → 대폭 가속
    const bool noPreview = msg.value("noPreview", false);
    // 개별 노드 실행 시 이 노드는 캐시 무시하고 항상 재실행 (상류만 캐시 재사용)
    const std::string forceNode = msg.value("forceNode", std::string());
    std::lock_guard<std::mutex> cacheLock(g_cacheMtx);
    // Parse nodes
    struct NodeSpec {
        std::string id, type;
        json params;
    };
    std::vector<NodeSpec> nodeSpecs;
    std::unordered_map<std::string, int> nodeIdx;

    for (const auto& n : msg.at("nodes")) {
        NodeSpec ns;
        ns.id     = n.at("id").get<std::string>();
        ns.type   = n.at("type").get<std::string>();
        ns.params = n.value("params", json::object());
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
    conn.send_text(json{{"event","start"}}.dump());

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
        conn.send_text(json{
            {"event","log"},{"level","info"},
            {"msg", "Running " + ns.type + " [" + ns.id + "]"}
        }.dump());

        auto tool = ToolFactory::create(ns.type, ns.params);
        if (!tool) {
            conn.send_text(json{
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
        if (result.output) {
            outputs[nodeId] = result.output;
            g_nodeCache[nodeId] = { result.output, ph };   // 캐시 갱신
        }

        bool ok = (result.status == ToolStatus::Ok);
        if (!ok) pipelinePass = false;

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
                if (result.output && result.output->zmap) {
                    jr["imgW"] = result.output->zmap->width;
                    jr["imgH"] = result.output->zmap->height;
                }
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
                if (result.output && result.output->zmap) {
                    jr["imgW"] = result.output->zmap->width;
                    jr["imgH"] = result.output->zmap->height;
                }
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
                if (result.output && result.output->zmap) {
                    jr["imgW"] = result.output->zmap->width;
                    jr["imgH"] = result.output->zmap->height;
                }
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

        // Attach image preview (base64 PNG). noPreview면 인코딩/스캔 전부 생략(배치 가속).
        if (result.output && !noPreview) {
            if (result.output->hasImage())
                jr["preview"] = imageToBase64(*result.output->image);
            else if (result.output->hasZMap()) {
                const auto& zm = *result.output->zmap;
                // zmapToBase64가 정규화하며 구한 z범위를 그대로 받음 (중복 스캔 제거)
                float zMin = 0, zMax = 0; bool hasRange = false;
                jr["preview"] = zmapToBase64(zm, &zMin, &zMax, &hasRange);
                if (hasRange) { jr["zMin"] = zMin; jr["zMax"] = zMax; }
                jr["xResMm"] = zm.xResMm;
                jr["yResMm"] = zm.yResMm;
            }
        }

        results.push_back(jr);
        conn.send_text(jr.dump());
    }

    const double totalMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pipeStart).count();
    conn.send_text(json{
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

// ── Main ─────────────────────────────────────────────────────────────────

int main() {
    std::cout << "[VisionEngine] Starting on ws://localhost:9000\n";

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
                    auto done = runPipeline(msg, conn);
                    conn.send_text(done.dump());
                    return;
                }
                conn.send_text(json{{"event","error"},{"msg","unknown cmd: " + cmd}}.dump());
            } catch (const std::exception& e) {
                conn.send_text(json{{"event","error"},{"msg", e.what()}}.dump());
            }
        });

    app.port(9000).multithreaded().run();
    return 0;
}
