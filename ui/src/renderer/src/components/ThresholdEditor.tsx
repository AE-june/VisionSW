import ImageViewer from './ImageViewer'

export interface ThresholdSettings {
  channel: number
  thresholdMm: number
  keepAbove: boolean
}

interface Props extends ThresholdSettings {
  preview?: string
  zMin?: number
  zMax?: number
  resXMm?: number
  resYMm?: number
  viewKey?: string
  onChange: (next: ThresholdSettings) => void
}

// Threshold: 채널 높이(mm)에 임계값을 적용해 Region(마스크) 생산.
// 상류 preview를 참고용으로 표시하고, 결과 마스크는 결과 탭에서 확인.
export default function ThresholdEditor(props: Props) {
  const { channel, thresholdMm, keepAbove, preview, zMin, zMax, resXMm, resYMm, viewKey, onChange } = props
  const emit = (patch: Partial<ThresholdSettings>) =>
    onChange({ channel, thresholdMm, keepAbove, ...patch })

  return (
    <div>
      {preview && (
        <ImageViewer
          preview={preview}
          zMin={zMin}
          zMax={zMax}
          resXMm={resXMm}
          resYMm={resYMm}
          viewKey={viewKey}
          canvasHeight={280}
        />
      )}
      <div className="param-section">임계값 (Image → Region)</div>
      <div className="param-row">
        <span className="param-label">기준 높이 (mm)</span>
        <input className="param-input" type="number" step="0.001" value={thresholdMm}
          onChange={e => emit({ thresholdMm: parseFloat(e.target.value) || 0 })} />
      </div>
      <div className="param-row">
        <span className="param-label">방향</span>
        <select className="param-select" value={keepAbove ? 'above' : 'below'}
          onChange={e => emit({ keepAbove: e.target.value === 'above' })}>
          <option value="above">이상 (z ≥ 기준)</option>
          <option value="below">이하 (z ≤ 기준)</option>
        </select>
      </div>
      <div className="param-row">
        <span className="param-label">채널</span>
        <input className="param-input" type="number" step="1" min="0" value={channel}
          onChange={e => emit({ channel: parseInt(e.target.value) || 0 })} />
      </div>
    </div>
  )
}
