import { useState } from 'react'
import RoiCanvas, { type Roi } from './RoiCanvas'
import { ScanArrow } from './lineCenterViz'

export interface LineCenterSettings {
  rois: Roi[]
  threshold: number
}

// 스캔은 항상 ROI 로컬 +x(좌→우) 방향 — 방향은 ROI 회전으로 결정
const SCAN_DIR = 'lr'

interface Props extends LineCenterSettings {
  preview?: string
  zMin?: number
  zMax?: number
  resXMm?: number
  resYMm?: number
  onChange: (next: LineCenterSettings) => void
}

const ROI_TYPES = [{ type: 'search', label: 'Search' }]

export default function LineCenterEditor(props: Props) {
  const { rois, threshold, preview, zMin, zMax, resXMm, resYMm, onChange } = props
  const [img, setImg] = useState({ w: 0, h: 0 })

  const emit = (patch: Partial<LineCenterSettings>) =>
    onChange({ rois, threshold, ...patch })

  const setPolarity = (id: string, val: string) =>
    emit({ rois: rois.map(r => r.id === id ? { ...r, polarity: val } : r) })

  const searchRois = rois.filter(r => r.type === 'search')

  // 각 검색 ROI 밖에 스캔 방향 화살표 (회전 따라감). zoom을 받아 화면상 크기 고정.
  const arrow = img.w && img.h
    ? (zoom: number) => <>{searchRois.map(r => (
        <ScanArrow key={r.id} roi={r} scanDir={SCAN_DIR} imgW={img.w} imgH={img.h} zoom={zoom} />
      ))}</>
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

      {searchRois.length > 0 && (
        <>
          <div className="param-section">라인별 에지 극성</div>
          {searchRois.map((r, i) => (
            <div className="param-row" key={r.id}>
              <span className="param-label">라인 {i + 1}</span>
              <select className="param-select" value={r.polarity ?? 'd2l'}
                onChange={e => setPolarity(r.id, e.target.value)}>
                <option value="d2l">흑 → 백 (어두움→밝음)</option>
                <option value="l2d">백 → 흑 (밝음→어두움)</option>
              </select>
            </div>
          ))}
        </>
      )}

      <div className="param-section">이진화</div>
      <div className="param-row">
        <span className="param-label">임계값 (raw)</span>
        <input className="param-input" type="number" step="1" value={threshold}
          onChange={e => emit({ threshold: parseFloat(e.target.value) || 0 })} />
      </div>
      <div className="param-empty" style={{ fontSize: 10 }}>
        각 검색 ROI에서 스캔 방향(화살표)으로 진행하며 첫 에지를 찾아 라인 중심을 출력합니다. ROI를 회전하면 대각선 에지도 검색됩니다.
      </div>
    </div>
  )
}
