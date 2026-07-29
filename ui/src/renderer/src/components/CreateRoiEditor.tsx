import RoiCanvas, { type Roi } from './RoiCanvas'

export interface CreateRoiSettings {
  rois: Roi[]
}

interface Props extends CreateRoiSettings {
  preview?: string
  zMin?: number
  zMax?: number
  resXMm?: number
  resYMm?: number
  originCol?: number
  originRow?: number
  viewKey?: string
  onChange: (next: CreateRoiSettings) => void
}

const ROI_TYPES = [{ type: 'roi', label: '영역' }]

// CreateROI: 사각/원/폴리곤 ROI를 그려 Region(마스크) 생산. 여러 ROI는 합집합.
export default function CreateRoiEditor(props: Props) {
  const { rois, preview, zMin, zMax, resXMm, resYMm, originCol, originRow, viewKey, onChange } = props
  return (
    <div>
      <div className="param-section">영역 그리기 (합집합)</div>
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
        enableCircle
        enablePolygon
        enableRotate
        onChange={r => onChange({ rois: r })}
      />
    </div>
  )
}
