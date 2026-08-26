#include "ToolFactory.h"
#include "HeightMapCache.h"
#include "ExposureMergeCore.h"
#include "JsonBridge.h"
#include "ImageEncoder.h"
#include "Logger.h"
#include "PlaneFitTool.h"
#include "HeightFromPlaneTool.h"
#include "CsvWriterTool.h"
#include "LineCenterTool.h"
#include "AlignTool.h"
#include "RegionMeasureTool.h"
#include "BroadcastRun.h"
#include "Broadcast.h"
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
#include <limits>
#include <algorithm>
#include <future>
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

struct Edge {
    std::string source, target;
    int sourcePort = 0;   // sourceHandle "output-N"의 N (없으면 0)
    int targetPort = 0;   // targetHandle "input-N"의 N (없으면 0)
};

// "output-3" / "input-2" 같은 핸들 문자열에서 마지막 '-' 뒤 정수를 뽑는다.
// 형식이 안 맞으면 0으로 폴백 → 기존 동작 보존.
static int parsePortHandle(const std::string& handle) {
    auto pos = handle.rfind('-');
    if (pos == std::string::npos || pos + 1 >= handle.size()) return 0;
    int v = 0;
    for (std::size_t i = pos + 1; i < handle.size(); ++i) {
        char ch = handle[i];
        if (ch < '0' || ch > '9') return 0;
        v = v * 10 + (ch - '0');
    }
    return v;
}

// 입력 엣지 참조 — 출처 노드 + 포트 인덱스
struct InputRef {
    std::string source;
    int srcPort = 0;
    int dstPort = 0;
};


// ── Notch envelope 캐시 (NotchMeasureV2 시각화용 — nodeId → chunk envelope Profile[]) ──
static std::unordered_map<std::string, std::vector<std::shared_ptr<vision::Profile>>> g_notchEnvCache;
static std::mutex g_notchEnvCacheMtx;

// ── 노드 결과 캐시 ─────────────────────────────────────────────────────────
//  개별 노드 실행 시, 파라미터가 바뀌지 않은 상류 노드는 재실행하지 않고 캐시 재사용.
//  (단일 사용자 가정 — 접근은 g_cacheMtx로 직렬화)
struct CachedNode { VisionDataPtr output; std::size_t paramHash; };
static std::unordered_map<std::string, CachedNode> g_nodeCache;
static std::mutex g_cacheMtx;

// 프로파일 온디맨드 조회용 캐시 (run → fetchProfile)
static std::unordered_map<std::string, std::vector<std::shared_ptr<Profile>>> g_profileCache;
static std::mutex g_profileCacheMtx; // 병렬 노드 쓰기 + fetchProfile 읽기 동기화

// ── Pipeline execution ───────────────────────────────────────────────────

static json runPipeline(const json& msg, crow::websocket::connection* conn) {
    // conn==nullptr 이면 헤드리스 실행 — 이벤트 전송/프리뷰 인코딩 생략, 결과 json만 반환.
    auto emit = [&](const std::string& s){ if (conn) conn->send_text(s); };

    // schemaVersion 검사 — 구 스키마 레시피를 조용히 로드하지 않는다 (ARCH §1)
    if (!msg.contains("schemaVersion") || msg["schemaVersion"].get<int>() < 2) {
        const std::string ver = msg.contains("schemaVersion")
            ? msg["schemaVersion"].dump() : "없음";
        const std::string errMsg =
            "이 레시피는 구 스키마입니다. 마이그레이션이 필요합니다. "
            "(schemaVersion=" + ver + ", 필요=2)";
        emit(json{{"event","error"},{"message",errMsg}}.dump());
        std::cerr << "recipe rejected: " << errMsg << "\n";
        return json{{"ok",false},{"error",errMsg}};
    }

    const bool useCache = msg.value("useCache", false);
    // 배치 검사 등 화면 표시가 필요 없을 때 미리보기(PNG 인코딩+z스캔) 생략 → 대폭 가속
    const bool noPreview = msg.value("noPreview", false);
    // 개별 노드 실행 시 이 노드는 캐시 무시하고 항상 재실행 (상류만 캐시 재사용)
    const std::string forceNode = msg.value("forceNode", std::string());
    // 배치(폴더검사) 모드 — 이번 실행에 쓴 HeightMapLoader 원본 파일을 끝나고 캐시에서 즉시 비움
    // (여러 이미지를 순회하는 워커 프로세스가 g_heightmapFileCache를 무한정 쌓아두지 않도록)
    const bool batchMode = msg.value("batch", false);
    std::lock_guard<std::mutex> cacheLock(g_cacheMtx);

    // 실행 1회분 프레임 레지스트리 — 모든 노드가 공유
    auto runFrames = std::make_shared<FrameRegistry>();
    runFrames->define(Frame{frames::kWorld, "", Transform2D::identity()});

    // Parse nodes
    struct NodeSpec {
        std::string id, type;
        json params;
        std::vector<vision::PortMeta> inputPorts;
    };
    std::vector<NodeSpec> nodeSpecs;
    std::unordered_map<std::string, int> nodeIdx;
    std::vector<std::string> heightmapPathsUsed;

    for (const auto& n : msg.at("nodes")) {
        NodeSpec ns;
        ns.id     = n.at("id").get<std::string>();
        ns.type   = n.at("type").get<std::string>();
        ns.params = n.value("params", json::object());
        if (n.contains("inputPorts") && n.at("inputPorts").is_array()) {
            for (const auto& pm : n.at("inputPorts")) {
                vision::PortMeta m;
                m.type    = pm.value("type", std::string());
                m.isArray = pm.value("isArray", false);
                ns.inputPorts.push_back(m);
            }
        }
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
            Edge ed;
            ed.source = e.at("source").get<std::string>();
            ed.target = e.at("target").get<std::string>();
            // UI가 sourceHandle="output-N", targetHandle="input-N" 형식을 보낸다.
            // 없거나 형식이 안 맞으면 0 폴백 → 기존 병합과 동일.
            ed.sourcePort = parsePortHandle(e.value("sourceHandle", std::string()));
            ed.targetPort = parsePortHandle(e.value("targetHandle", std::string()));
            edges.push_back(std::move(ed));
        }
    }

    // Which nodes' outputs feed each node (다중 입력 지원)
    // 포트 정보 포함 — 파싱 순서(=엣지 순서) 보존 (제약 C-2: "먼저 온 것이 이김")
    std::unordered_map<std::string, std::vector<InputRef>> inputsFrom;
    for (const auto& e : edges)
        inputsFrom[e.target].push_back(InputRef{e.source, e.sourcePort, e.targetPort});

    std::vector<std::string> ids;
    for (const auto& ns : nodeSpecs) ids.push_back(ns.id);

    bool pipelinePass = true;
    json results = json::array();

    const auto pipeStart = std::chrono::steady_clock::now();
    emit(json{{"event","start"}}.dump());

    // ── 데이터플로우(actor) 모델 ─────────────────────────────────────────────────
    // 각 노드를 독립 스레드로 실행. 상류 future가 준비된 즉시 시작 — 레벨 배리어 없음.
    // g_cacheMtx는 파이프라인 전체를 직렬화(동시 2개 파이프라인 방지), 별도 유지.
    // 파이프라인 내 공유 상태(emit/frames/results/nodeCache)는 아래 로컬 뮤텍스로 보호.
    struct NodeResult { VisionDataPtr data; bool dirty = true; };

    std::unordered_map<std::string, std::promise<NodeResult>>      nodePromises;
    std::unordered_map<std::string, std::shared_future<NodeResult>> nodeFutures;
    for (const auto& nodeId : ids) {
        nodePromises.emplace(nodeId, std::promise<NodeResult>{});
        nodeFutures[nodeId] = nodePromises.at(nodeId).get_future().share();
    }

    std::mutex emitMtx, framesMtx, resultsMtx, nodeCacheMtx;
    std::atomic<bool> pipelinePassA{true};

    auto safeEmit = [&](const std::string& s) {
        std::lock_guard<std::mutex> lk(emitMtx);
        emit(s);
    };

    std::vector<std::future<void>> nodeTasks;
    for (const auto& nodeId : ids) {
        if (nodeIdx.find(nodeId) == nodeIdx.end()) continue;
        nodeTasks.push_back(std::async(std::launch::async, [&, nodeId]() {
            const int nsI        = nodeIdx.at(nodeId);
            const auto& ns       = nodeSpecs[nsI];
            const std::size_t ph = std::hash<std::string>{}(ns.params.dump());

            // 1. 상류 입력 대기 + dirty 집계
            bool upstreamDirty = false;
            std::vector<std::pair<InputRef, VisionDataPtr>> upstreamResults;
            if (inputsFrom.count(nodeId)) {
                std::vector<InputRef> ordered = inputsFrom.at(nodeId);
                std::stable_sort(ordered.begin(), ordered.end(),
                    [](const InputRef& a, const InputRef& b){ return a.dstPort < b.dstPort; });
                for (const auto& in : ordered) {
                    auto nr = nodeFutures.at(in.source).get();
                    if (nr.dirty) upstreamDirty = true;
                    upstreamResults.push_back({in, nr.data});
                }
            }

            // 2. 캐시 체크
            if (useCache && !upstreamDirty && nodeId != forceNode) {
                std::lock_guard<std::mutex> lk(nodeCacheMtx);
                auto cit = g_nodeCache.find(nodeId);
                if (cit != g_nodeCache.end() && cit->second.paramHash == ph && cit->second.output) {
                    for (const auto& f : cit->second.output->definedFrames) {
                        std::lock_guard<std::mutex> flk(framesMtx);
                        runFrames->define(f);
                    }
                    nodePromises.at(nodeId).set_value({cit->second.output, false});
                    return;
                }
            }

            safeEmit(json{{"event","log"},{"level","info"},
                          {"msg","Running " + ns.type + " [" + nodeId + "]"}}.dump());

            // 3. 포트별 입력 라우팅 (Phase 2: 병합 대신 inputs[dstPort]에 상류 출력을 그대로 넣음)
            VisionDataPtr inputData;
            if (!upstreamResults.empty()) {
                auto merged = std::make_shared<VisionData>();
                merged->frames = runFrames;
                bool any = false;
                for (const auto& [in, o] : upstreamResults) {
                    if (!o) continue;
                    VISION_LOG_INFO("[pipeline] edge {}:{} -> {}:{}", in.source, in.srcPort, nodeId, in.dstPort);
                    any = true;
                    const std::size_t dstPort = static_cast<std::size_t>(std::max(0, in.dstPort));
                    if (dstPort >= merged->inputs.size()) merged->inputs.resize(dstPort + 1);
                    // srcPort 라우팅: 생산자의 출력 포트 s(>0)가 heightmaps[s]를 가리키면,
                    //  소비자가 inHeightMap(port,0)으로 그 출력을 읽도록 heightmaps[s]를 [0]으로 노출.
                    //  (예: ExposureMerge3 포트1 = intensity → HeightMapSaver로 저장)
                    std::shared_ptr<VisionData> routed = o;
                    const std::size_t s = static_cast<std::size_t>(std::max(0, in.srcPort));
                    if (s > 0 && s < o->heightmaps.size()) {
                        auto copy = std::make_shared<VisionData>(*o);
                        copy->heightmaps.clear();
                        copy->heightmaps.push_back(o->heightmaps[s]);          // 선택 출력 → [0]
                        for (std::size_t k = 0; k < o->heightmaps.size(); ++k) // 나머지는 뒤에 유지
                            if (k != s) copy->heightmaps.push_back(o->heightmaps[k]);
                        routed = copy;
                    }
                    if (!merged->inputs[dstPort]) {
                        merged->inputs[dstPort] = routed;
                    } else {
                        VISION_LOG_WARN("[pipeline] {} port{} 충돌 — 내용 병합", nodeId, dstPort);
                        auto combined = std::make_shared<VisionData>(*merged->inputs[dstPort]);
                        for (auto& hm : routed->heightmaps) combined->heightmaps.push_back(hm);
                        for (auto& rg : routed->regions)    combined->regions.push_back(rg);
                        for (auto& pl : routed->planes)     combined->planes.push_back(pl);
                        for (auto& ln : routed->lines)      combined->lines.push_back(ln);
                        merged->inputs[dstPort] = combined;
                    }
                    if (merged->sourceId.empty()) merged->sourceId = o->sourceId;
                }
                if (any) inputData = merged;
            }

            // 4. T0-1 P2: 소비자 프레임 불일치 검사 (경고만) — 포트 0 HeightMap vs 나머지 포트
            if (inputData) {
                auto hm0 = inputData->inHeightMap(0);
                if (hm0 && !hm0->frameId.empty()) {
                    const std::string& hf = hm0->frameId;
                    for (std::size_t pi = 1; pi < inputData->inputs.size(); ++pi) {
                        auto inp = inputData->in(pi);
                        if (!inp) continue;
                        for (const auto& rp : inp->regions)
                            if (rp && !rp->frameId.empty() && rp->frameId != hf)
                                VISION_LOG_WARN("[frame] {} [{}]: port{} Region frame '{}' != HeightMap frame '{}'",
                                                ns.type, nodeId, pi, rp->frameId, hf);
                        for (const auto& pp : inp->planes)
                            if (pp && !pp->frameId.empty() && pp->frameId != hf)
                                VISION_LOG_WARN("[frame] {} [{}]: port{} Plane frame '{}' != HeightMap frame '{}'",
                                                ns.type, nodeId, pi, pp->frameId, hf);
                    }
                }
            }

            // 5. 툴 생성 — 프레임 생성 노드(SurfaceCrop 등)가 고유 frameId를 만들 수 있도록 nodeId 주입
            auto toolParams = ns.params;
            toolParams["_nodeId"] = nodeId;
            auto tool = ToolFactory::create(ns.type, toolParams, noPreview);
            if (!tool) {
                safeEmit(json{{"event","log"},{"level","error"},
                              {"msg","Unknown tool type: " + ns.type}}.dump());
                pipelinePassA = false;
                nodePromises.at(nodeId).set_value({nullptr, true});
                return;
            }

            // 6. 실행 — 브로드캐스트: 스칼라선언 포트가 배열 받으면 원소별 N회 (설계 §4.4)
            const auto t0 = std::chrono::steady_clock::now();
            ToolResult result;
            std::vector<std::size_t> axisLens =
                inputData ? vision::broadcastAxisLengths(*inputData, ns.inputPorts)
                          : std::vector<std::size_t>{};
            vision::BroadcastPlan plan = vision::computeBroadcast(axisLens);
            if (!plan.ok) {
                result = { ToolStatus::Fail,
                           "브로드캐스트 배열 길이 불일치 (" + ns.type + ")" };
            } else if (plan.count <= 1) {
                result = tool->execute(inputData);            // 기존 경로 (회귀 0)
            } else {
                // N>1: 원소별 실행 후 생산 벡터를 인덱스 순서로 concat
                auto agg = std::make_shared<VisionData>();
                bool anyOk = false;
                for (std::size_t i = 0; i < plan.count; ++i) {
                    auto slice = vision::sliceBroadcastInput(*inputData, i, ns.inputPorts);
                    auto r = tool->execute(slice);
                    if (r.status != ToolStatus::Ok) {
                        result = { ToolStatus::Fail,
                                   r.message.empty() ? ("브로드캐스트 원소 실패 idx="
                                       + std::to_string(i)) : r.message };
                        agg.reset();
                        break;
                    }
                    if (r.output) {
                        anyOk = true;
                        if (agg->sourceId.empty()) agg->sourceId = r.output->sourceId;
                        if (!agg->frames) agg->frames = r.output->frames;
                        auto& o = *r.output;
                        for (auto& e : o.heightmaps)    agg->heightmaps.push_back(e);
                        for (auto& e : o.clouds)        agg->clouds.push_back(e);
                        for (auto& e : o.regions)       agg->regions.push_back(e);
                        for (auto& e : o.planes)        agg->planes.push_back(e);
                        for (auto& e : o.lines)         agg->lines.push_back(e);
                        for (auto& e : o.geometries)    agg->geometries.push_back(e);
                        for (auto& e : o.profiles)      agg->profiles.push_back(e);
                        for (auto& e : o.points)        agg->points.push_back(e);
                        for (auto& e : o.measurements)  agg->measurements.push_back(e);
                        for (auto& e : o.decisions)     agg->decisions.push_back(e);
                        for (auto& e : o.overlays)      agg->overlays.push_back(e);
                        for (auto& f : o.definedFrames) agg->definedFrames.push_back(f);
                    }
                }
                if (agg && anyOk) result = { ToolStatus::Ok, "", agg };
                else if (result.status != ToolStatus::Fail)
                    result = { ToolStatus::Ok, "", agg };   // N>1 이지만 전부 빈 출력
            }
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                                         std::chrono::steady_clock::now() - t0).count();
            VISION_LOG_INFO("[pipeline] {} [{:.1f} ms]{}", ns.type, elapsedMs,
                            plan.count > 1 ? (" x" + std::to_string(plan.count)) : "");

            // 7. 출력·캐시·프레임 전파
            if (result.output) {
                {
                    std::lock_guard<std::mutex> lk(nodeCacheMtx);
                    g_nodeCache[nodeId] = {result.output, ph};
                }
                auto& out = *result.output;
                auto inHm0 = inputData ? inputData->inHeightMap(0) : std::shared_ptr<HeightMap>{};
                if (ns.type == "HeightMapLoader" && !out.heightmaps.empty() && out.heightmaps[0]) {
                    const std::string fid = "hm:" + nodeId;
                    out.heightmaps[0]->frameId = fid;
                    const Frame f{fid, frames::kWorld, Transform2D::identity()};
                    {
                        std::lock_guard<std::mutex> flk(framesMtx);
                        runFrames->define(f);
                    }
                    out.definedFrames.push_back(f);
                } else if (inHm0 && !inHm0->frameId.empty()) {
                    const std::string& inFid = inHm0->frameId;
                    for (auto& hm : out.heightmaps) if (hm && hm->frameId.empty()) hm->frameId = inFid;
                    for (auto& rp : out.regions)    if (rp && rp->frameId.empty()) rp->frameId = inFid;
                    for (auto& pp : out.planes)     if (pp && pp->frameId.empty()) pp->frameId = inFid;
                    for (auto& c  : out.clouds)     if (c  && c->frameId.empty())  c->frameId  = inFid;
                }
            }

            // 8. promise 이행 → 하류 노드 즉시 깨어남
            nodePromises.at(nodeId).set_value({result.output, true});

            bool ok = (result.status == ToolStatus::Ok);
            if (!ok) pipelinePassA = false;

            // 9. 결과 JSON 빌드 + emit
            auto dispHm = (result.output && result.output->heightmap0()) ? result.output->heightmap0()
                        : (inputData && inputData->inHeightMap(0)) ? inputData->inHeightMap(0)
                        : std::shared_ptr<HeightMap>{};
            const HeightMap* dispZ = dispHm ? dispHm.get() : nullptr;

            json jr;
            jr["event"]     = "result";
            jr["id"]        = nodeId;
            jr["tool"]      = ns.type;
            jr["ok"]        = ok;
            jr["msg"]       = result.message;
            jr["elapsedMs"] = elapsedMs;

            if (dispZ) {
                jr["imgW"]      = dispZ->width;
                jr["imgH"]      = dispZ->height;
                jr["originCol"] = dispZ->originCol;
                jr["originRow"] = dispZ->originRow;
                jr["xResMm"]    = dispZ->xResMm;   // Region 출력 툴도 mm 환산 가능하도록 항상 제공
                jr["yResMm"]    = dispZ->yResMm;
            }

            // ── Generic: measurements ───────────────────────────────────────
            if (result.output && !result.output->measurements.empty()) {
                json meas = json::array();
                for (const auto& m : result.output->measurements)
                    meas.push_back({{"name",m.name},{"value",m.value},{"unit",m.unit},{"valid",m.valid}});
                jr["measurements"] = meas;
            }
            // ── Generic: decisions (+ allPass → pipelinePassA) ─────────────
            if (result.output && !result.output->decisions.empty()) {
                json decs = json::array();
                for (const auto& d : result.output->decisions) {
                    decs.push_back({{"name",d.name},{"pass",d.pass},{"reason",d.reason},
                                    {"measured",d.measured},{"nominal",d.nominal},{"tolerance",d.tolerance}});
                    if (d.name == "allPass" && !d.pass) pipelinePassA = false;
                }
                jr["decisions"] = decs;
            }
            // ── Generic: overlays (Cloud → jr["cloud"], Lines → jr["lines"]) ─
            if (result.output) {
                for (const auto& ov : result.output->overlays) {
                    if (ov.kind == Overlay::Kind::Cloud && !ov.cloudPoints.empty()) {
                        json pts = json::array();
                        for (const auto& p : ov.cloudPoints) pts.push_back({p[0], p[1], p[2]});
                        jr["cloud"] = pts;
                    }
                    if (ov.kind == Overlay::Kind::Lines && !ov.lines.empty()) {
                        json arr = json::array();
                        for (const auto& l : ov.lines)
                            arr.push_back({{"cx",l.cx},{"cy",l.cy},{"cxMm",l.cxMm},{"cyMm",l.cyMm},
                                           {"angleDeg",l.angleDeg},{"roiIndex",l.roiIndex},{"pointCount",l.pointCount},
                                           {"p0x",l.p0x},{"p0y",l.p0y},{"p1x",l.p1x},{"p1y",l.p1y}});
                        jr["lines"] = arr;
                    }
                }
            }
            // ── PointCloud3D output (HeightMapToCloud, ExposureMergeCloud 등) ─
            if (result.output && result.output->cloud0() && !result.output->cloud0()->empty()) {
                const auto& cpts = result.output->cloud0()->points;
                const size_t cap = 50000;
                const size_t stride = cpts.size() > cap ? (cpts.size() + cap - 1) / cap : 1;
                json pts = json::array();
                for (size_t i = 0; i < cpts.size(); i += stride)
                    pts.push_back({cpts[i].x, cpts[i].y, cpts[i].z});
                jr["cloud"] = pts;
                jr["cloudTotal"] = static_cast<long long>(cpts.size());
            }
            // ── Profile[] output (CloudToProfiles, ExtractProfile 등) ─ 메타만 전송, x/z는 fetchProfile로
            if (result.output && !result.output->profiles.empty()) {
                const auto& profs = result.output->profiles;
                // __notchenv_ 접두사 profile → notchEnvCache 분리 (NotchMeasureV2 시각화)
                std::vector<std::shared_ptr<Profile>> regularProfs, envProfs;
                for (const auto& p : profs) {
                    if (p->label.rfind("__notchenv_", 0) == 0)
                        envProfs.push_back(p);
                    else
                        regularProfs.push_back(p);
                }
                if (!envProfs.empty()) {
                    std::lock_guard<std::mutex> lk(g_notchEnvCacheMtx);
                    g_notchEnvCache[nodeId] = envProfs;
                    jr["notchChunkCount"] = static_cast<long long>(envProfs.size());
                }

                if (!regularProfs.empty()) {
                    jr["profileCount"] = static_cast<long long>(regularProfs.size());
                    // 캐시에 저장 → fetchProfile 온디맨드 지원
                    {
                        std::lock_guard<std::mutex> lk(g_profileCacheMtx);
                        g_profileCache[nodeId] = regularProfs;
                    }
                    // 메타만 전송 — x/z는 fetchProfile로
                    json meta = json::array();
                    for (const auto& pr : regularProfs)
                        meta.push_back({{"label", pr->label}, {"n", (long long)pr->size()}});
                    jr["profileMeta"] = meta;
                }
            }

            if (!noPreview) {
                if (result.output && result.output->hasRegion()) {
                    const auto& rgn = *result.output->region0();
                    jr["preview"] = regionToBase64(rgn);
                    jr["zMin"] = 0.f; jr["zMax"] = 255.f;
                    jr["imgW"] = rgn.width; jr["imgH"] = rgn.height;
                }
                else if (dispZ) {
                    float zMin = 0, zMax = 0; bool hasRange = false;
                    jr["preview"] = heightmapToBase64(*dispZ, &zMin, &zMax, &hasRange);
                    if (hasRange) { jr["zMin"] = zMin; jr["zMax"] = zMax; }
                    jr["xResMm"] = dispZ->xResMm;
                    jr["yResMm"] = dispZ->yResMm;
                }
            }

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

            {
                std::lock_guard<std::mutex> lk(resultsMtx);
                results.push_back(jr);
            }
            safeEmit(jr.dump());
        }));
    }

    for (auto& f : nodeTasks) f.get();
    pipelinePass = pipelinePassA.load();

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
//   레시피를 폴더의 모든 HeightMap에 적용해 RegionMeasure 영역별 높이/PlaneFit 파라미터를
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

    // schemaVersion 검사 — 구 스키마 레시피를 조용히 로드하지 않는다 (ARCH §1)
    if (!recipe.contains("schemaVersion") || recipe["schemaVersion"].get<int>() < 2) {
        const std::string ver = recipe.contains("schemaVersion")
            ? recipe["schemaVersion"].dump() : "없음";
        std::cerr << "recipe rejected: 이 레시피는 구 스키마입니다. 마이그레이션이 필요합니다. "
                     "(schemaVersion=" << ver << ", 필요=2)\n";
        return 1;
    }

    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(folder)) {
        auto ext = e.path().extension().string();
        if (ext == ".png" || ext == ".PNG") files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::cerr << "no png in " << folder << "\n"; return 1; }
    std::cout << "[repeat-analyze] recipe=" << recipePath << " files=" << files.size() << "\n";

    // 파이프라인 실행 중 다음 이미지를 미리 로딩하기 위한 해상도 파라미터
    float pfX = 1.f, pfY = 1.f, pfZ = 0.001f;
    for (const auto& n : recipe["nodes"])
        if (n.value("type", "") == "HeightMapLoader") {
            pfX = n["params"].value("xResMm", 1.f);
            pfY = n["params"].value("yResMm", 1.f);
            pfZ = n["params"].value("zResMm", 0.001f);
            break;
        }

    std::ofstream csv(outCsv);
    std::vector<std::vector<double>> dist, npts;   // [region][sample]
    std::vector<double> planeA, planeB, planeC, rmseV, tiltV;
    bool header = false;

    for (size_t fi = 0; fi < files.size(); ++fi) {
        // 다음 파일을 백그라운드에서 미리 로딩 — 현재 파일 compute(~1600ms)에 I/O(~800ms) 숨김
        if (fi + 1 < files.size()) {
            std::string nxt = files[fi + 1];
            std::thread([nxt, pfX, pfY, pfZ]() {
                {
                    std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
                    if (g_heightmapFileCache.count(nxt)) return;
                }
                auto hm = loadHeightMapFromFile(nxt, pfX, pfY, pfZ);
                if (!hm) return;
                std::lock_guard<std::mutex> lk(g_heightmapFileCacheMtx);
                heightmapCachePut(nxt, hm);
            }).detach();
        }

        json msg = recipe;
        msg["cmd"] = "run"; msg["noPreview"] = true; msg["useCache"] = false; msg["batch"] = true;
        for (auto& n : msg["nodes"])
            if (n.value("type", "") == "HeightMapLoader") { n["params"]["path"] = files[fi]; n["params"]["mode"] = "file"; }
        json done = runPipeline(msg, nullptr);

        std::vector<double> d, np;
        for (const auto& r : done["results"]) {
            if (r.value("tool", "") == "RegionMeasure" && r.contains("measurements")) {
                for (const auto& m : r["measurements"]) {
                    const std::string name = m.value("name", "");
                    const double val = m.value("value", 0.0);
                    const bool valid = m.value("valid", false);
                    if (name == "zMm" && valid)
                        d.push_back(val);
                    else if (name == "areaPx")
                        np.push_back(val);
                }
            }
            if (r.value("tool", "") == "PlaneFit" && r.contains("measurements")) {
                for (const auto& m : r["measurements"]) {
                    const std::string name = m.value("name", "");
                    const double val = m.value("value", 0.0);
                    if      (name == "planeA")  planeA.push_back(val);
                    else if (name == "planeB")  planeB.push_back(val);
                    else if (name == "planeC")  planeC.push_back(val);
                    else if (name == "rmse")    rmseV.push_back(val);
                    else if (name == "tiltDeg") tiltV.push_back(val);
                }
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
                if (cmd == "fetchProfile") {
                    std::string nid = msg.value("nodeId", "");
                    int idx = msg.value("profileIdx", 0);
                    std::lock_guard<std::mutex> lk(g_profileCacheMtx);
                    auto it = g_profileCache.find(nid);
                    if (it == g_profileCache.end() || idx < 0 || idx >= (int)it->second.size()) {
                        conn.send_text(json{{"event","profileData"},{"nodeId",nid},{"profileIdx",idx},{"error","not found"}}.dump());
                        return;
                    }
                    const auto& pr = *it->second[idx];
                    const size_t cap = 600;
                    const size_t ss = pr.size() > cap ? (pr.size() + cap - 1) / cap : 1;
                    json xs = json::array(), zs = json::array();
                    for (size_t i = 0; i < pr.size(); i += ss) {
                        xs.push_back(pr.x[i]);
                        zs.push_back(std::isnan(pr.z[i]) ? json(nullptr) : json(pr.z[i]));
                    }
                    conn.send_text(json{
                        {"event","profileData"},{"nodeId",nid},{"profileIdx",idx},
                        {"label",pr.label},{"n",(long long)pr.size()},{"x",xs},{"z",zs}
                    }.dump());
                    return;
                }
                if (cmd == "fetchNotchEnv") {
                    std::string nid = msg.value("nodeId", "");
                    int idx = msg.value("chunkIdx", 0);
                    std::lock_guard<std::mutex> lk(g_notchEnvCacheMtx);
                    auto it = g_notchEnvCache.find(nid);
                    if (it == g_notchEnvCache.end() || idx < 0 || idx >= (int)it->second.size()) {
                        conn.send_text(json{{"event","notchEnvData"},{"nodeId",nid},{"chunkIdx",idx},{"error","not found"}}.dump());
                        return;
                    }
                    const auto& pr = *it->second[idx];
                    json xs = json::array(), zs = json::array(), yFit = json::array();
                    for (size_t i = 0; i < pr.x.size(); ++i) {
                        xs.push_back(pr.x[i]);
                        zs.push_back(pr.z[i]);
                        yFit.push_back(pr.y.size() > i ? json(pr.y[i]) : json(nullptr));
                    }
                    double c0           = pr.s.size() > 0 ? pr.s[0] : 0.0;
                    double c1           = pr.s.size() > 1 ? pr.s[1] : 0.0;
                    double c2           = pr.s.size() > 2 ? pr.s[2] : 0.0;
                    double c3           = pr.s.size() > 3 ? pr.s[3] : 0.0;
                    double notchLoY     = pr.s.size() > 4 ? pr.s[4] : 0.0;
                    double notchHiY     = pr.s.size() > 5 ? pr.s[5] : 0.0;
                    double floorCenterY = pr.s.size() > 6 ? pr.s[6] : 0.0;
                    double floorZRelUm  = pr.s.size() > 7 ? pr.s[7] : 0.0;
                    double leftLandZmm  = pr.s.size() > 8  ? pr.s[8]  : std::numeric_limits<double>::quiet_NaN();
                    double rightLandZmm = pr.s.size() > 9  ? pr.s[9]  : std::numeric_limits<double>::quiet_NaN();
                    double leftEdgeZmm  = pr.s.size() > 10 ? pr.s[10] : std::numeric_limits<double>::quiet_NaN();
                    double rightEdgeZmm = pr.s.size() > 11 ? pr.s[11] : std::numeric_limits<double>::quiet_NaN();
                    double leftEdgeYmm  = pr.s.size() > 12 ? pr.s[12] : std::numeric_limits<double>::quiet_NaN();
                    double rightEdgeYmm = pr.s.size() > 13 ? pr.s[13] : std::numeric_limits<double>::quiet_NaN();
                    double polyAtFloor = c0 + c1*floorCenterY + c2*floorCenterY*floorCenterY + c3*floorCenterY*floorCenterY*floorCenterY;
                    double floorZmm    = polyAtFloor + floorZRelUm / 1000.0;
                    json resp = {
                        {"event","notchEnvData"},{"nodeId",nid},{"chunkIdx",idx},
                        {"x",xs},{"z",zs},{"yFit",yFit},
                        {"notchLoY",notchLoY},{"notchHiY",notchHiY},
                        {"floorCenterY",floorCenterY},{"floorZRelUm",floorZRelUm},
                        {"floorZmm",floorZmm}
                    };
                    if (!std::isnan(leftLandZmm))  resp["leftLandZmm"]  = leftLandZmm;
                    if (!std::isnan(rightLandZmm)) resp["rightLandZmm"] = rightLandZmm;
                    if (!std::isnan(leftEdgeZmm))  resp["leftEdgeZmm"]  = leftEdgeZmm;
                    if (!std::isnan(rightEdgeZmm)) resp["rightEdgeZmm"] = rightEdgeZmm;
                    if (!std::isnan(leftEdgeYmm))  resp["leftEdgeYmm"]  = leftEdgeYmm;
                    if (!std::isnan(rightEdgeYmm)) resp["rightEdgeYmm"] = rightEdgeYmm;
                    conn.send_text(resp.dump());
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
