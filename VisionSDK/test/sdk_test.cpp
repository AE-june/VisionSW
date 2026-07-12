/* VisionSDK 스모크 테스트 — DLL 함수를 C++에서 직접 호출.
 * 파일 의존 없이 인메모리 ZMap(램프)으로 여러 노드를 실행하고 결과를 확인. */
#include "vision_sdk.h"
#include <cstdio>
#include <vector>

int main() {
    const int W = 16, H = 16;
    std::vector<float> d((size_t)W * H);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c)
            d[r * W + c] = 1000.f + c + 2.f * r;   // 기울어진 램프 (평면)

    VsdkZMap in{};
    in.width = W; in.height = H;
    in.xResMm = 0.01f; in.yResMm = 0.05f; in.zResMm = 0.001f; in.zZeroCount = 0.f;
    in.data = d.data();

    std::printf("VisionSDK: %s\n", vsdk_version());
    bool ok = true;

    // 1) NoiseFilter (mean 3x3)
    VsdkResult r1{};
    int s1 = vsdk_noise_filter(&in, "{\"filterType\":\"mean\",\"kernelSizeX\":3,\"kernelSizeY\":3}", &r1);
    std::printf("noise_filter : status=%d out=%dx%d data=%p msg='%s'\n",
                s1, r1.zmap.width, r1.zmap.height, (void*)r1.zmap.data, r1.msg);
    ok &= (s1 == VSDK_OK && r1.zmap.data && r1.zmap.width == W && r1.zmap.height == H);
    vsdk_free_result(&r1);

    // 2) ExposureMerge2 (인터리브 머지, halfRes=false → 원래 높이)
    VsdkResult r2{};
    int s2 = vsdk_exposure_merge(&in, "{\"halfRes\":false}", &r2);
    std::printf("exposure_mrg : status=%d out=%dx%d\n", s2, r2.zmap.width, r2.zmap.height);
    ok &= (s2 == VSDK_OK && r2.zmap.data);
    vsdk_free_result(&r2);

    // 3) 제네릭 vsdk_run — PlaneFit (전체를 ref ROI로). 기대 평면 ~ z=0.1x+0.04y+1.0
    VsdkResult r3{};
    const char* pf =
        "{\"algorithm\":\"LeastSquares\",\"rois\":[{\"type\":\"ref\",\"shape\":\"rect\","
        "\"xPct\":0.0,\"yPct\":0.0,\"wPct\":1.0,\"hPct\":1.0}]}";
    int s3 = vsdk_run("PlaneFit", pf, &in, nullptr, &r3);
    std::printf("plane_fit    : status=%d valid=%d a=%.5f b=%.5f c=%.4f\n",
                s3, r3.plane.valid, r3.plane.a, r3.plane.b, r3.plane.c);
    ok &= (s3 == VSDK_OK && r3.plane.valid);
    vsdk_free_result(&r3);

    // 4) 알 수 없는 노드 → 실패 경로 확인
    VsdkResult r4{};
    int s4 = vsdk_run("NoSuchNode", "{}", &in, nullptr, &r4);
    std::printf("unknown node : status=%d msg='%s'\n", s4, r4.msg);
    ok &= (s4 == VSDK_FAIL);
    vsdk_free_result(&r4);

    std::printf(ok ? "ALL OK\n" : "FAIL\n");
    return ok ? 0 : 1;
}
