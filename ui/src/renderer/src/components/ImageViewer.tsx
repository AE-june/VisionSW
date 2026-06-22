import { useState, useRef, useCallback, type ReactNode } from 'react'

export interface DrawRect { xPct: number; yPct: number; wPct: number; hPct: number }

interface DrawState { startX: number; startY: number; curX: number; curY: number }
interface Pan { x: number; y: number }

interface Props {
  preview?: string
  /** null/undefined면 팬 모드. 문자열이면 해당 타입 이름으로 ROI 그리기 (프리뷰 색상에 사용) */
  drawMode?: string | null
  onDrawComplete?: (rect: DrawRect) => void
  /** 이미지 좌표계(0~1) 위에 함께 변환되어 렌더되는 오버레이 (ROI, 결과 마커 등) */
  overlay?: ReactNode
  /** 툴바 좌측 커스텀 영역 (그리기 버튼 등) */
  toolbarLeft?: ReactNode
  /** 하단 요약/힌트 영역 */
  footer?: ReactNode
  placeholder?: ReactNode
}

const MIN_ZOOM = 0.5
const MAX_ZOOM = 40

export default function ImageViewer({
  preview, drawMode, onDrawComplete, overlay, toolbarLeft, footer, placeholder
}: Props) {
  const [zoom, setZoom] = useState(1)
  const [pan, setPan] = useState<Pan>({ x: 0, y: 0 })
  const [draw, setDraw] = useState<DrawState | null>(null)
  const [imgAspect, setImgAspect] = useState<number | null>(null)
  const panStartRef = useRef<{ mx: number; my: number; px: number; py: number } | null>(null)
  const containerRef = useRef<HTMLDivElement>(null)

  // client 좌표 → 이미지 상대 퍼센트(0~1)
  const toImgPct = useCallback((clientX: number, clientY: number) => {
    const r = containerRef.current!.getBoundingClientRect()
    return {
      x: (clientX - r.left - pan.x) / (r.width  * zoom),
      y: (clientY - r.top  - pan.y) / (r.height * zoom),
    }
  }, [pan, zoom])

  // ── 휠 줌: 항상 커서 위치를 고정점으로 (updater 내 부수효과 없이 순수 계산) ──
  const onWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault()
    const r = containerRef.current!.getBoundingClientRect()
    const cx = e.clientX - r.left
    const cy = e.clientY - r.top
    const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15
    const nz = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, zoom * factor))
    if (nz === zoom) return
    const scale = nz / zoom
    setZoom(nz)
    setPan({ x: cx - (cx - pan.x) * scale, y: cy - (cy - pan.y) * scale })
  }, [zoom, pan])

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0) return
    if (drawMode) {
      e.preventDefault()
      const { x, y } = toImgPct(e.clientX, e.clientY)
      setDraw({ startX: x, startY: y, curX: x, curY: y })
    } else {
      panStartRef.current = { mx: e.clientX, my: e.clientY, px: pan.x, py: pan.y }
    }
  }, [drawMode, pan, toImgPct])

  const onMouseMove = useCallback((e: React.MouseEvent) => {
    if (draw) {
      const { x, y } = toImgPct(e.clientX, e.clientY)
      setDraw(d => d ? { ...d, curX: x, curY: y } : null)
    } else if (panStartRef.current) {
      const dx = e.clientX - panStartRef.current.mx
      const dy = e.clientY - panStartRef.current.my
      setPan({ x: panStartRef.current.px + dx, y: panStartRef.current.py + dy })
    }
  }, [draw, toImgPct])

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

  const resetView = () => { setZoom(1); setPan({ x: 0, y: 0 }) }

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
          <span className="pfe-zoom-label">{Math.round(zoom * 100)}%</span>
          <button className="pfe-btn" onClick={resetView} title="줌 리셋">↺</button>
        </div>
      </div>

      <div
        ref={containerRef}
        className={`pfe-canvas${modeClass}`}
        style={imgAspect && preview ? { aspectRatio: String(imgAspect) } : undefined}
        onWheel={onWheel}
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
            <img
              src={`data:image/png;base64,${preview}`}
              className="pfe-bg"
              alt="view"
              draggable={false}
              onLoad={e => {
                const img = e.currentTarget
                if (img.naturalHeight > 0) setImgAspect(img.naturalWidth / img.naturalHeight)
              }}
            />
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
      </div>

      {footer && <div className="pfe-roi-summary">{footer}</div>}
    </div>
  )
}
