import RoiCanvas, { type Roi } from './RoiCanvas'

interface Props {
  params: Record<string, unknown>
  preview?: string
  zMin?: number
  zMax?: number
  resXMm?: number
  resYMm?: number
  originCol?: number
  originRow?: number
  viewKey?: string
  onChange: (next: Record<string, unknown>) => void
}

// 늘릴 영역(세로 밴드). 사각형으로 그리되 세로(y) 구간만 의미가 있고 가로 전체에 적용된다.
const ROI_TYPES = [{ type: 'stretch', label: '늘릴 영역' }]

type StretchRoi = Roi & { scale?: number }

export default function RowStretchEditor(props: Props) {
  const { params, preview, zMin, zMax, resXMm, resYMm, originCol, originRow, viewKey, onChange } = props
  const rois = (params.rois as StretchRoi[]) ?? []

  const setScale = (id: string, scale: number) =>
    onChange({ ...params, rois: rois.map(r => (r.id === id ? { ...r, scale } : r)) })

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
        onChange={r => onChange({ ...params, rois: r })}
      />
      <div className="param-empty" style={{ fontSize: 10 }}>
        지정한 세로 구간(가로 전체)의 행을 배수만큼 선형보간으로 늘립니다. 구간 밖은 그대로이며,
        늘린 만큼 이미지 세로가 커집니다. 영역이 없으면 변화 없음.
      </div>

      {rois.length > 0 && (
        <div className="roi-coord-list">
          <div className="roi-coord-head"><span>영역별 배수 (×N)</span></div>
          <div className="roi-coord-rows">
            {rois.map((roi, i) => (
              <div className="roi-coord-row" key={roi.id}>
                <span className="roi-coord-idx">{i + 1}</span>
                <label className="roi-field">
                  <span className="roi-field-label">배수</span>
                  <input
                    type="number"
                    min={1}
                    step={1}
                    value={roi.scale ?? 2}
                    onChange={e => setScale(roi.id, Math.max(1, parseInt(e.target.value, 10) || 1))}
                  />
                </label>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}
