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
  /** 이미지 좌표계 위 임의 오버레이 (예: 스캔 방향 화살표) */
  overlay?: ReactNode
  zMin?: number
  zMax?: number
  /** 원형 ROI 그리기 허용 (도형 토글 버튼 표시) */
  enableCircle?: boolean
  /** ROI 회전 허용 (회전 핸들 표시) */
  enableRotate?: boolean
  /** mm 단위 좌표 입력용 분해능 (mm/pixel). 있으면 px/mm 토글 표시 */
  resXMm?: number
  resYMm?: number
  /** 이미지 원본 픽셀 크기 콜백 */
  onImageSize?: (w: number, h: number) => void
}

let uidCounter = 0
// 레시피 로드로 들어온 기존 id와도 충돌하지 않도록 카운터 + 랜덤 접미사
const uid = () => `roi-${Date.now().toString(36)}-${++uidCounter}-${Math.random().toString(36).slice(2, 7)}`

export default function RoiCanvas({ rois, roiTypes, preview, onChange, overlayFor, overlay, zMin, zMax, enableCircle, enableRotate, resXMm, resYMm, onImageSize }: Props) {
  const [drawType, setDrawType] = useState<string | null>(null)
  const [drawShape, setDrawShape] = useState<'rect' | 'circle'>('rect')
  const [imgSize, setImgSize] = useState({ w: 0, h: 0 })
  const [unit, setUnit] = useState<'px' | 'mm'>('px')
  const [selType, setSelType] = useState<string>(roiTypes[0]?.type ?? '')

  const addRoi = (rect: DrawRect) => {
    if (!drawType) return
    const newRoi: Roi = { id: uid(), type: drawType, shape: drawShape, ...rect }
    const spec = roiTypes.find(t => t.type === drawType)
    const updated = spec?.single
      ? [...rois.filter(r => r.type !== drawType), newRoi]
      : [...rois, newRoi]
    onChange(updated)
    setDrawType(null)
  }

  const activeType = selType || roiTypes[0]?.type || ''
  const toolbarLeft = <>
    {roiTypes.length > 1 && (
      <select className="param-select" value={activeType}
        onChange={e => setSelType(e.target.value)}>
        {roiTypes.map(s => <option key={s.type} value={s.type}>{s.label}</option>)}
      </select>
    )}
    {enableCircle && (
      <select className="param-select" value={drawShape}
        onChange={e => setDrawShape(e.target.value as 'rect' | 'circle')}>
        <option value="rect">Rectangle</option>
        <option value="circle">Circle</option>
      </select>
    )}
    <button
      className={`pfe-btn${drawType ? ' active' : ''}`}
      title={drawType ? '그리기 취소' : 'ROI 추가 (드래그로 영역 지정)'}
      onClick={() => setDrawType(m => m ? null : activeType)}
    >{drawType ? '취소' : '+ 추가'}</button>
    {/* selType은 타입이 여러 개일 때만 노출되므로 단일 타입에선 미사용 경고 방지 */}
  </>

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

  const canMm = !!resXMm && !!resYMm
  const useMm = unit === 'mm' && canMm
  // pct → 표시값, 표시값 → pct (단위/분해능 반영)
  const fx = useMm ? imgSize.w * (resXMm ?? 1) : imgSize.w
  const fy = useMm ? imgSize.h * (resYMm ?? 1) : imgSize.h
  const disp = (pct: number, f: number) => f > 0 ? +(pct * f).toFixed(useMm ? 2 : 0) : 0
  const update = (id: string, field: 'xPct' | 'yPct' | 'wPct' | 'hPct', val: number, f: number) => {
    const pct = f > 0 ? Math.max(0, Math.min(1, val / f)) : 0
    onChange(rois.map(r => r.id === id ? { ...r, [field]: pct } : r))
  }
  // 원형 ROI: 바운딩 박스 ↔ 중심점/반지름 환산
  const clamp01 = (v: number) => Math.max(0, Math.min(1, v))
  const cCx = (r: Roi) => disp(r.xPct + r.wPct / 2, fx)
  const cCy = (r: Roi) => disp(r.yPct + r.hPct / 2, fy)
  const cR  = (r: Roi) => fx > 0 ? +(r.wPct * fx / 2).toFixed(useMm ? 2 : 0) : 0
  const updateCircle = (id: string, field: 'cx' | 'cy' | 'r', val: number) => {
    onChange(rois.map(r => {
      if (r.id !== id) return r
      if (field === 'cx') return { ...r, xPct: clamp01((fx > 0 ? val / fx : 0) - r.wPct / 2) }
      if (field === 'cy') return { ...r, yPct: clamp01((fy > 0 ? val / fy : 0) - r.hPct / 2) }
      // r: 반지름 변경 시 중심 고정
      const ccx = r.xPct + r.wPct / 2, ccy = r.yPct + r.hPct / 2
      const wPct = fx > 0 ? clamp01(2 * val / fx) : r.wPct
      const hPct = fy > 0 ? clamp01(2 * val / fy) : r.hPct
      return { ...r, wPct, hPct, xPct: clamp01(ccx - wPct / 2), yPct: clamp01(ccy - hPct / 2) }
    }))
  }
  const setShape = (id: string, shape: 'rect' | 'circle') =>
    onChange(rois.map(r => r.id === id ? { ...r, shape } : r))

  // 도형별 입력 필드 정의 — 도형마다 파라미터 이름/개수가 달라도 각 행이 스스로 설명
  const fieldsFor = (roi: Roi): { label: string; value: number; set: (v: number) => void }[] =>
    roi.shape === 'circle'
      ? [
          { label: 'Cx', value: cCx(roi), set: v => updateCircle(roi.id, 'cx', v) },
          { label: 'Cy', value: cCy(roi), set: v => updateCircle(roi.id, 'cy', v) },
          { label: 'R',  value: cR(roi),  set: v => updateCircle(roi.id, 'r',  v) },
        ]
      : [
          { label: 'X', value: disp(roi.xPct, fx), set: v => update(roi.id, 'xPct', v, fx) },
          { label: 'Y', value: disp(roi.yPct, fy), set: v => update(roi.id, 'yPct', v, fy) },
          { label: 'W', value: disp(roi.wPct, fx), set: v => update(roi.id, 'wPct', v, fx) },
          { label: 'H', value: disp(roi.hPct, fy), set: v => update(roi.id, 'hPct', v, fy) },
        ]

  return (
    <div>
      <ImageViewer
        preview={preview}
        zMin={zMin}
        zMax={zMax}
        drawMode={drawType}
        drawShape={drawShape}
        onDrawComplete={addRoi}
        onImageSize={(w, h) => { setImgSize({ w, h }); onImageSize?.(w, h) }}
        rois={rois}
        onRoisChange={onChange}
        roiTypeLabel={() => 'ROI'}
        overlayFor={overlayFor}
        overlay={overlay}
        toolbarLeft={toolbarLeft}
        footer={footer}
        enableRotate={enableRotate}
      />

      {rois.length > 0 && (
        <div className="roi-coord-list">
          <div className="roi-coord-head">
            <span>영역 좌표</span>
            {canMm && (
              <span className="roi-unit-toggle">
                <button className={`pfe-btn${unit === 'px' ? ' active' : ''}`} onClick={() => setUnit('px')}>px</button>
                <button className={`pfe-btn${unit === 'mm' ? ' active' : ''}`} onClick={() => setUnit('mm')}>mm</button>
              </span>
            )}
          </div>
          {rois.map((roi, i, arr) => {
            const idxInType = arr.slice(0, i).filter(r => r.type === roi.type).length
            return (
              <div className="roi-coord-row" key={roi.id}>
                <span className="roi-coord-idx">{idxInType + 1}</span>
                {enableCircle && (
                  <select className="param-select roi-shape-sel" value={roi.shape ?? 'rect'}
                    onChange={e => setShape(roi.id, e.target.value as 'rect' | 'circle')}>
                    <option value="rect">Rectangle</option>
                    <option value="circle">Circle</option>
                  </select>
                )}
                {fieldsFor(roi).map(f => (
                  <label className="roi-field" key={f.label}>
                    <span className="roi-field-label">{f.label}</span>
                    <input type="number" value={f.value} onChange={e => f.set(+e.target.value)} />
                  </label>
                ))}
                <button className="roi-coord-del" onClick={() => onChange(rois.filter(r => r.id !== roi.id))}>×</button>
              </div>
            )
          })}
        </div>
      )}
    </div>
  )
}
