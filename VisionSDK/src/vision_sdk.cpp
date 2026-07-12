/* VisionSDK 구현 — ToolFactory::create + execute 를 C ABI로 래핑.
 * 모든 노드가 ToolFactory를 통해 생성되므로 팩토리가 아는 모든 타입이 자동 노출된다. */
/* VISIONSDK_EXPORTS 는 CMake(target_compile_definitions)에서 정의됨 */
#include "vision_sdk.h"

#include "ToolFactory.h"
#include "VisionData.h"
#include "ZMap.h"
#include <nlohmann/json.hpp>

#include <cstring>
#include <cstdlib>
#include <string>
#include <memory>
#include <exception>

using namespace vision;
using json = nlohmann::json;

static void setMsg(VsdkResult* r, const std::string& m) {
    std::strncpy(r->msg, m.c_str(), sizeof(r->msg) - 1);
    r->msg[sizeof(r->msg) - 1] = '\0';
}

// VsdkZMap(+선택 평면) → VisionData 입력 (복사).
static VisionDataPtr makeInput(const VsdkZMap* z, const VsdkPlane* p) {
    if ((!z || !z->data) && (!p || !p->valid)) return nullptr;
    auto d = std::make_shared<VisionData>();
    if (z && z->data) {
        auto zm = std::make_shared<ZMap>();
        zm->width = z->width;         zm->height = z->height;
        zm->xResMm = z->xResMm;       zm->yResMm = z->yResMm;   zm->zResMm = z->zResMm;
        zm->zZeroCount = z->zZeroCount; zm->originCol = z->originCol; zm->originRow = z->originRow;
        zm->data.assign(z->data, z->data + (size_t)z->width * z->height);
        d->zmap = zm;
    }
    if (p && p->valid)
        d->plane = std::make_shared<PlaneModel>(PlaneModel{ p->a, p->b, p->c, true });
    return d;
}

// VisionData 출력 → VsdkResult 페이로드 (SDK가 malloc 소유).
static void marshalOut(const VisionDataPtr& o, VsdkResult* r) {
    if (!o) return;
    if (o->zmap) {
        const auto& z = *o->zmap;
        const size_t n = (size_t)z.width * z.height;
        r->zmap.width = z.width;       r->zmap.height = z.height;
        r->zmap.xResMm = z.xResMm;     r->zmap.yResMm = z.yResMm; r->zmap.zResMm = z.zResMm;
        r->zmap.zZeroCount = z.zZeroCount; r->zmap.originCol = z.originCol; r->zmap.originRow = z.originRow;
        r->zmap.data = (float*)std::malloc(n * sizeof(float));
        if (r->zmap.data) std::memcpy(r->zmap.data, z.data.data(), n * sizeof(float));
    }
    if (o->cloud) {
        const auto& c = *o->cloud;
        const int n = (int)c.points.size();
        r->cloud.count = n;
        r->cloud.xyz = (float*)std::malloc((size_t)n * 3 * sizeof(float));
        if (r->cloud.xyz)
            for (int i = 0; i < n; ++i) {
                r->cloud.xyz[i*3+0] = c.points[i].x;
                r->cloud.xyz[i*3+1] = c.points[i].y;
                r->cloud.xyz[i*3+2] = c.points[i].z;
            }
    }
    if (o->plane) {
        r->plane.a = o->plane->a; r->plane.b = o->plane->b; r->plane.c = o->plane->c;
        r->plane.valid = o->plane->valid ? 1 : 0;
    }
    if (o->heights) {
        const int n = (int)o->heights->size();
        r->heights.count = n;
        r->heights.values = (double*)std::malloc((size_t)n * sizeof(double));
        if (r->heights.values) std::memcpy(r->heights.values, o->heights->data(), n * sizeof(double));
    }
}

extern "C" {

const char* vsdk_version(void) { return "VisionSDK 0.1.0"; }

void vsdk_free_result(VsdkResult* r) {
    if (!r) return;
    std::free(r->zmap.data);      r->zmap.data = nullptr;
    std::free(r->cloud.xyz);      r->cloud.xyz = nullptr;
    std::free(r->heights.values); r->heights.values = nullptr;
}

int vsdk_run(const char* type, const char* paramsJson,
             const VsdkZMap* inZmap, const VsdkPlane* inPlane, VsdkResult* out) {
    if (!out) return VSDK_BADARG;
    std::memset(out, 0, sizeof(VsdkResult));
    if (!type) { out->status = VSDK_BADARG; setMsg(out, "type is null"); return VSDK_BADARG; }

    json params = json::object();
    if (paramsJson && paramsJson[0]) {
        try { params = json::parse(paramsJson); }
        catch (const std::exception& e) { out->status = VSDK_BADARG; setMsg(out, std::string("param json: ") + e.what()); return VSDK_BADARG; }
    }

    std::shared_ptr<IAlgorithmTool> tool;
    try { tool = ToolFactory::create(type, params, /*noPreview*/true); }
    catch (const std::exception& e) { out->status = VSDK_FAIL; setMsg(out, e.what()); return VSDK_FAIL; }
    if (!tool) { out->status = VSDK_FAIL; setMsg(out, std::string("unknown node type: ") + type); return VSDK_FAIL; }

    VisionDataPtr input = makeInput(inZmap, inPlane);
    ToolResult res;
    try { res = tool->execute(input); }
    catch (const std::exception& e) { out->status = VSDK_FAIL; setMsg(out, e.what()); return VSDK_FAIL; }

    out->status = (res.status == ToolStatus::Ok) ? VSDK_OK
                : (res.status == ToolStatus::Skip) ? VSDK_SKIP : VSDK_FAIL;
    setMsg(out, res.message);
    if (res.output) marshalOut(res.output, out);
    return out->status;
}

/* 노드별 전용 함수 — 각 노드를 개별 함수로 접근 (내부적으로 vsdk_run 위임) */
int vsdk_zmap_load(const char* path, float xr, float yr, float zr, VsdkResult* out) {
    json p = { {"path", path ? path : ""}, {"xResMm", xr}, {"yResMm", yr}, {"zResMm", zr} };
    return vsdk_run("ZMapLoader", p.dump().c_str(), nullptr, nullptr, out);
}
int vsdk_exposure_split(const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("ExposureMerge",  p, in, nullptr, o); }
int vsdk_exposure_merge(const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("ExposureMerge2", p, in, nullptr, o); }
int vsdk_noise_filter (const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("NoiseFilter",    p, in, nullptr, o); }
int vsdk_gap_fill     (const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("GapFill",        p, in, nullptr, o); }
int vsdk_edge_detector(const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("EdgeDetector",   p, in, nullptr, o); }
int vsdk_align        (const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("Align",          p, in, nullptr, o); }
int vsdk_plane_fit    (const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("PlaneFit",       p, in, nullptr, o); }
int vsdk_zmap_to_cloud(const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("ZMapToCloud",    p, in, nullptr, o); }
int vsdk_thickness    (const VsdkZMap* in, const char* p, VsdkResult* o) { return vsdk_run("ThicknessMeasure", p, in, nullptr, o); }
int vsdk_height_measure(const VsdkZMap* in, const VsdkPlane* pl, const char* p, VsdkResult* o) { return vsdk_run("HeightMeasure", p, in, pl, o); }

} // extern "C"
