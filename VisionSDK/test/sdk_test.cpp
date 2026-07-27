/* VisionSDK 스모크 테스트 — DLL 함수를 C++에서 직접 호출.
 * 파일 의존 없이 인메모리 HeightMap(램프)으로 여러 노드를 실행하고 결과를 확인. */
#include "vision_sdk.h"
#include <cstdio>
#include <vector>
#include <cmath>

int main() {
    const int W = 16, H = 16;
    std::vector<float> d((size_t)W * H);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c)
            d[r * W + c] = 1000.f + c + 2.f * r;   // 기울어진 램프 (평면)

    VsdkHeightMap in{};
    in.width = W; in.height = H;
    in.xResMm = 0.01f; in.yResMm = 0.05f; in.zResMm = 0.001f; in.zZeroCount = 0.f;
    in.data = d.data();

    std::printf("VisionSDK: %s\n", vsdk_version());
    bool ok = true;

    // 1) NoiseFilter (mean 3x3)
    VsdkResult r1{};
    int s1 = vsdk_noise_filter(&in, "{\"filterType\":\"mean\",\"kernelSizeX\":3,\"kernelSizeY\":3}", &r1);
    std::printf("noise_filter : status=%d out=%dx%d data=%p msg='%s'\n",
                s1, r1.heightmap.width, r1.heightmap.height, (void*)r1.heightmap.data, r1.msg);
    ok &= (s1 == VSDK_OK && r1.heightmap.data && r1.heightmap.width == W && r1.heightmap.height == H);
    vsdk_free_result(&r1);

    // 2) ExposureMerge2 (인터리브 머지, halfRes=false → 원래 높이)
    VsdkResult r2{};
    int s2 = vsdk_exposure_merge(&in, "{\"halfRes\":false}", &r2);
    std::printf("exposure_mrg : status=%d out=%dx%d\n", s2, r2.heightmap.width, r2.heightmap.height);
    ok &= (s2 == VSDK_OK && r2.heightmap.data);
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

    // 5) point cloud 이중노출 머지 — 조직화(width=4, profiles=4, 짝=저/홀=고). per-point X 보존 확인.
    {
        const int W = 4, P = 4;                 // n=2 출력 프로파일
        std::vector<float> pc((size_t)W*P*3);
        for (int pr = 0; pr < P; ++pr)
            for (int c = 0; c < W; ++c) {
                float* d = &pc[((size_t)pr*W + c)*3];
                d[0] = c*0.01f + pr*0.0001f;    // per-point X (프로파일마다 미세 차 → 보존 검증)
                d[1] = pr*0.05f;
                d[2] = 1000.f + c;              // 저·고 동일 Z → 머지는 저노출 채택(offset≈0)
            }
        VsdkResult r5{};
        int s5 = vsdk_exposure_merge_cloud(pc.data(), W, P, "{}", &r5);
        // 출력 셀 (프로파일0=저=입력프로파일0, col1)의 X가 입력 저노출 점 X와 일치하는지
        float gotX = (r5.cloud.xyz && r5.cloud.count >= 2) ? r5.cloud.xyz[(0*W+1)*3+0] : -1.f;
        float expX = pc[((size_t)0*W+1)*3+0];   // 입력 저노출(프로파일0) col1 X
        std::printf("merge_cloud  : status=%d count=%d (expect %d)  X=%.5f (expect %.5f, per-point 보존)\n",
                    s5, r5.cloud.count, (P/2)*W, gotX, expX);
        ok &= (s5 == VSDK_OK && r5.cloud.count == (P/2)*W && std::abs(gotX - expX) < 1e-6f);
        vsdk_free_result(&r5);
    }

    std::printf(ok ? "ALL OK\n" : "FAIL\n");
    return ok ? 0 : 1;
}
