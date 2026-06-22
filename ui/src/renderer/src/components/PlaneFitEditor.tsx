import RoiCanvas, { type Roi } from './RoiCanvas'

export type PlaneFitROI = Roi

interface Props {
  rois: Roi[]
  algorithm: string
  ransacThreshold: number
  ransacIterations: number
  preview?: string
  onChange: (rois: Roi[], algo: string, threshold: number, iterations: number) => void
}

const ROI_TYPES = [{ type: 'ref', label: 'Reference' }]

export default function PlaneFitEditor({
  rois, algorithm, ransacThreshold, ransacIterations, preview, onChange
}: Props) {
  return (
    <div>
      <RoiCanvas
        rois={rois}
        roiTypes={ROI_TYPES}
        preview={preview}
        onChange={r => onChange(r, algorithm, ransacThreshold, ransacIterations)}
      />

      <div className="param-section">알고리즘</div>
      <div className="param-row">
        <span className="param-label">Method</span>
        <select className="param-select" value={algorithm}
          onChange={e => onChange(rois, e.target.value, ransacThreshold, ransacIterations)}>
          <option value="LeastSquares">Least Squares</option>
          <option value="RANSAC">RANSAC</option>
          <option value="SVD">SVD (PCA)</option>
        </select>
      </div>
      {algorithm === 'RANSAC' && <>
        <div className="param-row">
          <span className="param-label">Threshold (mm)</span>
          <input className="param-input" type="number" step="0.001" value={ransacThreshold}
            onChange={e => onChange(rois, algorithm, parseFloat(e.target.value) || 0, ransacIterations)} />
        </div>
        <div className="param-row">
          <span className="param-label">Iterations</span>
          <input className="param-input" type="number" step="10" value={ransacIterations}
            onChange={e => onChange(rois, algorithm, ransacThreshold, parseInt(e.target.value) || 0)} />
        </div>
      </>}
    </div>
  )
}
