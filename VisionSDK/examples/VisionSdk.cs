// VisionSDK — C# P/Invoke 바인딩 + 사용 예시.
//   VisionSDK.dll 및 opencv_world*.dll 을 실행 폴더(또는 PATH)에 두고 실행.
//   구조체 레이아웃은 vision_sdk.h 와 1:1 대응.
using System;
using System.Runtime.InteropServices;

public static class VisionSdk
{
    const string DLL = "VisionSDK.dll";

    [StructLayout(LayoutKind.Sequential)]
    public struct HeightMap
    {
        public int width, height;
        public float xResMm, yResMm, zResMm, zZeroCount, originCol, originRow;
        public IntPtr data;   // float* (길이 width*height)
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Cloud { public int count; public IntPtr xyz; }      // float* (count*3)

    [StructLayout(LayoutKind.Sequential)]
    public struct Plane { public double a, b, c; public int valid; }

    [StructLayout(LayoutKind.Sequential)]
    public struct Heights { public int count; public IntPtr values; } // double*

    [StructLayout(LayoutKind.Sequential)]
    public struct Result
    {
        public int status;                 // 0=OK
        public HeightMap heightmap;
        public Cloud cloud;
        public Plane plane;
        public Heights heights;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
        public byte[] msg;
    }

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vsdk_version();

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vsdk_free_result(ref Result r);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vsdk_run(string type, string paramsJson,
                                      ref HeightMap inHeightmap, IntPtr inPlane, out Result outp);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vsdk_noise_filter(ref HeightMap inHeightmap, string paramsJson, out Result outp);

    // 노출 머지는 다중 HeightMap 입력 → vsdk_run_ex("ExposureMerge") 사용(아래 참조).

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vsdk_plane_fit(ref HeightMap inHeightmap, string paramsJson, out Result outp);

    // ── 포트 기반 확장 API (cloud 입력·멀티포트·다중 출력) ──────────────────────
    //   구조체는 vision_sdk.h 의 VsdkPort / VsdkResultEx 와 1:1.
    [StructLayout(LayoutKind.Sequential)]
    public struct Port
    {
        public int heightmapCount;
        public IntPtr heightmaps;   // HeightMap[] (길이 heightmapCount)
        public int cloudCount;
        public IntPtr clouds;        // Cloud[] (길이 cloudCount)
        public int planeValid;
        public Plane plane;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct ResultEx
    {
        public int status;
        public int heightmapCount;
        public IntPtr heightmaps;    // HeightMap[] (SDK malloc)
        public int cloudCount;
        public IntPtr clouds;         // Cloud[] (SDK malloc)
        public Plane plane;
        public Heights heights;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
        public byte[] msg;
    }

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vsdk_free_result_ex(ref ResultEx r);

    // ports: Port[] 를 pin 하여 포인터 전달. portCount = ports.Length.
    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vsdk_run_ex(string type, string paramsJson,
                                         IntPtr ports, int portCount, out ResultEx outp);

    // ResultEx.clouds(IntPtr) → Cloud[] 로 읽는 헬퍼.
    public static Cloud[] ReadClouds(ResultEx r)
    {
        var arr = new Cloud[r.cloudCount];
        int stride = Marshal.SizeOf<Cloud>();
        for (int i = 0; i < r.cloudCount; i++)
            arr[i] = Marshal.PtrToStructure<Cloud>(r.clouds + i * stride);
        return arr;
    }
    // ResultEx.heightmaps(IntPtr) → HeightMap[] 로 읽는 헬퍼.
    public static HeightMap[] ReadHeightMaps(ResultEx r)
    {
        var arr = new HeightMap[r.heightmapCount];
        int stride = Marshal.SizeOf<HeightMap>();
        for (int i = 0; i < r.heightmapCount; i++)
            arr[i] = Marshal.PtrToStructure<HeightMap>(r.heightmaps + i * stride);
        return arr;
    }

    // ── 사용 예시 ────────────────────────────────────────────────────────
    static void Main()
    {
        int W = 16, H = 16;
        var buf = new float[W * H];
        for (int r = 0; r < H; r++)
            for (int c = 0; c < W; c++)
                buf[r * W + c] = 1000f + c + 2f * r;

        // 입력 버퍼를 고정(pin)하고 포인터 전달
        var handle = GCHandle.Alloc(buf, GCHandleType.Pinned);
        try
        {
            var zin = new HeightMap
            {
                width = W, height = H,
                xResMm = 0.01f, yResMm = 0.05f, zResMm = 0.001f,
                data = handle.AddrOfPinnedObject()
            };

            // 1) NoiseFilter
            int s = vsdk_noise_filter(ref zin, "{\"filterType\":\"mean\",\"kernelSizeX\":3,\"kernelSizeY\":3}", out Result r1);
            Console.WriteLine($"noise_filter status={s} out={r1.heightmap.width}x{r1.heightmap.height}");
            // 출력 HeightMap 읽기
            if (r1.heightmap.data != IntPtr.Zero)
            {
                var outBuf = new float[r1.heightmap.width * r1.heightmap.height];
                Marshal.Copy(r1.heightmap.data, outBuf, 0, outBuf.Length);
            }
            vsdk_free_result(ref r1);   // 반드시 해제

            // 2) PlaneFit (제네릭 경로)
            string pf = "{\"algorithm\":\"LeastSquares\",\"rois\":[{\"type\":\"ref\",\"shape\":\"rect\"," +
                        "\"xPct\":0.0,\"yPct\":0.0,\"wPct\":1.0,\"hPct\":1.0}]}";
            int s2 = vsdk_run("PlaneFit", pf, ref zin, IntPtr.Zero, out Result r2);
            Console.WriteLine($"plane_fit status={s2} a={r2.plane.a:F4} b={r2.plane.b:F4} c={r2.plane.c:F4}");
            vsdk_free_result(ref r2);

            // 3) 포트 기반 cloud 파이프라인 (vsdk_run_ex) — CloudLoader → PointCloudSplit.
            //    로더는 입력 없음(ports=null, portCount=0). 출력 cloud 를 다음 노드 포트에 전달.
            int sL = vsdk_run_ex("CloudLoader",
                "{\"path\":\"D:\\\\data\\\\scan.ply\",\"swapXY\":false}", IntPtr.Zero, 0, out ResultEx rl);
            Console.WriteLine($"cloud_load status={sL} clouds={rl.cloudCount}");
            if (sL == 0 && rl.cloudCount >= 1)
            {
                // 로더 출력 cloud[0] 를 PointCloudSplit 입력 포트(port0)에 실어 전달.
                var port = new Port { cloudCount = rl.cloudCount, clouds = rl.clouds };
                var ph = GCHandle.Alloc(new[] { port }, GCHandleType.Pinned);
                try
                {
                    int sS = vsdk_run_ex("PointCloudSplit",
                        "{\"splitCount\":2,\"scanAxis\":\"x\",\"scanStepMm\":0.048}",
                        ph.AddrOfPinnedObject(), 1, out ResultEx rs);
                    Console.WriteLine($"split status={sS} clouds={rs.cloudCount}");  // 2 기대
                    // rs.clouds → CloudSelect/CloudZReduce … 로 계속 체인.
                    vsdk_free_result_ex(ref rs);
                }
                finally { ph.Free(); }
            }
            vsdk_free_result_ex(ref rl);
        }
        finally { handle.Free(); }
    }
}
