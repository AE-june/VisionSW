#pragma once
#include "IAlgorithmTool.h"
#include <string>

namespace vision {

// ─────────────────────────────────────────────
//  CloudToZmapTool — PointCloud3D(들) → HeightMap(Zmap) 래스터화.
//
//  입력 포트 0은 배열(여러 PointCloud3D를 한 포트에 연결 가능 — main.cpp의 포트 병합
//  로직이 같은 포트로 들어온 clouds를 하나로 합쳐줌). 모든 클라우드의 점을 모아 bounding
//  box(=격자 크기/원점)를 한 번만 계산하고, 그 하나의 공유 격자에 전부 비닝한다 —
//  ZMapBinning.h가 GetZMapInfoFromPointCloud로 격자를 한 번 정하고 여러 클라우드를 같은
//  격자에 누적하는 것과 같은 목적. 다만 이 툴은 스트리밍 API가 아니라 1회 배치 실행이라
//  "먼저 격자를 고정하고 이후 호출들이 재사용"하는 대신 매 실행마다 입력 클라우드 전체의
//  합집합(union) bbox로 격자를 계산한다 — 그래서 ZMapDimsConsistent 같은 별도 정합성
//  가드가 필요 없다(애초에 범위 밖으로 점이 잘릴 수가 없음).
//
//  축 배치(의도적으로 원시 클라우드 관례 x=transport/y=lateral과 다름 — 라인스캔
//  이미지처럼 "한 행 = 한 스캔 위치"가 되도록 사용자가 지정):
//    HeightMap col(가로/width)  ← point.y (lateral), 해상도 = lateralResMm
//    HeightMap row(세로/height) ← point.x (transport), 해상도 = transportResMm
//    HeightMap raw(height 채널) ← point.z (수직), 해상도 = verticalResMm, 기준=zZeroCount(아래 참고)
//  ⚠️ 따라서 이 출력을 HeightMapToCloud로 되돌리면 cloud.x=lateral, cloud.y=transport로
//     원본과 축이 뒤바뀐 채 나온다(의도된 동작).
//
//  격자 크기/원점: 클라우드의 y(lateral)/x(transport) bounding box에 자동으로 맞춤.
//    origin은 bbox 최솟값이 (col,row)=(0,0)이 되도록 자동 계산 — 별도 파라미터 없음.
//
//  셀 집계(agg): 같은 격자 셀에 여러 점이 겹치면 z값들 중
//    top=최댓값 · bottom=최솟값 · mean=평균 · median=중앙값(기본, 이상치에 강건)
//  포인트가 없는 셀은 NaN(HeightMap 표준 무효 픽셀 규약) — zZeroCount/clamp의 영향을 받지 않음.
//
//  Z 양자화: raw = round(z/verticalResMm + zZeroCount), [0, 65535] clamp.
//  zZeroCount(기본 32768)는 raw=32768(16bit 중간값)이 z=0이 되게 하는 기준 오프셋 —
//  ZMapBinning.h의 zeroPlane과 동일한 역할이며, HeightMapSaverTool이 이후 실제 16bit
//  이미지로 저장할 때 clamp(0,65535)하므로 여기서 미리 같은 범위로 넣어 데이터 손실을 막는다.
//  (clamp 전에는 NaN이 아닌 한 항상 값이 있으므로 무효 셀과 혼동되지 않는다 — Bottom 모드
//  sentinel 트릭이 필요 없는 이유: 빈 셀은 카운터로 판별해 NaN으로 직접 쓴다.)
// ─────────────────────────────────────────────
struct CloudToZmapParams {
    double lateralResMm   = 0.0063;   // col(가로) 해상도 — point.y(lateral)
    double transportResMm = 0.008;    // row(세로) 해상도 — point.x(transport)
    double verticalResMm  = 0.00105;  // z 해상도 (mm/count)
    double zZeroCount      = 32768.0; // z=0에 대응하는 raw count 기준(16bit 중간값). raw=z/verticalResMm+zZeroCount, [0,65535] clamp
    std::string agg       = "median"; // top|mean|median|bottom
};

class CloudToZmapTool : public IAlgorithmTool {
public:
    explicit CloudToZmapTool(CloudToZmapParams p = {}) : m_p(p) {}
    std::string name() const override { return "CloudToZmap"; }
    ToolResult  execute(VisionDataPtr input) override;

private:
    CloudToZmapParams m_p;
};

} // namespace vision
