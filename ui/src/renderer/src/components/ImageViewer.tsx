import { useState, useRef, useCallback, useEffect, type ReactNode } from 'react'

// jet 컬러맵: 0(낮음, 파랑) → 청록 → 녹색 → 노랑 → 1(높음, 빨강)
const clamp01 = (x: number) => Math.max(0, Math.min(1, x))
function jet(t: number): [number, number, number] {
  return [
    clamp01(1.5 - Math.abs(4 * t - 3)) * 255,
    clamp01(1.5 - Math.abs(4 * t - 2)) * 255,
    clamp01(1.5 - Math.abs(4 * t - 1)) * 255,
  ]
}

export interface DrawRect { xPct: number; yPct: number; wPct: number; hPct: number }

export interface Roi {
  id: string
  type: string
  xPct: number
  yPct: number
  wPct: number
  hPct: number
}

interface DrawState { startX: number; startY: number; curX: number; curY: number }
interface Pan { x: number; y: number }

type EditMode = 'move' | 'resize'
interface EditState { id: string; mode: EditMode; handle: string; startX: number; startY: number; orig: Roi }

const HANDLES = ['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w']
const clampUnit = (v: number) => Math.max(0, Math.min(1, v))

function applyEdit(e: EditState, x: number, y: number): Roi {
  const o = e.orig
  if (e.mode === 'move') {
    const dx = x - e.startX, dy = y - e.startY
    return {
      ...o,
      xPct: Math.max(0, Math.min(1 - o.wPct, o.xPct + dx)),
      yPct: Math.max(0, Math.min(1 - o.hPct, o.yPct + dy)),
    }
  }
  // resize: 핸들에 포함된 방향의 모서리만 마우스 위치로 이동
  let left = o.xPct, top = o.yPct, right = o.xPct + o.wPct, bottom = o.yPct + o.hPct
  if (e.handle.includes('w')) left = clampUnit(x)
  if (e.handle.includes('e')) right = clampUnit(x)
  if (e.handle.includes('n')) top = clampUnit(y)
  if (e.handle.includes('s')) bottom = clampUnit(y)
  return {
    ...o,
    xPct: Math.min(left, right),
    yPct: Math.min(top, bottom),
    wPct: Math.max(0.005, Math.abs(right - left)),
    hPct: Math.max(0.005, Math.abs(bottom - top)),
  }
}

interface Props {
  preview?: string
  /** null/undefined면 팬 모드. 문자열이면 해당 타입 이름으로 ROI 그리기 (프리뷰 색상에 사용) */
  drawMode?: string | null
  onDrawComplete?: (rect: DrawRect) => void
  /** 표시할 ROI 목록 */
  rois?: Roi[]
  /** ROI 편집(이동/리사이즈/삭제) 활성화 — 변경 콜백. 없으면 읽기전용 */
  onRoisChange?: (rois: Roi[]) => void
  /** ROI 타입 → 라벨 텍스트 */
  roiTypeLabel?: (type: string) => string
  /** ROI별 추가 오버레이 (결과 거리 등). 같은 type 내 0-based index */
  overlayFor?: (roi: Roi, indexInType: number) => ReactNode
  /** 이미지 좌표계 위에 렌더되는 임의 오버레이 */
  overlay?: ReactNode
  /** 툴바 좌측 커스텀 영역 (그리기 버튼 등) */
  toolbarLeft?: ReactNode
  /** 하단 요약/힌트 영역 */
  footer?: ReactNode
  placeholder?: ReactNode
  /** ZMap 실제 z 범위(raw count) — 컬러맵 range를 실제값 단위로 표시 */
  zMin?: number
  zMax?: number
}

const MIN_ZOOM = 0.5
const MAX_ZOOM = 40

export default function ImageViewer({
  preview, drawMode, onDrawComplete, rois, onRoisChange, roiTypeLabel, overlayFor,
  overlay, toolbarLeft, footer, placeholder, zMin, zMax
}: Props) {
  const [zoom, setZoom] = useState(1)
  const [pan, setPan] = useState<Pan>({ x: 0, y: 0 })
  const [draw, setDraw] = useState<DrawState | null>(null)
  const [edit, setEdit] = useState<EditState | null>(null)
  const [imgAspect, setImgAspect] = useState<number | null>(null)
  const [csize, setCsize] = useState({ w: 0, h: 0 })
  const [colormap, setColormap] = useState(false)
  const [autoRange, setAutoRange] = useState(true)
  const [rangeLo, setRangeLo] = useState(0)
  const [rangeHi, setRangeHi] = useState(255)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  // 최신 zoom/pan을 즉시 읽기 위한 ref (빠른 연속 휠에서 stale closure 방지)
  const zoomRef = useRef(1)
  const panRef = useRef<Pan>({ x: 0, y: 0 })
  const panStartRef = useRef<{ mx: number; my: number; px: number; py: number } | null>(null)
  const containerRef = useRef<HTMLDivElement>(null)

  // zoom/pan을 state + ref 동시 적용
  const applyView = useCallback((z: number, p: Pan) => {
    zoomRef.current = z; panRef.current = p
    setZoom(z); setPan(p)
  }, [])

  // client 좌표 → 이미지 상대 퍼센트(0~1) — 항상 최신 ref 사용
  const toImgPct = useCallback((clientX: number, clientY: number) => {
    const r = containerRef.current!.getBoundingClientRect()
    return {
      x: (clientX - r.left - panRef.current.x) / (r.width  * zoomRef.current),
      y: (clientY - r.top  - panRef.current.y) / (r.height * zoomRef.current),
    }
  }, [])

  // ── 휠 줌: native non-passive 리스너로 등록 (preventDefault + stopPropagation
  //    으로 부모 스크롤/스크롤바 전파 차단). 커서 고정 + 델타 비례 지수 줌 ──
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    const handler = (e: WheelEvent) => {
      e.preventDefault()
      e.stopPropagation()
      const r = el.getBoundingClientRect()
      const cx = e.clientX - r.left
      const cy = e.clientY - r.top
      const z = zoomRef.current, p = panRef.current
      const nz = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, z * Math.exp(-e.deltaY * 0.0015)))
      if (nz === z) return
      const scale = nz / z
      applyView(nz, { x: cx - (cx - p.x) * scale, y: cy - (cy - p.y) * scale })
    }
    el.addEventListener('wheel', handler, { passive: false })
    return () => el.removeEventListener('wheel', handler)
  }, [applyView])

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0) return
    if (drawMode) {
      e.preventDefault()
      const { x, y } = toImgPct(e.clientX, e.clientY)
      setDraw({ startX: x, startY: y, curX: x, curY: y })
    } else {
      panStartRef.current = { mx: e.clientX, my: e.clientY, px: panRef.current.x, py: panRef.current.y }
    }
  }, [drawMode, toImgPct])

  const onMouseMove = useCallback((e: React.MouseEvent) => {
    if (draw) {
      const { x, y } = toImgPct(e.clientX, e.clientY)
      setDraw(d => d ? { ...d, curX: x, curY: y } : null)
    } else if (panStartRef.current) {
      const dx = e.clientX - panStartRef.current.mx
      const dy = e.clientY - panStartRef.current.my
      applyView(zoomRef.current, { x: panStartRef.current.px + dx, y: panStartRef.current.py + dy })
    }
  }, [draw, toImgPct, applyView])

  const onMouseUp = useCallback(() => {
    panStartRef.current = null
    if (!draw) return
    const xPct = Math.min(draw.startX, draw.curX)
    const yPct = Math.min(draw.startY, draw.curY)
    const wPct = Math.abs(draw.curX - draw.startX)
    const hPct = Math.abs(draw.curY - draw.startY)
    setDraw(null)
    if (wPct < 0.01 || hPct < 0.01) return
    onDrawComplete?.({
      xPct: Math.max(0, xPct),
      yPct: Math.max(0, yPct),
      wPct: Math.min(wPct, 1 - Math.max(0, xPct)),
      hPct: Math.min(hPct, 1 - Math.max(0, yPct)),
    })
  }, [draw, onDrawComplete])

  const resetView = () => applyView(1, { x: 0, y: 0 })

  // ZMap 실제 z 범위가 도착하면 수동 range 초기값을 그 범위로 맞춤
  useEffect(() => {
    if (zMin !== undefined && zMax !== undefined) { setRangeLo(zMin); setRangeHi(zMax) }
  }, [zMin, zMax])

  // 컨테이너 크기 추적 (ROI를 화면 좌표로 그리기 위함)
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    const update = () => { const r = el.getBoundingClientRect(); setCsize({ w: r.width, h: r.height }) }
    update()
    const ro = new ResizeObserver(update)
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  // ROI 이동/리사이즈 드래그 — window 리스너로 캔버스 밖까지 추적
  useEffect(() => {
    if (!edit || !onRoisChange || !rois) return
    const onMove = (ev: MouseEvent) => {
      const { x, y } = toImgPct(ev.clientX, ev.clientY)
      onRoisChange(rois.map(r => r.id === edit.id ? applyEdit(edit, x, y) : r))
    }
    const onUp = () => setEdit(null)
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [edit, rois, onRoisChange, toImgPct])

  // preview(grayscale PNG)를 canvas에 그리고, colormap이면 jet 컬러맵 적용
  useEffect(() => {
    const cv = canvasRef.current
    if (!preview || !cv) return
    const img = new Image()
    img.onload = () => {
      if (img.naturalHeight > 0) setImgAspect(img.naturalWidth / img.naturalHeight)
      cv.width = img.naturalWidth
      cv.height = img.naturalHeight
      const ctx = cv.getContext('2d')
      if (!ctx) return
      ctx.drawImage(img, 0, 0)
      if (colormap) {
        // 8bit gray(0~255)를 실제 z(raw)로 역산한 뒤 [lo,hi] 범위로 매핑
        const hasRange = zMin !== undefined && zMax !== undefined
        const lo = autoRange ? (hasRange ? zMin! : 0) : rangeLo
        const hi = autoRange ? (hasRange ? zMax! : 255) : rangeHi
        const span = Math.max(1e-6, hi - lo)
        const id = ctx.getImageData(0, 0, cv.width, cv.height)
        const d = id.data
        for (let i = 0; i < d.length; i += 4) {
          const value = hasRange ? zMin! + (d[i] / 255) * (zMax! - zMin!) : d[i]
          const t = clamp01((value - lo) / span)
          const [r, g, b] = jet(t)
          d[i] = r; d[i + 1] = g; d[i + 2] = b
        }
        ctx.putImageData(id, 0, 0)
      }
    }
    img.src = `data:image/png;base64,${preview}`
  }, [preview, colormap, autoRange, rangeLo, rangeHi, zMin, zMax])

  const drawStyle = draw ? {
    left:   `${Math.min(draw.startX, draw.curX) * 100}%`,
    top:    `${Math.min(draw.startY, draw.curY) * 100}%`,
    width:  `${Math.abs(draw.curX - draw.startX) * 100}%`,
    height: `${Math.abs(draw.curY - draw.startY) * 100}%`,
  } : null

  const isPanning = !!panStartRef.current
  const modeClass = drawMode ? ' pfe-mode-draw' : isPanning ? ' pfe-mode-pan' : ''

  return (
    <div className="pfe-root">
      <div className="pfe-toolbar">
        {toolbarLeft}
        <div className="pfe-toolbar-right">
          <button
            className={`pfe-btn${colormap ? ' active' : ''}`}
            onClick={() => setColormap(v => !v)}
            title="컬러맵(높이) 토글"
          >🌡</button>
          <span className="pfe-zoom-label">{Math.round(zoom * 100)}%</span>
          <button className="pfe-btn" onClick={resetView} title="줌 리셋">↺</button>
        </div>
      </div>

      {colormap && (
        <div className="pfe-colormap-bar">
          <label className="pfe-cm-auto">
            <input type="checkbox" checked={autoRange}
              onChange={e => setAutoRange(e.target.checked)} /> 자동
          </label>
          <div className="pfe-cm-gradient" />
          {!autoRange && (() => {
            const sMin = zMin ?? 0, sMax = zMax ?? 255
            const step = Math.max(1, Math.round((sMax - sMin) / 255))
            return (
              <div className="pfe-cm-sliders">
                <input type="range" min={sMin} max={sMax} step={step} value={rangeLo}
                  onChange={e => setRangeLo(Math.min(parseFloat(e.target.value), rangeHi - step))} />
                <input type="range" min={sMin} max={sMax} step={step} value={rangeHi}
                  onChange={e => setRangeHi(Math.max(parseFloat(e.target.value), rangeLo + step))} />
                <span className="pfe-cm-range-label">{Math.round(rangeLo)}–{Math.round(rangeHi)}</span>
              </div>
            )
          })()}
        </div>
      )}

      <div
        ref={containerRef}
        className={`pfe-canvas${modeClass}`}
        style={imgAspect && preview ? { aspectRatio: String(imgAspect) } : undefined}
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={onMouseUp}
        onMouseLeave={onMouseUp}
      >
        <div
          className="pfe-viewport"
          style={{ transform: `translate(${pan.x}px,${pan.y}px) scale(${zoom})`, transformOrigin: '0 0' }}
        >
          {preview ? (
            <canvas ref={canvasRef} className="pfe-bg" />
          ) : (
            <div className="pfe-placeholder">
              {placeholder ?? <>상류 노드를 실행하면<br /><small>이미지가 여기에 표시됩니다</small></>}
            </div>
          )}

          {overlay}

          {drawStyle && drawMode && (
            <div className={`pfe-roi pfe-roi-${drawMode} pfe-roi-drawing`} style={drawStyle} />
          )}
        </div>

        {/* ROI는 scale 레이어 밖(화면 좌표)에 그려 항상 선명하게 — 테두리/글자 흐림 방지 */}
        <div className="pfe-roi-layer">
          {rois?.map((roi, i, arr) => {
            const idxInType = arr.slice(0, i).filter(r => r.type === roi.type).length
            const label = `${roiTypeLabel?.(roi.type) ?? roi.type} ${idxInType + 1}`
            const editable = !!onRoisChange && !drawMode
            const left = pan.x + roi.xPct * csize.w * zoom
            const top = pan.y + roi.yPct * csize.h * zoom
            const width = roi.wPct * csize.w * zoom
            const height = roi.hPct * csize.h * zoom
            return (
              <div
                key={roi.id}
                className={`pfe-roi pfe-roi-${roi.type}${editable ? ' pfe-roi-editable' : ''}`}
                style={{ left: `${left}px`, top: `${top}px`, width: `${width}px`, height: `${height}px`,
                  pointerEvents: drawMode ? 'none' : 'auto' }}
                onMouseDown={editable ? (e => {
                  e.stopPropagation(); e.preventDefault()
                  const { x, y } = toImgPct(e.clientX, e.clientY)
                  setEdit({ id: roi.id, mode: 'move', handle: '', startX: x, startY: y, orig: roi })
                }) : undefined}
              >
                <span className="pfe-roi-label">{label}</span>
                {editable && (
                  <button className="pfe-roi-del"
                    onMouseDown={e => e.stopPropagation()}
                    onClick={e => { e.stopPropagation(); onRoisChange!(rois.filter(r => r.id !== roi.id)) }}
                  >×</button>
                )}
                {overlayFor && (
                  <div className="pfe-roi-overlay">{overlayFor(roi, idxInType)}</div>
                )}
                {editable && HANDLES.map(h => (
                  <div key={h} className={`pfe-roi-handle h-${h}`}
                    onMouseDown={e => {
                      e.stopPropagation(); e.preventDefault()
                      setEdit({ id: roi.id, mode: 'resize', handle: h, startX: 0, startY: 0, orig: roi })
                    }} />
                ))}
              </div>
            )
          })}
        </div>
      </div>

      {footer && <div className="pfe-roi-summary">{footer}</div>}
    </div>
  )
}
