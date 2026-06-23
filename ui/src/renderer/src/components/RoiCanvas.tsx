import { useState, type ReactNode } from 'react'
import ImageViewer, { type Roi, type DrawRect } from './ImageViewer'

export type { Roi } from './ImageViewer'

export interface RoiTypeSpec {
  type: string       // 'ref' | 'measure' 등
  label: string      // 버튼/라벨 텍스트
  single?: boolean   // true면 해당 타입은 1개만 유지
}

interface Props {
  rois: Roi[]
  roiTypes: RoiTypeSpec[]
  preview?: string
  onChange: (rois: Roi[]) => void
  /** ROI별 추가 오버레이 (예: 측정 결과). 같은 type 내 0-based index 전달 */
  overlayFor?: (roi: Roi, indexInType: number) => ReactNode
  zMin?: number
  zMax?: number
}

let uidCounter = 0
const uid = () => `roi-${++uidCounter}`

export default function RoiCanvas({ rois, roiTypes, preview, onChange, overlayFor, zMin, zMax }: Props) {
  const [drawType, setDrawType] = useState<string | null>(null)

  const addRoi = (rect: DrawRect) => {
    if (!drawType) return
    const newRoi: Roi = { id: uid(), type: drawType, ...rect }
    const spec = roiTypes.find(t => t.type === drawType)
    const updated = spec?.single
      ? [...rois.filter(r => r.type !== drawType), newRoi]
      : [...rois, newRoi]
    onChange(updated)
    setDrawType(null)
  }

  const toolbarLeft = roiTypes.map(spec => (
    <button
      key={spec.type}
      className={`pfe-btn ${spec.type}${drawType === spec.type ? ' active' : ''}`}
      onClick={() => setDrawType(m => m === spec.type ? null : spec.type)}
    >+ {spec.label}</button>
  ))

  const footer = <>
    {roiTypes.map(spec => (
      <span key={spec.type} className={`pfe-roi-count ${spec.type}`}>
        {spec.label}: {rois.filter(r => r.type === spec.type).length}{spec.single ? '/1' : ''}
      </span>
    ))}
    {drawType
      ? <span className="pfe-drawing-hint">드래그해서 영역 지정 · 다시 버튼 클릭 시 취소</span>
      : <span className="pfe-drawing-hint">ROI를 드래그하면 이동, 모서리를 끌면 크기 조정</span>}
  </>

  return (
    <ImageViewer
      preview={preview}
      zMin={zMin}
      zMax={zMax}
      drawMode={drawType}
      onDrawComplete={addRoi}
      rois={rois}
      onRoisChange={onChange}
      roiTypeLabel={() => 'ROI'}
      overlayFor={overlayFor}
      toolbarLeft={toolbarLeft}
      footer={footer}
    />
  )
}
