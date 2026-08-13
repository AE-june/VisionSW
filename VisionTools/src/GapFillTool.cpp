#include "GapFillTool.h"
#include "Logger.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

namespace vision {

ToolResult GapFillTool::execute(VisionDataPtr input) {
    if (!input || !input->inHeightMap(0)) return { ToolStatus::Fail, "GapFill: HeightMap 입력이 필요합니다" };
    const HeightMap& zm = *input->inHeightMap(0);
    const int w = zm.width, h = zm.height;
    const size_t N = (size_t)w * h;
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const auto& src = zm.data;

    // 유효/구멍 마스크 + 각 픽셀→가장 가까운 유효까지 거리
    cv::Mat data(h, w, CV_32F, const_cast<float*>(src.data()));
    cv::Mat validMask = (data == data);                 // 255=유효, 0=NaN
    cv::Mat holeMask;  cv::bitwise_not(validMask, holeMask);
    cv::Mat dist;      cv::distanceTransform(holeMask, dist, cv::DIST_L2, 3);  // 구멍=가장 가까운 유효까지 거리
    const float* dp = dist.ptr<float>();

    // 채울 대상: NaN 이고 거리 ≤ maxGap
    std::vector<uint8_t> fillable(N, 0);
    long target = 0;
    for (size_t i = 0; i < N; ++i)
        if (std::isnan(src[i]) && dp[i] <= (float)m_maxGap) { fillable[i] = 1; ++target; }

    std::vector<float> out = src;   // 유효 픽셀은 그대로 유지

    auto atc = [&](int r, int c) -> float { return out[(size_t)r*w + c]; };

    if (m_method == Method::Neighbor) {
        std::vector<float> cur = out;
        for (int it = 0; it < m_maxGap; ++it) {
            std::vector<float> nxt = cur;
            bool changed = false;
            for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
                size_t i = (size_t)r*w + c;
                if (!fillable[i] || !std::isnan(cur[i])) continue;
                double s = 0; int cnt = 0;
                for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) {
                    if (!dr && !dc) continue;
                    int nr = r+dr, nc = c+dc; if (nr<0||nr>=h||nc<0||nc>=w) continue;
                    float v = cur[(size_t)nr*w+nc]; if (!std::isnan(v)) { s += v; ++cnt; }
                }
                if (cnt >= m_minValid) { nxt[i] = (float)(s/cnt); changed = true; }
            }
            cur.swap(nxt);
            if (!changed) break;
        }
        out.swap(cur);
    }
    else if (m_method == Method::Median) {
        // 반복 이웃 '중앙값' — 평균과 달리 한쪽 값을 택해 단차(엣지)를 보존
        std::vector<float> cur = out;
        for (int it = 0; it < m_maxGap; ++it) {
            std::vector<float> nxt = cur;
            bool changed = false;
            for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
                size_t i = (size_t)r*w + c;
                if (!fillable[i] || !std::isnan(cur[i])) continue;
                float vals[8]; int cnt = 0;
                for (int dr=-1;dr<=1;++dr) for (int dc=-1;dc<=1;++dc) {
                    if (!dr && !dc) continue;
                    int nr=r+dr, nc=c+dc; if(nr<0||nr>=h||nc<0||nc>=w) continue;
                    float v = cur[(size_t)nr*w+nc]; if(!std::isnan(v)) vals[cnt++]=v;
                }
                if (cnt >= m_minValid) {
                    std::nth_element(vals, vals+cnt/2, vals+cnt);
                    nxt[i] = vals[cnt/2]; changed = true;
                }
            }
            cur.swap(nxt);
            if (!changed) break;
        }
        out.swap(cur);
    }
    else if (m_method == Method::Nearest) {
        std::vector<uint8_t> done(N, 0);
        std::deque<int> q;
        for (size_t i = 0; i < N; ++i) if (!std::isnan(src[i])) { done[i]=1; q.push_back((int)i); }
        const int d4r[4]={-1,1,0,0}, d4c[4]={0,0,-1,1};
        while (!q.empty()) {
            int i = q.front(); q.pop_front(); int r=i/w, c=i%w;
            for (int k=0;k<4;++k) {
                int nr=r+d4r[k], nc=c+d4c[k]; if(nr<0||nr>=h||nc<0||nc>=w) continue;
                size_t j=(size_t)nr*w+nc;
                if (!done[j] && fillable[j]) { out[j]=out[i]; done[j]=1; q.push_back((int)j); }
            }
        }
    }
    else if (m_method == Method::Idw) {
        const int R = m_idwRadius;
        for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
            size_t i = (size_t)r*w + c; if (!fillable[i]) continue;
            double s = 0, ws = 0;
            for (int dr=-R; dr<=R; ++dr) for (int dc=-R; dc<=R; ++dc) {
                int nr=r+dr, nc=c+dc; if(nr<0||nr>=h||nc<0||nc>=w) continue;
                float v = src[(size_t)nr*w+nc]; if (std::isnan(v)) continue;
                double d = std::sqrt((double)dr*dr + (double)dc*dc);
                if (d < 1e-6 || d > R) continue;
                double wt = 1.0 / std::pow(d, (double)m_idwPower);
                s += wt*v; ws += wt;
            }
            if (ws > 0) out[i] = (float)(s/ws);
        }
    }
    else if (m_method == Method::Linear) {
        std::vector<float> rowF(N, NaN), colF(N, NaN);
        // 행 방향: 유효로 둘러싸인 NaN 구간을 양끝 값으로 선형보간
        for (int r = 0; r < h; ++r) {
            int c = 0;
            while (c < w) {
                if (!std::isnan(src[(size_t)r*w+c])) { ++c; continue; }
                int s0 = c; while (c < w && std::isnan(src[(size_t)r*w+c])) ++c;
                int left = s0-1, right = c;
                if (left >= 0 && right < w) {
                    float vl = src[(size_t)r*w+left], vr = src[(size_t)r*w+right];
                    for (int k=s0;k<right;++k){ float t=(float)(k-left)/(right-left); rowF[(size_t)r*w+k]=vl*(1-t)+vr*t; }
                }
            }
        }
        // 열 방향
        for (int c = 0; c < w; ++c) {
            int r = 0;
            while (r < h) {
                if (!std::isnan(src[(size_t)r*w+c])) { ++r; continue; }
                int s0 = r; while (r < h && std::isnan(src[(size_t)r*w+c])) ++r;
                int top = s0-1, bot = r;
                if (top >= 0 && bot < h) {
                    float vt = src[(size_t)top*w+c], vb = src[(size_t)bot*w+c];
                    for (int k=s0;k<bot;++k){ float t=(float)(k-top)/(bot-top); colF[(size_t)k*w+c]=vt*(1-t)+vb*t; }
                }
            }
        }
        for (size_t i=0;i<N;++i) {
            if (!fillable[i]) continue;
            bool hr=!std::isnan(rowF[i]), hc=!std::isnan(colF[i]);
            if (hr && hc) out[i]=(rowF[i]+colF[i])*0.5f;
            else if (hr)  out[i]=rowF[i];
            else if (hc)  out[i]=colF[i];
        }
    }
    else if (m_method == Method::Anisotropic) {
        // 엣지 보존 확산: nearest로 초기화(엣지 대략 배치) 후, 값 차이가 크면(엣지)
        // 그 방향으로는 섞지 않는 가중 확산으로 면 안쪽만 매끈하게. 단차 보존.
        std::vector<uint8_t> done(N, 0);
        std::deque<int> q;
        for (size_t i=0;i<N;++i) if(!std::isnan(src[i])){ done[i]=1; q.push_back((int)i); }
        const int a4r[4]={-1,1,0,0}, a4c[4]={0,0,-1,1};
        while(!q.empty()){
            int i=q.front(); q.pop_front(); int r=i/w,c=i%w;
            for(int k=0;k<4;++k){ int nr=r+a4r[k],nc=c+a4c[k]; if(nr<0||nr>=h||nc<0||nc>=w)continue;
                size_t j=(size_t)nr*w+nc; if(!done[j]&&fillable[j]){ out[j]=out[i]; done[j]=1; q.push_back((int)j);} }
        }
        const float sig = std::max(1.f, m_edgeSigma);
        const float inv2s2 = 1.f / (2.f*sig*sig);
        const int maxIter = std::min(500, m_maxGap*6 + 30);
        for (int it=0; it<maxIter; ++it) {
            double maxDelta = 0;
            for (int r=0;r<h;++r) for(int c=0;c<w;++c){
                size_t i=(size_t)r*w+c; if(!fillable[i]) continue;
                float ci=out[i]; double s=0, ws=0;
                auto acc=[&](int nr,int nc){ if(nr<0||nr>=h||nc<0||nc>=w)return; float v=out[(size_t)nr*w+nc];
                    if(std::isnan(v))return; float d=v-ci; float wt=std::exp(-d*d*inv2s2); s+=wt*v; ws+=wt; };
                acc(r-1,c); acc(r+1,c); acc(r,c-1); acc(r,c+1);
                if(ws>0){ float nv=(float)(s/ws); maxDelta=std::max(maxDelta,(double)std::fabs(nv-ci)); out[i]=nv; }
            }
            if (maxDelta < 1e-3) break;
        }
    }
    else {  // Laplace (Gauss-Seidel 반복)
        double meanV = 0; long vc = 0;
        for (size_t i=0;i<N;++i) if(!std::isnan(src[i])){ meanV+=src[i]; ++vc; }
        float init = vc ? (float)(meanV/vc) : 0.f;
        for (size_t i=0;i<N;++i) if (fillable[i]) out[i]=init;
        const int maxIter = std::min(3000, m_maxGap*m_maxGap*6 + 100);
        for (int it=0; it<maxIter; ++it) {
            double maxDelta = 0;
            for (int r=0;r<h;++r) for (int c=0;c<w;++c) {
                size_t i=(size_t)r*w+c; if(!fillable[i]) continue;
                double s=0; int cnt=0;
                if (r>0)   { float v=atc(r-1,c); if(!std::isnan(v)){s+=v;++cnt;} }
                if (r<h-1) { float v=atc(r+1,c); if(!std::isnan(v)){s+=v;++cnt;} }
                if (c>0)   { float v=atc(r,c-1); if(!std::isnan(v)){s+=v;++cnt;} }
                if (c<w-1) { float v=atc(r,c+1); if(!std::isnan(v)){s+=v;++cnt;} }
                if (cnt>0) { float nv=(float)(s/cnt); maxDelta=std::max(maxDelta,(double)std::fabs(nv-out[i])); out[i]=nv; }
            }
            if (maxDelta < 1e-3) break;
        }
    }

    long filled = 0;
    for (size_t i=0;i<N;++i) if (fillable[i] && !std::isnan(out[i])) ++filled;

    // 출력 HeightMap (메타 유지). 주 출력은 항상 메운 결과(stage 0). UI가 stages에서 선택.
    auto mk = [&](const std::vector<float>& d){ auto z=std::make_shared<HeightMap>(zm); z->data=d; return z; };
    auto zFilled = mk(out);

    auto vd = std::make_shared<VisionData>();
    vd->setHeightMap(zFilled);
    vd->sourceId = input->sourceId;
    vd->frames = input->frames;
    if (!m_noPreview) {
        std::vector<float> maskData(N, NaN);
        for (size_t i=0;i<N;++i) maskData[i] = (fillable[i] && !std::isnan(out[i])) ? 1.f
                                           : (std::isnan(src[i]) ? NaN : 0.f);
        auto zOrig = mk(src), zMask = mk(maskData);
        vd->stages = std::make_shared<std::vector<std::pair<std::string, HeightMapPtr>>>();
        vd->stages->push_back({ "1. 메운 결과", zFilled });
        vd->stages->push_back({ "2. 원본",      zOrig });
        vd->stages->push_back({ "3. 메운 영역", zMask });
    }
    VISION_LOG_INFO("GapFill: 채움 {} / 대상 {} px (maxGap={})", filled, target, m_maxGap);
    return { ToolStatus::Ok, "", vd };
}

} // namespace vision
