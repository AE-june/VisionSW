/* VisionSDK 레시피 스모크 테스트 — "unfiltered PCD_merge.json" 전 노드를 vsdk_run_ex 로 체인.
 *   CloudLoader → PointCloudSplit → (CloudSelect0→ZReduce min→ZReduce max)  ┐
 *                                    (CloudSelect1→ZReduce max)              │
 *                 → CloudToHeightMap ×2 → ExposureMerge(port1=idx0,port0=idx1)
 *                 → GapFill → HeightMapToCloud → CloudSaver.
 * 실제 .ply 파일이 있어야 실행됨(없으면 SKIP 처리하고 성공 반환).
 * 목적: 확장 SDK(vsdk_run_ex)만으로 cloud 입력/멀티포트/다중 cloud 출력이
 *       전부 동작하는지(=레시피 전 노드 라이브러리 사용 가능) 검증. */
#include "vision_sdk.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>

static const char* PLY_PATH =
    "D:\\Feasibility Study\\260511_SDC\\0916_unfiltered point cloud test\\exp6,40us\\"
    "unfiltered_pointcloud_00-01-61-18-99_260916_175046.ply";

// 포트 하나에 cloud 배열을 실어 반환하는 헬퍼 (VsdkResultEx.clouds → VsdkPort).
static VsdkPort cloudPort(VsdkCloud* clouds, int n) {
    VsdkPort p{}; p.cloudCount = n; p.clouds = clouds; return p;
}
static VsdkPort hmPort(VsdkHeightMap* hms, int n) {
    VsdkPort p{}; p.heightmapCount = n; p.heightmaps = hms; return p;
}

// 노드 1개 실행 + 상태/출력 요약 출력. 실패 시 프로그램 실패.
static bool step(const char* type, const char* params,
                 const VsdkPort* ports, int portCount, VsdkResultEx* out, bool& ok) {
    std::memset(out, 0, sizeof(*out));
    int s = vsdk_run_ex(type, params, ports, portCount, out);
    std::printf("%-18s: status=%d  hm=%d cloud=%d  msg='%s'\n",
                type, s, out->heightmapCount, out->cloudCount, out->msg);
    bool good = (s == VSDK_OK);
    ok &= good;
    return good;
}

int main() {
    std::printf("VisionSDK recipe test: %s\n", vsdk_version());

    // 파일 존재 확인 (없으면 SKIP — CI/타 머신에서 성공 처리)
    if (FILE* f = std::fopen(PLY_PATH, "rb")) { std::fclose(f); }
    else { std::printf("SKIP: ply not found (%s)\n", PLY_PATH); std::printf("ALL OK\n"); return 0; }

    bool ok = true;

    // node-1 CloudLoader (입력 없음)
    VsdkResultEx loaded{};
    { std::string params = std::string("{\"path\":\"") + [] {
        std::string s = PLY_PATH; std::string e; for (char c : s) { if (c=='\\') e += "\\\\"; else e += c; } return e; }()
        + "\",\"swapXY\":false}";
      step("CloudLoader", params.c_str(), nullptr, 0, &loaded, ok); }
    if (!ok || loaded.cloudCount < 1) { std::printf("FAIL (load)\n"); vsdk_free_result_ex(&loaded); return 1; }

    // node-2 PointCloudSplit(splitCount=2, x, 0.048) → clouds[0..1]
    VsdkResultEx split{};
    { VsdkPort p = cloudPort(loaded.clouds, loaded.cloudCount);
      step("PointCloudSplit", "{\"splitCount\":2,\"scanAxis\":\"x\",\"scanStepMm\":0.048}", &p, 1, &split, ok); }
    if (split.cloudCount < 2) { std::printf("FAIL: split expected 2 clouds, got %d\n", split.cloudCount); ok = false; }

    // ── 경로 A (idx0): CloudSelect0 → ZReduce(min,roi x0..12) → ZReduce(max,roi x12..) → CloudToHeightMap
    VsdkResultEx selA{}, redA1{}, redA2{}, hmA{};
    { VsdkPort p = cloudPort(split.clouds, split.cloudCount);
      step("CloudSelect", "{\"cloudIdx\":0}", &p, 1, &selA, ok); }
    { VsdkPort p = cloudPort(selA.clouds, selA.cloudCount);
      step("CloudZReduce", "{\"reduce\":\"min\",\"xStepMm\":0.096,\"yStepMm\":0.0126,\"neighborRange\":3,"
           "\"roiEnabled\":true,\"roiXMin\":0,\"roiXMax\":12,\"roiYMin\":-1000,\"roiYMax\":1000}", &p, 1, &redA1, ok); }
    { VsdkPort p = cloudPort(redA1.clouds, redA1.cloudCount);
      step("CloudZReduce", "{\"reduce\":\"max\",\"xStepMm\":0.096,\"yStepMm\":0.0126,\"neighborRange\":2,"
           "\"roiEnabled\":true,\"roiXMin\":12,\"roiXMax\":1000,\"roiYMin\":-1000,\"roiYMax\":1000,"
           "\"roiZMin\":-1000,\"roiZMax\":1000}", &p, 1, &redA2, ok); }
    { VsdkPort p = cloudPort(redA2.clouds, redA2.cloudCount);
      step("CloudToHeightMap", "{\"mode\":\"top\",\"xResMm\":0.0126,\"yResMm\":0.096,\"zResMm\":0.00105,"
           "\"autoRange\":false,\"xMin\":0,\"xMax\":360,\"yMin\":-100,\"yMax\":100}", &p, 1, &hmA, ok); }

    // ── 경로 B (idx1): CloudSelect1 → ZReduce(max,noroi) → CloudToHeightMap
    VsdkResultEx selB{}, redB{}, hmB{};
    { VsdkPort p = cloudPort(split.clouds, split.cloudCount);
      step("CloudSelect", "{\"cloudIdx\":1}", &p, 1, &selB, ok); }
    { VsdkPort p = cloudPort(selB.clouds, selB.cloudCount);
      step("CloudZReduce", "{\"reduce\":\"max\",\"xStepMm\":0.096,\"yStepMm\":0.0126,\"neighborRange\":3,"
           "\"roiEnabled\":false}", &p, 1, &redB, ok); }
    { VsdkPort p = cloudPort(redB.clouds, redB.cloudCount);
      step("CloudToHeightMap", "{\"mode\":\"top\",\"xResMm\":0.0126,\"yResMm\":0.096,\"zResMm\":0.00105,"
           "\"autoRange\":false,\"xMin\":0,\"xMax\":360,\"yMin\":-100,\"yMax\":100}", &p, 1, &hmB, ok); }

    // node-19 ExposureMerge: port0 = hmB(node-18, idx1), port1 = hmA(node-17, idx0)
    VsdkResultEx merged{};
    if (hmA.heightmapCount >= 1 && hmB.heightmapCount >= 1) {
        VsdkPort ports[2] = { hmPort(hmB.heightmaps, 1), hmPort(hmA.heightmaps, 1) };
        step("ExposureMerge", "{\"exposureCount\":2,\"matchTol\":30,\"reflTol\":30,\"tolX\":10,\"tolY\":50,"
             "\"gapK\":3,\"removeReflection\":true,\"bands\":0}", ports, 2, &merged, ok);
    } else { std::printf("FAIL: merge inputs missing\n"); ok = false; }

    // node-24 GapFill → node-22 HeightMapToCloud → node-23 CloudSaver
    VsdkResultEx gap{}, toCloud{}, saved{};
    if (merged.heightmapCount >= 1) {
        VsdkPort p = hmPort(merged.heightmaps, 1);
        step("GapFill", "{\"method\":\"median\",\"maxGap\":5,\"minValidNeighbors\":3,"
             "\"idwRadius\":8,\"idwPower\":2,\"edgeSigma\":30}", &p, 1, &gap, ok);
    }
    if (gap.heightmapCount >= 1) {
        VsdkPort p = hmPort(gap.heightmaps, 1);
        step("HeightMapToCloud", "{\"step\":1}", &p, 1, &toCloud, ok);
    }
    if (toCloud.cloudCount >= 1) {
        VsdkPort p = cloudPort(toCloud.clouds, toCloud.cloudCount);
        step("CloudSaver", "{\"folder\":\"D:\\\\Feasibility Study\\\\260511_SDC\\\\0916_unfiltered point cloud test\\\\exp6,40us\","
             "\"filename\":\"result_sdk\",\"format\":\"ply\",\"cloudIdx\":0}", &p, 1, &saved, ok);
    }

    // 정리
    for (VsdkResultEx* r : { &loaded,&split,&selA,&redA1,&redA2,&hmA,&selB,&redB,&hmB,&merged,&gap,&toCloud,&saved })
        vsdk_free_result_ex(r);

    std::printf(ok ? "ALL OK\n" : "FAIL\n");
    return ok ? 0 : 1;
}
