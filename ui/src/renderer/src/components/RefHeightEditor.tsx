import RoiCanvas, { type Roi } from './RoiCanvas'

export type RefHeightROI = Roi

export interface RefHeightSettings {
  rois: Roi[]
  mode: string
  sorSigma: number
  lowTailPct: number
  highTailPct: number
}

interface Props extends RefHeightSettings {
  preview?: string
  zMin?: number
  zMax?: number
  resXMm?: number
  resYMm?: number
  originCol?: number
  originRow?: number
  viewKey?: string
  onChange: (next: RefHeightSettings) => void
}

const ROI_TYPES = [{ type: 'ref', label: 'Reference' }]

export default function RefHeightEditor(props: Props) {
  const { rois, mode, sorSigma, lowTailPct, highTailPct, preview, zMin, zMax, resXMm, resYMm, originCol, originRow, viewKey, onChange } = props

  const emit = (patch: Partial<RefHeightSettings>) =>
    onChange({ rois, mode, sorSigma, lowTailPct, highTailPct, ...patch })

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
        originCol={originCol}
        originRow={originRow}
        viewKey={viewKey}
        onChange={r => emit({ rois: r })}
      />

      <div className="param-section">아웃라이어 제거</div>
      <div className="param-row">
        <span className="param-label">Mode</span>
        <select className="param-select" value={mode}
          onChange={e => emit({ mode: e.target.value })}>
          <option value="sor">SOR (표준편차 기준)</option>
          <option value="percentileTrim">Percentile Trim</option>
        </select>
      </div>
      {mode === 'sor' ? (
        <div className="param-row">
          <span className="param-label" title="전체 ROI 표본의 평균에서 이 배수(σ) 이상 벗어난 점을 제거. 작을수록 엄격.">Sigma (σ)</span>
          <input className="param-input" type="number" step="0.1" min="0" value={sorSigma}
            onChange={e => emit({ sorSigma: parseFloat(e.target.value) || 0 })} />
        </div>
      ) : (
        <>
          <div className="param-row">
            <span className="param-label" title="정렬된 Z값 중 하위 이 비율(%)만큼 절삭 후 평균.">Low Tail (%)</span>
            <input className="param-input" type="number" step="1" min="0" max="100" value={lowTailPct}
              onChange={e => emit({ lowTailPct: parseFloat(e.target.value) || 0 })} />
          </div>
          <div className="param-row">
            <span className="param-label" title="정렬된 Z값 중 상위 이 비율(%)만큼 절삭 후 평균.">High Tail (%)</span>
            <input className="param-input" type="number" step="1" min="0" max="100" value={highTailPct}
              onChange={e => emit({ highTailPct: parseFloat(e.target.value) || 0 })} />
          </div>
        </>
      )}
    </div>
  )
}
