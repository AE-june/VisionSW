import { useState } from 'react'
import RoiCanvas, { type Roi } from './RoiCanvas'
import { ScanArrow } from './lineCenterViz'

export interface LineCenterSettings {
  rois: Roi[]
  threshold: number
  scanDir: string    // 'lr' | 'rl' | 'tb' | 'bt'
  polarity: string   // 'd2l' (흑→백) | 'l2d' (백→흑)
}

interface Props extends LineCenterSettings {
  preview?: string
  zMin?: number
  zMax?: number
  resXMm?: number
  resYMm?: number
  onChange: (next: LineCenterSettings) => void
}

const ROI_TYPES = [{ type: 'search', label: 'Search', single: true }]

export default function LineCenterEditor(props: Props) {
  const { rois, threshold, scanDir, polarity, preview, zMin, zMax, resXMm, resYMm, onChange } = props
  const [img, setImg] = useState({ w: 0, h: 0 })

  const emit = (patch: Partial<LineCenterSettings>) =>
    onChange({ rois, threshold, scanDir, polarity, ...patch })

  // 검색 ROI 위에 스캔 방향 화살표
  const arrow = img.w && img.h
    ? <ScanArrow roi={rois.find(r => r.type === 'search')} scanDir={scanDir} imgW={img.w} imgH={img.h} />
    : undefined

  return (
    <div>
      <RoiCanvas
        rois={rois}
        roiTypes={ROI_TYPES}
        preview={preview}
        zMin={zMin}
        zMax={zMax}
        resXMm={resXMm}
        resYMm={resYMm}
        enableRotate
        overlay={arrow}
        onImageSize={(w, h) => setImg({ w, h })}
        onChange={r => emit({ rois: r })}
      />

      <div className="param-section">스캔</div>
      <div className="param-row">
        <span className="param-label">방향</span>
        <select className="param-select" value={scanDir}
          onChange={e => emit({ scanDir: e.target.value })}>
          <option value="lr">좌 → 우 →</option>
          <option value="rl">우 → 좌 ←</option>
          <option value="tb">위 → 아래 ↓</option>
          <option value="bt">아래 → 위 ↑</option>
        </select>
      </div>
      <div className="param-row">
        <span className="param-label">에지 극성</span>
        <select className="param-select" value={polarity}
          onChange={e => emit({ polarity: e.target.value })}>
          <option value="d2l">흑 → 백 (어두움→밝음)</option>
          <option value="l2d">백 → 흑 (밝음→어두움)</option>
        </select>
      </div>
      <div className="param-section">이진화</div>
      <div className="param-row">
        <span className="param-label">임계값 (raw)</span>
        <input className="param-input" type="number" step="1" value={threshold}
          onChange={e => emit({ threshold: parseFloat(e.target.value) || 0 })} />
      </div>
      <div className="param-empty" style={{ fontSize: 10 }}>
        스캔 방향으로 진행하며 첫 에지(임계값 교차)를 찾아 라인 중심 (x, y)을 출력합니다. ROI를 회전하면 대각선 에지도 검색됩니다.
      </div>
    </div>
  )
}
