import { useState, useRef, useCallback } from 'react'

export interface PlaneFitROI {
  id: string
  type: 'ref' | 'measure'
  xPct: number
  yPct: number
  wPct: number
  hPct: number
}

interface DrawState {
  startX: number
  startY: number
  curX: number
  curY: number
}

interface Pan { x: number; y: number }

interface Props {
  rois: PlaneFitROI[]
  algorithm: string
  ransacThreshold: number
  ransacIterations: number
  preview?: string
  onChange: (rois: PlaneFitROI[], algo: string, threshold: number, iterations: number) => void
}

let uidCounter = 0
const uid = () => `roi-${++uidCounter}`

const MIN_ZOOM = 0.5
const MAX_ZOOM = 20

export default function PlaneFitEditor({
  rois, algorithm, ransacThreshold, ransacIterations, preview, onChange
}: Props) {
  const [drawMode, setDrawMode] = useState<'ref' | 'measure' | null>(null)
  const [draw, setDraw] = useState<DrawState | null>(null)
  const [zoom, setZoom] = useState(1)
  const [pan, setPan] = useState<Pan>({ x: 0, y: 0 })
  const panStartRef = useRef<{ mx: number; my: number; px: number; py: number } | null>(null)
  const containerRef = useRef<HTMLDivElement>(null)

  // Convert client position → image-relative percentage (0~1)
  const toImgPct = useCallback((clientX: number, clientY: number) => {
    const r = containerRef.current!.getBoundingClientRect()
    return {
      x: (clientX - r.left - pan.x) / (r.width  * zoom),
      y: (clientY - r.top  - pan.y) / (r.height * zoom),
    }
  }, [pan, zoom])

  // ── Wheel zoom (centered on cursor) ─────────────────────────────────
  const onWheel = useCallback((e: React.WheelEvent) => {
    e.preventDefault()
    const r = containerRef.current!.getBoundingClientRect()
    const cx = e.clientX - r.left
    const cy = e.clientY - r.top
    const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15
    setZoom(z => {
      const nz = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, z * factor))
      const scale = nz / z
      setPan(p => ({ x: cx - (cx - p.x) * scale, y: cy - (cy - p.y) * scale }))
      return nz
    })
  }, [])

  // ── Mouse events ─────────────────────────────────────────────────────
  const onMouseDown = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0) return
    if (drawMode) {
      e.preventDefault()
      const { x, y } = toImgPct(e.clientX, e.clientY)
      setDraw({ startX: x, startY: y, curX: x, curY: y })
    } else {
      // Pan
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
    if (!draw || !drawMode) return

    const xPct = Math.min(draw.startX, draw.curX)
    const yPct = Math.min(draw.startY, draw.curY)
    const wPct = Math.abs(draw.curX - draw.startX)
    const hPct = Math.abs(draw.curY - draw.startY)

    setDraw(null)
    if (wPct < 0.01 || hPct < 0.01) return

    const newRoi: PlaneFitROI = {
      id: uid(), type: drawMode,
      xPct: Math.max(0, xPct),
      yPct: Math.max(0, yPct),
      wPct: Math.min(wPct, 1 - Math.max(0, xPct)),
      hPct: Math.min(hPct, 1 - Math.max(0, yPct)),
    }
    const updated = drawMode === 'measure'
      ? [...rois.filter(r => r.type !== 'measure'), newRoi]
      : [...rois, newRoi]

    onChange(updated, algorithm, ransacThreshold, ransacIterations)
    setDrawMode(null)
  }, [draw, drawMode, rois, algorithm, ransacThreshold, ransacIterations, onChange])

  const deleteRoi = (id: string) =>
    onChange(rois.filter(r => r.id !== id), algorithm, ransacThreshold, ransacIterations)

  const setAlgo = (algo: string) =>
    onChange(rois, algo, ransacThreshold, ransacIterations)

  const setRansac = (field: 'ransacThreshold' | 'ransacIterations', val: number) => {
    const t = field === 'ransacThreshold' ? val : ransacThreshold
    const i = field === 'ransacIterations' ? val : ransacIterations
    onChange(rois, algorithm, t, i)
  }

  const resetView = () => { setZoom(1); setPan({ x: 0, y: 0 }) }

  // Draw preview rect
  const drawStyle = draw ? {
    left:   `${Math.min(draw.startX, draw.curX) * 100}%`,
    top:    `${Math.min(draw.startY, draw.curY) * 100}%`,
    width:  `${Math.abs(draw.curX - draw.startX) * 100}%`,
    height: `${Math.abs(draw.curY - draw.startY) * 100}%`,
  } : null

  const isPanning = !!panStartRef.current

  return (
    <div className="pfe-root">
      {/* Toolbar */}
      <div className="pfe-toolbar">
        <button
          className={`pfe-btn ref${drawMode === 'ref' ? ' active' : ''}`}
          onClick={() => setDrawMode(m => m === 'ref' ? null : 'ref')}
        >+ Reference</button>
        <button
          className={`pfe-btn measure${drawMode === 'measure' ? ' active' : ''}`}
          onClick={() => setDrawMode(m => m === 'measure' ? null : 'measure')}
        >+ Measure</button>
        <div className="pfe-toolbar-right">
          <span className="pfe-zoom-label">{Math.round(zoom * 100)}%</span>
          <button className="pfe-btn" onClick={resetView} title="줌 리셋">↺</button>
        </div>
      </div>

      {/* Canvas */}
      <div
        ref={containerRef}
        className={`pfe-canvas${drawMode ? ' pfe-mode-draw' : isPanning ? ' pfe-mode-pan' : ''}`}
        onWheel={onWheel}
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={onMouseUp}
        onMouseLeave={onMouseUp}
      >
        {/* Zoomable viewport */}
        <div
          className="pfe-viewport"
          style={{ transform: `translate(${pan.x}px,${pan.y}px) scale(${zoom})`, transformOrigin: '0 0' }}
        >
          {preview ? (
            <img src={`data:image/png;base64,${preview}`} className="pfe-bg" alt="zmap" draggable={false} />
          ) : (
            <div className="pfe-placeholder">
              노드를 실행하면<br /><small>ZMap이 여기에 표시됩니다</small>
            </div>
          )}

          {/* ROIs */}
          {rois.map(roi => (
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
              <span className="pfe-roi-label">{roi.type === 'ref' ? 'Ref' : 'Meas'}</span>
              <button
                className="pfe-roi-del"
                onMouseDown={e => e.stopPropagation()}
                onClick={e => { e.stopPropagation(); deleteRoi(roi.id) }}
              >×</button>
            </div>
          ))}

          {/* Drawing preview */}
          {drawStyle && drawMode && (
            <div className={`pfe-roi pfe-roi-${drawMode} pfe-roi-drawing`} style={drawStyle} />
          )}
        </div>
      </div>

      {/* Summary */}
      <div className="pfe-roi-summary">
        <span className="pfe-roi-count ref">Ref: {rois.filter(r => r.type === 'ref').length}</span>
        <span className="pfe-roi-count measure">Meas: {rois.filter(r => r.type === 'measure').length}/1</span>
        {drawMode && <span className="pfe-drawing-hint">드래그해서 영역 지정 · ESC로 취소</span>}
      </div>

      {/* Algorithm */}
      <div className="param-section">알고리즘</div>
      <div className="param-row">
        <span className="param-label">Method</span>
        <select className="param-select" value={algorithm} onChange={e => setAlgo(e.target.value)}>
          <option value="LeastSquares">Least Squares</option>
          <option value="RANSAC">RANSAC</option>
          <option value="SVD">SVD (PCA)</option>
        </select>
      </div>
      {algorithm === 'RANSAC' && <>
        <div className="param-row">
          <span className="param-label">Threshold (mm)</span>
          <input className="param-input" type="number" step="0.001" value={ransacThreshold}
            onChange={e => setRansac('ransacThreshold', parseFloat(e.target.value) || 0)} />
        </div>
        <div className="param-row">
          <span className="param-label">Iterations</span>
          <input className="param-input" type="number" step="10" value={ransacIterations}
            onChange={e => setRansac('ransacIterations', parseInt(e.target.value) || 0)} />
        </div>
      </>}
    </div>
  )
}
