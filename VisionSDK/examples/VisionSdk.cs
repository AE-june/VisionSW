// VisionSDK — C# P/Invoke 바인딩 + 사용 예시.
//   VisionSDK.dll 및 opencv_world*.dll 을 실행 폴더(또는 PATH)에 두고 실행.
//   구조체 레이아웃은 vision_sdk.h 와 1:1 대응.
using System;
using System.Runtime.InteropServices;

public static class VisionSdk
{
    const string DLL = "VisionSDK.dll";

    [StructLayout(LayoutKind.Sequential)]
    public struct ZMap
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
        public ZMap zmap;
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
                                      ref ZMap inZmap, IntPtr inPlane, out Result outp);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vsdk_noise_filter(ref ZMap inZmap, string paramsJson, out Result outp);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vsdk_exposure_merge(ref ZMap inZmap, string paramsJson, out Result outp);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vsdk_plane_fit(ref ZMap inZmap, string paramsJson, out Result outp);

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
            var zin = new ZMap
            {
                width = W, height = H,
                xResMm = 0.01f, yResMm = 0.05f, zResMm = 0.001f,
                data = handle.AddrOfPinnedObject()
            };

            // 1) NoiseFilter
            int s = vsdk_noise_filter(ref zin, "{\"filterType\":\"mean\",\"kernelSizeX\":3,\"kernelSizeY\":3}", out Result r1);
            Console.WriteLine($"noise_filter status={s} out={r1.zmap.width}x{r1.zmap.height}");
            // 출력 ZMap 읽기
            if (r1.zmap.data != IntPtr.Zero)
            {
                var outBuf = new float[r1.zmap.width * r1.zmap.height];
                Marshal.Copy(r1.zmap.data, outBuf, 0, outBuf.Length);
            }
            vsdk_free_result(ref r1);   // 반드시 해제

            // 2) PlaneFit (제네릭 경로)
            string pf = "{\"algorithm\":\"LeastSquares\",\"rois\":[{\"type\":\"ref\",\"shape\":\"rect\"," +
                        "\"xPct\":0.0,\"yPct\":0.0,\"wPct\":1.0,\"hPct\":1.0}]}";
            int s2 = vsdk_run("PlaneFit", pf, ref zin, IntPtr.Zero, out Result r2);
            Console.WriteLine($"plane_fit status={s2} a={r2.plane.a:F4} b={r2.plane.b:F4} c={r2.plane.c:F4}");
            vsdk_free_result(ref r2);
        }
        finally { handle.Free(); }
    }
}
