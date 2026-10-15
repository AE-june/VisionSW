import RoiCanvas, { type Roi } from './RoiCanvas'
import { NoiseFilterParams } from './ParamPanel'

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

// 필터 적용 영역: 사각형만 (원/폴리곤 미지원)
const ROI_TYPES = [{ type: 'filter', label: '필터 영역' }]

export default function NoiseFilterEditor(props: Props) {
  const { params, preview, zMin, zMax, resXMm, resYMm, originCol, originRow, viewKey, onChange } = props
  const rois = (params.rois as Roi[]) ?? []

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
        영역을 지정하지 않으면 전체 이미지에 필터가 적용됩니다
      </div>

      <NoiseFilterParams params={params} onChange={onChange} />
    </div>
  )
}
