import RoiCanvas, { type Roi } from './RoiCanvas'

export interface HeightFromPlaneSettings {
  rois: Roi[]
  aggregation: string
  highTailPct: number
  useTolerance: boolean
  nominalMm: number
  toleranceMm: number
}

export interface MeasureResult {
  distance: number; pointCount: number; pass: boolean
}

interface Props extends HeightFromPlaneSettings {
  preview?: string
  measures?: MeasureResult[]
  onChange: (next: HeightFromPlaneSettings) => void
}

const ROI_TYPES = [{ type: 'measure', label: 'Measure' }]

export default function HeightFromPlaneEditor(props: Props) {
  const { rois, aggregation, highTailPct, useTolerance, nominalMm, toleranceMm, preview, measures, onChange } = props

  const emit = (patch: Partial<HeightFromPlaneSettings>) =>
    onChange({ rois, aggregation, highTailPct, useTolerance, nominalMm, toleranceMm, ...patch })

  return (
    <div>
      <RoiCanvas
        rois={rois}
        roiTypes={ROI_TYPES}
        preview={preview}
        onChange={r => emit({ rois: r })}
        overlayFor={(roi, idx) => {
          if (roi.type !== 'measure') return null
          const m = measures?.[idx]
          if (!m) return null
          return (
            <span className={`pfe-roi-result ${m.pass ? 'pass' : 'fail'}`}>
              {m.pointCount === 0 ? '빈 ROI' : `${m.distance.toFixed(4)} mm`}
            </span>
          )
        }}
      />

      <div className="param-section">Z 추출 방식</div>
      <div className="param-row">
        <span className="param-label">Aggregation</span>
        <select className="param-select" value={aggregation}
          onChange={e => emit({ aggregation: e.target.value })}>
          <option value="Mean">Mean (평균)</option>
          <option value="Max">Max (최대)</option>
          <option value="HighTail">HighTail (상위 %)</option>
        </select>
      </div>
      {aggregation === 'HighTail' && (
        <div className="param-row">
          <span className="param-label">High Tail (%)</span>
          <input className="param-input" type="number" step="1" min="1" max="100" value={highTailPct}
            onChange={e => emit({ highTailPct: parseFloat(e.target.value) || 0 })} />
        </div>
      )}

      <div className="param-section">합부 판정</div>
      <div className="param-row">
        <span className="param-label">Tolerance 사용</span>
        <input type="checkbox" checked={useTolerance}
          onChange={e => emit({ useTolerance: e.target.checked })} />
      </div>
      {useTolerance && <>
        <div className="param-row">
          <span className="param-label">기준 거리 (mm)</span>
          <input className="param-input" type="number" step="0.001" value={nominalMm}
            onChange={e => emit({ nominalMm: parseFloat(e.target.value) || 0 })} />
        </div>
        <div className="param-row">
          <span className="param-label">허용 오차 (± mm)</span>
          <input className="param-input" type="number" step="0.001" value={toleranceMm}
            onChange={e => emit({ toleranceMm: parseFloat(e.target.value) || 0 })} />
        </div>
      </>}
    </div>
  )
}
