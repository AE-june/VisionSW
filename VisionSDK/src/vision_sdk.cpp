/* VisionSDK 구현 — ToolFactory::create + execute 를 C ABI로 래핑.
 * 모든 노드가 ToolFactory를 통해 생성되므로 팩토리가 아는 모든 타입이 자동 노출된다. */
/* VISIONSDK_EXPORTS 는 CMake(target_compile_definitions)에서 정의됨 */
#include "vision_sdk.h"

#include "ToolFactory.h"
#include "VisionData.h"
#include "HeightMap.h"
#include "ExposureMergeCore.h"
#include <nlohmann/json.hpp>

#include <cstring>
#include <cstdlib>
#include <string>
#include <memory>
#include <exception>
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>

using namespace vision;
using json = nlohmann::json;

static void setMsg(VsdkResult* r, const std::string& m) {
    std::strncpy(r->msg, m.c_str(), sizeof(r->msg) - 1);
    r->msg[sizeof(r->msg) - 1] = '\0';
}

// VsdkHeightMap(+선택 평면) → VisionData 입력 (복사).
static VisionDataPtr makeInput(const VsdkHeightMap* z, const VsdkPlane* p) {
    if ((!z || !z->data) && (!p || !p->valid)) return nullptr;
    auto d = std::make_shared<VisionData>();
    if (z && z->data) {
        auto zm = std::make_shared<HeightMap>();
        zm->width = z->width;         zm->height = z->height;
        zm->xResMm = z->xResMm;       zm->yResMm = z->yResMm;   zm->zResMm = z->zResMm;
        zm->zZeroCount = z->zZeroCount; zm->originCol = z->originCol; zm->originRow = z->originRow;
        zm->data.assign(z->data, z->data + (size_t)z->width * z->height);
        d->heightmap = zm;
    }
    if (p && p->valid)
        d->plane = std::make_shared<PlaneModel>(PlaneModel{ p->a, p->b, p->c, true });
    return d;
}

// VisionData 출력 → VsdkResult 페이로드 (SDK가 malloc 소유).
static void marshalOut(const VisionDataPtr& o, VsdkResult* r) {
    if (!o) return;
    if (o->heightmap) {
        const auto& z = *o->heightmap;
        const size_t n = (size_t)z.width * z.height;
        r->heightmap.width = z.width;       r->heightmap.height = z.height;
        r->heightmap.xResMm = z.xResMm;     r->heightmap.yResMm = z.yResMm; r->heightmap.zResMm = z.zResMm;
        r->heightmap.zZeroCount = z.zZeroCount; r->heightmap.originCol = z.originCol; r->heightmap.originRow = z.originRow;
        r->heightmap.data = (float*)std::malloc(n * sizeof(float));
        if (r->heightmap.data) std::memcpy(r->heightmap.data, z.data.data(), n * sizeof(float));
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
    std::free(r->heightmap.data);      r->heightmap.data = nullptr;
    std::free(r->cloud.xyz);      r->cloud.xyz = nullptr;
    std::free(r->heights.values); r->heights.values = nullptr;
}

int vsdk_run(const char* type, const char* paramsJson,
             const VsdkHeightMap* inHeightmap, const VsdkPlane* inPlane, VsdkResult* out) {
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

    VisionDataPtr input = makeInput(inHeightmap, inPlane);
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
int vsdk_heightmap_load(const char* path, float xr, float yr, float zr, VsdkResult* out) {
    json p = { {"path", path ? path : ""}, {"xResMm", xr}, {"yResMm", yr}, {"zResMm", zr} };
    return vsdk_run("HeightMapLoader", p.dump().c_str(), nullptr, nullptr, out);
}
int vsdk_exposure_split(const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("ExposureMerge",  p, in, nullptr, o); }
int vsdk_exposure_merge(const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("ExposureMerge2", p, in, nullptr, o); }
int vsdk_noise_filter (const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("NoiseFilter",    p, in, nullptr, o); }
int vsdk_gap_fill     (const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("GapFill",        p, in, nullptr, o); }
int vsdk_edge_detector(const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("EdgeDetector",   p, in, nullptr, o); }
int vsdk_align        (const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("Align",          p, in, nullptr, o); }
int vsdk_plane_fit    (const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("PlaneFit",       p, in, nullptr, o); }
int vsdk_heightmap_to_cloud(const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("HeightMapToCloud",    p, in, nullptr, o); }
int vsdk_thickness    (const VsdkHeightMap* in, const char* p, VsdkResult* o) { return vsdk_run("ThicknessMeasure", p, in, nullptr, o); }
int vsdk_height_measure(const VsdkHeightMap* in, const VsdkPlane* pl, const char* p, VsdkResult* o) { return vsdk_run("HeightMeasure", p, in, pl, o); }

/* ── 조직화된 point cloud 이중노출 머지 (per-point X 보존) ──────────────────────
 *  xyz: numProfiles*width 개 점(각 x,y,z 3연속 float, row-major). 짝수 프로파일=저노출,
 *       홀수=고노출. 무효점(NaN/Inf/−999999/(0,0,0))은 Z=NaN로 취급.
 *  결과: out->cloud 에 머지된 조직화 cloud (count=(numProfiles/2)*width, row-major).
 *        저=이긴 저노출 점(z−offset), 고=고노출 점, 제거=NaN 점. 그리드는 (numProfiles/2)×width. */
int vsdk_exposure_merge_cloud(const float* xyz, int width, int numProfiles,
                              const char* paramsJson, VsdkResult* out) {
    if (!out) return VSDK_BADARG;
    std::memset(out, 0, sizeof(VsdkResult));
    if (!xyz || width <= 0 || numProfiles < 2) { out->status=VSDK_BADARG; setMsg(out,"bad args (xyz/width/numProfiles)"); return VSDK_BADARG; }

    json p = json::object();
    if (paramsJson && paramsJson[0]) {
        try { p = json::parse(paramsJson); }
        catch (const std::exception& e) { out->status=VSDK_BADARG; setMsg(out, std::string("param json: ")+e.what()); return VSDK_BADARG; }
    }
    const float matchTol = p.value("matchTol", 20.f), tolX = p.value("tolX", 5.f), tolY = p.value("tolY", 30.f);
    const int   gapK     = p.value("gapK", 0);

    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const int n = numProfiles / 2;               // 출력 프로파일(pair) 수
    const size_t BN = (size_t)n * width;
    auto invalid = [](float x, float y, float z) {
        return std::isnan(x)||std::isnan(y)||std::isnan(z)||std::isinf(x)||std::isinf(y)||std::isinf(z)
            || x==-999999.f||y==-999999.f||z==-999999.f || (x==0.f&&y==0.f&&z==0.f);
    };

    // 짝/홀 프로파일 → 저/고 Z 그리드
    std::vector<float> low(BN), high(BN);
    for (int r = 0; r < n; ++r) for (int c = 0; c < width; ++c) {
        size_t i = (size_t)r*width + c;
        const float* pl = &xyz[((size_t)(2*r)*width   + c)*3];
        const float* ph = &xyz[((size_t)(2*r+1)*width + c)*3];
        low[i]  = invalid(pl[0],pl[1],pl[2]) ? NaN : pl[2];
        high[i] = invalid(ph[0],ph[1],ph[2]) ? NaN : ph[2];
    }

    std::vector<uint8_t> source;
    float offset = exposureMergeDecision(low.data(), high.data(), width, n, matchTol, tolX, tolY, gapK, NaN, source);

    // source → 이긴 노출의 점(x,y,z) (저는 z−offset). 제거/무효는 NaN 점.
    float* mc = (float*)std::malloc(BN * 3 * sizeof(float));
    if (!mc) { out->status=VSDK_FAIL; setMsg(out,"oom"); return VSDK_FAIL; }
    for (int r = 0; r < n; ++r) for (int c = 0; c < width; ++c) {
        size_t i = (size_t)r*width + c; float* d = &mc[i*3];
        uint8_t s = source[i];
        if (s == 1)      { const float* pl=&xyz[((size_t)(2*r)*width  +c)*3]; d[0]=pl[0]; d[1]=pl[1]; d[2]=pl[2]-offset; }
        else if (s == 2) { const float* ph=&xyz[((size_t)(2*r+1)*width+c)*3]; d[0]=ph[0]; d[1]=ph[1]; d[2]=ph[2]; }
        else             { d[0]=d[1]=d[2]=NaN; }
    }
    out->status = VSDK_OK;
    out->cloud.count = (int)BN;    // 그리드 = (numProfiles/2) × width (호출자가 앎)
    out->cloud.xyz   = mc;
    return VSDK_OK;
}

} // extern "C"
