import RoiCanvas, { type Roi } from './RoiCanvas'

export type PlaneFitROI = Roi

export interface PlaneFitSettings {
  rois: Roi[]
  algorithm: string
  ransacThreshold: number
  ransacIterations: number
  maxCloudPoints: number
}

interface Props extends PlaneFitSettings {
  preview?: string
  zMin?: number
  zMax?: number
  resXMm?: number
  resYMm?: number
  originCol?: number
  originRow?: number
  viewKey?: string
  onChange: (next: PlaneFitSettings) => void
}

const ROI_TYPES = [{ type: 'ref', label: 'Reference' }]

export default function PlaneFitEditor(props: Props) {
  const { rois, algorithm, ransacThreshold, ransacIterations, maxCloudPoints, preview, zMin, zMax, resXMm, resYMm, originCol, originRow, viewKey, onChange } = props

  const emit = (patch: Partial<PlaneFitSettings>) =>
    onChange({ rois, algorithm, ransacThreshold, ransacIterations, maxCloudPoints, ...patch })

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

      <div className="param-section">알고리즘</div>
      <div className="param-row">
        <span className="param-label">Method</span>
        <select className="param-select" value={algorithm}
          onChange={e => emit({ algorithm: e.target.value })}>
          <option value="LeastSquares">Least Squares</option>
          <option value="RANSAC">RANSAC</option>
          <option value="SVD">SVD (PCA)</option>
        </select>
      </div>
      {algorithm === 'RANSAC' && <>
        <div className="param-row">
          <span className="param-label" title="점에서 후보 평면까지의 수직거리가 이 값 이하면 인라이어로 취급. 작을수록 엄격. 단위: mm">Threshold (mm)</span>
          <input className="param-input" type="number" step="0.001" value={ransacThreshold}
            onChange={e => emit({ ransacThreshold: parseFloat(e.target.value) || 0 })} />
        </div>
        <div className="param-row">
          <span className="param-label" title="RANSAC 반복 횟수. 많을수록 정확하지만 느림. 보통 100~500.">Iterations</span>
          <input className="param-input" type="number" step="10" value={ransacIterations}
            onChange={e => emit({ ransacIterations: parseInt(e.target.value) || 0 })} />
        </div>
      </>}

      <div className="param-section">3D 뷰</div>
      <div className="param-row">
        <span className="param-label">최대 포인트 수 (≤50만)</span>
        <input className="param-input" type="number" step="10000" min="1000" max="500000" value={maxCloudPoints}
          onChange={e => emit({ maxCloudPoints: Math.min(500000, parseInt(e.target.value) || 1000) })} />
      </div>
    </div>
  )
}
