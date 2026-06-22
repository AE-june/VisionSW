import { useState, type ReactNode } from 'react'
import ImageViewer, { type DrawRect } from './ImageViewer'

export interface Roi {
  id: string
  type: string
  xPct: number
  yPct: number
  wPct: number
  hPct: number
}

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
}

let uidCounter = 0
const uid = () => `roi-${++uidCounter}`

export default function RoiCanvas({ rois, roiTypes, preview, onChange, overlayFor }: Props) {
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

  const deleteRoi = (id: string) => onChange(rois.filter(r => r.id !== id))
  const labelFor = (type: string) => roiTypes.find(t => t.type === type)?.label ?? type

  // 같은 type 내 인덱스 계산용 카운터
  const typeCounters: Record<string, number> = {}

  const overlay = rois.map(roi => {
    const idxInType = (typeCounters[roi.type] = (typeCounters[roi.type] ?? -1) + 1)
    return (
      <div
        key={roi.id}
        className={`pfe-roi pfe-roi-${roi.type}`}
        style={{
          left:   `${roi.xPct * 100}%`,
          top:    `${roi.yPct * 100}%`,
          width:  `${roi.wPct * 100}%`,
          height: `${roi.hPct * 100}%`,
        }}
      >
        <span className="pfe-roi-label">{labelFor(roi.type)} {idxInType + 1}</span>
        <button
          className="pfe-roi-del"
          onMouseDown={e => e.stopPropagation()}
          onClick={e => { e.stopPropagation(); deleteRoi(roi.id) }}
        >×</button>
        {overlayFor?.(roi, idxInType)}
      </div>
    )
  })

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
    {drawType && <span className="pfe-drawing-hint">드래그해서 영역 지정 · 다시 버튼 클릭 시 취소</span>}
  </>

  return (
    <ImageViewer
      preview={preview}
      drawMode={drawType}
      onDrawComplete={addRoi}
      overlay={overlay}
      toolbarLeft={toolbarLeft}
      footer={footer}
    />
  )
}
