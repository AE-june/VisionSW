#include "ToolFactory.h"
#include "JsonBridge.h"
#include "ImageEncoder.h"
#include "Logger.h"
#include "LineFitHeightMeasure.h"
#include "ThicknessMeasure.h"
#include "PlaneFitTool.h"
#include "HeightFromPlaneTool.h"
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

// ── Pipeline execution ───────────────────────────────────────────────────

static json runPipeline(const json& msg, crow::websocket::connection& conn) {
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

    // Node outputs cache
    std::unordered_map<std::string, VisionDataPtr> outputs;

    bool pipelinePass = true;
    json results = json::array();

    // Send start event
    conn.send_text(json{{"event","start"}}.dump());

    for (const auto& nodeId : order) {
        if (nodeIdx.find(nodeId) == nodeIdx.end()) continue;
        const auto& ns = nodeSpecs[nodeIdx.at(nodeId)];

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
                if (o->zmap  && !merged->zmap)  merged->zmap  = o->zmap;
                if (o->image && !merged->image) merged->image = o->image;
                if (o->cloud && !merged->cloud) merged->cloud = o->cloud;
                if (o->plane && !merged->plane) merged->plane = o->plane;
                if (merged->sourceId.empty()) merged->sourceId = o->sourceId;
            }
            if (any) inputData = merged;
        }

        auto result = tool->execute(inputData);
        if (result.output) outputs[nodeId] = result.output;

        bool ok = (result.status == ToolStatus::Ok);
        if (!ok) pipelinePass = false;

        // Build per-tool result
        json jr;
        jr["event"] = "result";
        jr["id"]    = nodeId;
        jr["tool"]  = ns.type;
        jr["ok"]    = ok;
        jr["msg"]   = result.message;

        // Attach measurements for known tool types
        if (ns.type == "LineFitHeight") {
            auto* m = dynamic_cast<LineFitHeightMeasure*>(tool.get());
            if (m && m->lastResult().valid) {
                const auto& r = m->lastResult();
                jr["heightDiff"] = r.heightDiff;
                jr["Qz"]         = r.Qz;
                jr["refZatQ"]    = r.refZatQ;
                jr["slope"]      = r.slope;
                jr["intercept"]  = r.intercept;
            }
        }
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
                if (!r.allPass) pipelinePass = false;
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

        // Attach image preview (base64 PNG)
        if (result.output) {
            if (result.output->hasImage())
                jr["preview"] = imageToBase64(*result.output->image);
            else if (result.output->hasZMap()) {
                const auto& zm = *result.output->zmap;
                jr["preview"] = zmapToBase64(zm);
                // 실제 z(raw count) 범위 — 프론트 컬러맵 range를 실제값 단위로 표시
                float zMin = std::numeric_limits<float>::max();
                float zMax = -std::numeric_limits<float>::max();
                for (float v : zm.data)
                    if (!std::isnan(v)) { zMin = std::min(zMin, v); zMax = std::max(zMax, v); }
                if (zMin <= zMax) { jr["zMin"] = zMin; jr["zMax"] = zMax; }
            }
        }

        results.push_back(jr);
        conn.send_text(jr.dump());
    }

    json done;
    done["event"]   = "done";
    done["pass"]    = pipelinePass;
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
