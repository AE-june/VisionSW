import { useState, useRef, type ReactNode } from 'react'
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
  /** 이미지 좌표계 위 임의 오버레이 (예: 스캔 방향 화살표). 함수면 현재 zoom 전달 */
  overlay?: ReactNode | ((zoom: number) => ReactNode)
  zMin?: number
  zMax?: number
  /** 원형 ROI 그리기 허용 (도형 토글 버튼 표시) */
  enableCircle?: boolean
  /** ROI 회전 허용 (회전 핸들 표시) */
  enableRotate?: boolean
  /** mm 단위 좌표 입력용 분해능 (mm/pixel). 있으면 px/mm 토글 표시 */
  resXMm?: number
  resYMm?: number
  /** 원점 오프셋 (픽셀). 설정된 경우 좌표 표시를 원점 기준 상대값으로 보여줌 */
  originCol?: number
  originRow?: number
  /** 이미지 원본 픽셀 크기 콜백 */
  onImageSize?: (w: number, h: number) => void
}

let uidCounter = 0
// 레시피 로드로 들어온 기존 id와도 충돌하지 않도록 카운터 + 랜덤 접미사
const uid = () => `roi-${Date.now().toString(36)}-${++uidCounter}-${Math.random().toString(36).slice(2, 7)}`

// 좌표 입력 필드: 편집 중에는 로컬 문자열 버퍼를 그대로 보여줘 자유롭게 지우고 다시 입력할 수 있게 한다.
// (컨트롤드 number input이 공백을 0으로 강제 되돌리던 문제 해결) 공백으로 확정하면 0.
function NumField({ value, onCommit, step = 1 }: { value: number; onCommit: (v: number) => void; step?: number }) {
  const [buf, setBuf] = useState<string | null>(null)
  return (
    <input
      type="number"
      step={step}
      value={buf ?? value}
      onChange={e => {
        const t = e.target.value
        setBuf(t)
        if (t === '' || t === '-' || t === '.' || t === '-.') return   // 편집 중 미완성 값은 커밋 보류
        const n = Number(t)
        if (!Number.isNaN(n)) onCommit(n)
      }}
      onBlur={() => {
        if (buf === '' || buf === '-' || buf === '.' || buf === '-.') onCommit(0)   // 공백 확정 → 0
        setBuf(null)   // 정규(반올림)값 표시로 복귀
      }}
    />
  )
}

export default function RoiCanvas({ rois, roiTypes, preview, onChange, overlayFor, overlay, zMin, zMax, enableCircle, enableRotate, resXMm, resYMm, originCol, originRow, onImageSize }: Props) {
  const [drawType, setDrawType] = useState<string | null>(null)
  const [drawShape, setDrawShape] = useState<'rect' | 'circle'>('rect')
  const [imgSize, setImgSize] = useState({ w: 0, h: 0 })
  const [unit, setUnit] = useState<'px' | 'mm'>('px')
  const [selType, setSelType] = useState<string>(roiTypes[0]?.type ?? '')
  // 디스플레이(캔버스) 높이 — 구분선 드래그로 좌표 패널과 높이 배분 조절
  const [dispH, setDispH] = useState(420)
  const dragRef = useRef<{ startY: number; startH: number } | null>(null)
  const startHDrag = (e: React.MouseEvent) => {
    e.preventDefault()
    dragRef.current = { startY: e.clientY, startH: dispH }
    const onMove = (ev: MouseEvent) => {
      if (!dragRef.current) return
      const h = dragRef.current.startH + (ev.clientY - dragRef.current.startY)
      setDispH(Math.max(150, Math.min(1000, h)))
    }
    const onUp = () => {
      dragRef.current = null
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }

  // 저장 좌표는 원점(Align 검출) 기준 상대값. 캔버스(ImageViewer)는 절대 좌표로 그리므로
  // 렌더링 직전 원점(pct)을 더하고, 변경/생성 결과는 다시 빼서 상대값으로 저장한다.
  const oPctX = originCol != null && imgSize.w > 0 ? originCol / imgSize.w : 0
  const oPctY = originRow != null && imgSize.h > 0 ? originRow / imgSize.h : 0
  const toAbs = (r: Roi): Roi => ({ ...r, xPct: r.xPct + oPctX, yPct: r.yPct + oPctY })
  const toRel = (r: Roi): Roi => ({ ...r, xPct: r.xPct - oPctX, yPct: r.yPct - oPctY })

  const addRoi = (rect: DrawRect) => {
    if (!drawType) return
    const newRoi: Roi = { id: uid(), type: drawType, shape: drawShape, ...rect, xPct: rect.xPct - oPctX, yPct: rect.yPct - oPctY }
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
  // pct(원점 상대) → 표시값. 저장값이 이미 원점 기준이라 분해능만 곱하면 mm.
  const fx = useMm ? imgSize.w * (resXMm ?? 1) : imgSize.w
  const fy = useMm ? imgSize.h * (resYMm ?? 1) : imgSize.h
  const disp = (pct: number, f: number) => f > 0 ? +(pct * f).toFixed(useMm ? 2 : 0) : 0
  const update = (id: string, field: 'xPct' | 'yPct' | 'wPct' | 'hPct', val: number, f: number) => {
    const pct = f > 0 ? val / f : 0
    onChange(rois.map(r => r.id === id ? { ...r, [field]: pct } : r))
  }
  // 원형 ROI: 바운딩 박스 ↔ 중심점/반지름 환산 (위치는 음수 허용, 크기만 0~1 클램프)
  const clamp01 = (v: number) => Math.max(0, Math.min(1, v))
  const cCx = (r: Roi) => disp(r.xPct + r.wPct / 2, fx)
  const cCy = (r: Roi) => disp(r.yPct + r.hPct / 2, fy)
  const cR  = (r: Roi) => fx > 0 ? +(r.wPct * fx / 2).toFixed(useMm ? 2 : 0) : 0
  const updateCircle = (id: string, field: 'cx' | 'cy' | 'r', val: number) => {
    onChange(rois.map(r => {
      if (r.id !== id) return r
      if (field === 'cx') return { ...r, xPct: (fx > 0 ? val / fx : 0) - r.wPct / 2 }
      if (field === 'cy') return { ...r, yPct: (fy > 0 ? val / fy : 0) - r.hPct / 2 }
      // r: 반지름 변경 시 중심 고정
      const ccx = r.xPct + r.wPct / 2, ccy = r.yPct + r.hPct / 2
      const wPct = fx > 0 ? clamp01(2 * val / fx) : r.wPct
      const hPct = fy > 0 ? clamp01(2 * val / fy) : r.hPct
      return { ...r, wPct, hPct, xPct: ccx - wPct / 2, yPct: ccy - hPct / 2 }
    }))
  }
  const setShape = (id: string, shape: 'rect' | 'circle') =>
    onChange(rois.map(r => r.id === id ? { ...r, shape } : r))

  // 각도(deg) 입력 — 회전 허용(enableRotate) 시에만 노출. 중심 고정, deg 단위.
  const setAngle = (id: string, deg: number) =>
    onChange(rois.map(r => r.id === id ? { ...r, angleDeg: deg } : r))
  const angleField = (roi: Roi) => ({ label: 'A°', value: +(roi.angleDeg ?? 0).toFixed(1), set: (v: number) => setAngle(roi.id, v) })

  // 사각형 중심(Cx,Cy) 기준 편집 — 회전된 ROI에서 좌상단 좌표는 실제 위치와 안 맞으므로
  // 회전 가능한 편집기에선 중심 기준으로 표시(회전 무관하게 실제 위치 반영). W/H는 중심 고정.
  const rCx = (r: Roi) => disp(r.xPct + r.wPct / 2, fx)
  const rCy = (r: Roi) => disp(r.yPct + r.hPct / 2, fy)
  const updateRectCenter = (id: string, field: 'cx' | 'cy', val: number) => {
    onChange(rois.map(r => {
      if (r.id !== id) return r
      if (field === 'cx') return { ...r, xPct: (fx > 0 ? val / fx : 0) - r.wPct / 2 }
      return { ...r, yPct: (fy > 0 ? val / fy : 0) - r.hPct / 2 }
    }))
  }
  // 화면(스크린 축) 기준 W/H: 회전된 ROI가 화면에서 차지하는 가로/세로 = 회전 박스의 AABB.
  // 픽셀 공간에서 계산(회전은 픽셀 기준). 표시는 px/mm.
  const rectAabbPx = (r: Roi) => {
    const th = ((r.angleDeg ?? 0) * Math.PI) / 180
    const c = Math.abs(Math.cos(th)), s = Math.abs(Math.sin(th))
    const Wpx = r.wPct * imgSize.w, Hpx = r.hPct * imgSize.h
    return { wPx: Wpx * c + Hpx * s, hPx: Wpx * s + Hpx * c }
  }
  const aW = (r: Roi) => { const p = rectAabbPx(r).wPx; return +(useMm ? p * (resXMm ?? 1) : p).toFixed(useMm ? 2 : 0) }
  const aH = (r: Roi) => { const p = rectAabbPx(r).hPx; return +(useMm ? p * (resYMm ?? 1) : p).toFixed(useMm ? 2 : 0) }
  const setAabb = (id: string, field: 'w' | 'h', val: number) => {
    onChange(rois.map(r => {
      if (r.id !== id) return r
      const th = ((r.angleDeg ?? 0) * Math.PI) / 180
      const c = Math.abs(Math.cos(th)), s = Math.abs(Math.sin(th))
      const det = c * c - s * s
      if (Math.abs(det) < 0.06) return r   // 45° 부근: AABB 역변환 불안정 → 무시
      const cur = rectAabbPx(r)
      const valPx = useMm ? val / (field === 'w' ? (resXMm ?? 1) : (resYMm ?? 1)) : val
      const tW = field === 'w' ? valPx : cur.wPx
      const tH = field === 'h' ? valPx : cur.hPx
      const Wpx = Math.max(1, Math.abs((tW * c - tH * s) / det))
      const Hpx = Math.max(1, Math.abs((tH * c - tW * s) / det))
      const w = imgSize.w > 0 ? Wpx / imgSize.w : r.wPct
      const h = imgSize.h > 0 ? Hpx / imgSize.h : r.hPct
      const ccx = r.xPct + r.wPct / 2, ccy = r.yPct + r.hPct / 2
      return { ...r, wPct: w, hPct: h, xPct: ccx - w / 2, yPct: ccy - h / 2 }
    }))
  }

  // 도형별 입력 필드 정의 — 도형마다 파라미터 이름/개수가 달라도 각 행이 스스로 설명
  const fieldsFor = (roi: Roi): { label: string; value: number; set: (v: number) => void }[] => {
    if (roi.shape === 'circle')
      return [
        { label: 'Cx', value: cCx(roi), set: (v: number) => updateCircle(roi.id, 'cx', v) },
        { label: 'Cy', value: cCy(roi), set: (v: number) => updateCircle(roi.id, 'cy', v) },
        { label: 'R',  value: cR(roi),  set: (v: number) => updateCircle(roi.id, 'r',  v) },
      ]
    // 회전 가능 편집기: 중심(Cx,Cy) + 화면 기준 폭/높이(AABB) + 각도.
    if (enableRotate)
      return [
        { label: 'Cx', value: rCx(roi), set: (v: number) => updateRectCenter(roi.id, 'cx', v) },
        { label: 'Cy', value: rCy(roi), set: (v: number) => updateRectCenter(roi.id, 'cy', v) },
        { label: 'W',  value: aW(roi),  set: (v: number) => setAabb(roi.id, 'w', v) },
        { label: 'H',  value: aH(roi),  set: (v: number) => setAabb(roi.id, 'h', v) },
        angleField(roi),
      ]
    return [
      { label: 'X', value: disp(roi.xPct, fx), set: (v: number) => update(roi.id, 'xPct', v, fx) },
      { label: 'Y', value: disp(roi.yPct, fy), set: (v: number) => update(roi.id, 'yPct', v, fy) },
      { label: 'W', value: disp(roi.wPct, fx), set: (v: number) => update(roi.id, 'wPct', v, fx) },
      { label: 'H', value: disp(roi.hPct, fy), set: (v: number) => update(roi.id, 'hPct', v, fy) },
    ]
  }

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
        rois={rois.map(toAbs)}
        onRoisChange={next => onChange(next.map(toRel))}
        roiTypeLabel={() => 'ROI'}
        overlayFor={overlayFor}
        overlay={overlay}
        canvasHeight={dispH}
        toolbarLeft={toolbarLeft}
        footer={footer}
        enableRotate={enableRotate}
        resXMm={resXMm}
        resYMm={resYMm}
      />

      {rois.length > 0 && (
        <div className="pfe-hsplit" onMouseDown={startHDrag} title="드래그하여 디스플레이/좌표 패널 높이 조절">
          <span className="pfe-hsplit-grip" />
        </div>
      )}

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
          <div className="roi-coord-rows">
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
                    <NumField value={f.value} onCommit={f.set} step={useMm ? 0.01 : 1} />
                  </label>
                ))}
                <button className="roi-coord-del" onClick={() => onChange(rois.filter(r => r.id !== roi.id))}>×</button>
              </div>
            )
          })}
          </div>
        </div>
      )}
    </div>
  )
}
