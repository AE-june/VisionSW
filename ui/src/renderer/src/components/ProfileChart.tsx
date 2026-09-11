// Profile 형상 차트 — z(세로) vs x(가로). NaN은 갭. 점(scatter) 또는 선(line).
import { useState, useCallback, useRef } from 'react'

export interface CaliperFeature {
  sMm: number
  zMm: number
  kind: string
  label?: string
}

export interface CaliperLineFit {
  fromMm: number
  toMm: number
  slope: number
  intercept: number
}

export interface ProfileRange {
  fromMm: number
  toMm: number
  color: string
  label: string
}

interface Props {
  x: number[]
  z: (number | null)[]
  mode?: 'line' | 'points'
  height?: number
  features?: CaliperFeature[]
  lineFits?: CaliperLineFit[]
  /** 컬러 밴드 오버레이 (피처 검색범위, 라인피팅 구간 등) */
  ranges?: ProfileRange[]
  /** 드래그로 범위 선택 시 콜백. 제공되면 좌클릭 드래그 = 범위 선택 모드 */
  onRangeDrag?: (fromMm: number, toMm: number) => void
}

const ZOOM_IN = 0.85
const ZOOM_OUT = 1 / ZOOM_IN

export default function ProfileChart({ x, z, mode = 'line', height = 220, features, lineFits, ranges, onRangeDrag }: Props) {
  const W = 480, H = height
  const padL = 44, padR = 10, padT = 10, padB = 26

  const [tooltip, setTooltip] = useState<{ svgX: number; svgY: number; dx: number; dz: number } | null>(null)
  const [lockedRange, setLockedRange] = useState<{ xMin: number; xMax: number; zMin: number; zMax: number } | null>(null)
  const [fixedByUser, setFixedByUser] = useState(false)
  const [isPanning, setIsPanning] = useState(false)
  const panRef = useRef<{ mouseX: number; mouseY: number; xMinAtStart: number; xMaxAtStart: number; zMinAtStart: number; zMaxAtStart: number } | null>(null)

  // 범위 드래그 상태
  const [dragRange, setDragRange] = useState<{ startMm: number; curMm: number } | null>(null)
  const dragRangeRef = useRef<{ startMm: number } | null>(null)

  // 유효 샘플 범위 (auto)
  let xMinAuto = Infinity, xMaxAuto = -Infinity, zMinAuto = Infinity, zMaxAuto = -Infinity
  for (let i = 0; i < x.length; i++) {
    const zi = z[i]
    if (zi === null || Number.isNaN(zi as number)) continue
    if (x[i] < xMinAuto) xMinAuto = x[i]; if (x[i] > xMaxAuto) xMaxAuto = x[i]
    if ((zi as number) < zMinAuto) zMinAuto = zi as number
    if ((zi as number) > zMaxAuto) zMaxAuto = zi as number
  }
  if (!Number.isFinite(xMinAuto)) return <div className="param-empty">유효 샘플 없음</div>

  const xMin = lockedRange?.xMin ?? xMinAuto
  const xMax = lockedRange?.xMax ?? xMaxAuto
  const zMin = lockedRange?.zMin ?? zMinAuto
  const zMax = lockedRange?.zMax ?? zMaxAuto

  const xr = xMax - xMin || 1, zr = zMax - zMin || 1
  const sx = (v: number) => padL + ((v - xMin) / xr) * (W - padL - padR)
  const sy = (v: number) => padT + (1 - (v - zMin) / zr) * (H - padT - padB)
  // SVG x → data mm
  const svgXToMm = (svgX: number) => xMin + ((svgX - padL) / (W - padL - padR)) * xr

  // 선분: NaN 경계에서 끊음 → 여러 polyline
  const segments: string[] = []
  let cur: string[] = []
  for (let i = 0; i < x.length; i++) {
    const zi = z[i]
    if (zi === null || Number.isNaN(zi as number)) { if (cur.length) { segments.push(cur.join(' ')); cur = [] } continue }
    cur.push(`${sx(x[i]).toFixed(1)},${sy(zi as number).toFixed(1)}`)
  }
  if (cur.length) segments.push(cur.join(' '))

  const getSvgXFromEvent = (e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect()
    return (e.clientX - rect.left) / rect.width * W
  }

  const handleMouseMove = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect()

    // 우클릭 팬 중
    if (panRef.current) {
      const p = panRef.current
      const scaleX = W / rect.width
      const scaleY = H / rect.height
      const dxPx = (e.clientX - p.mouseX) * scaleX
      const dyPx = (e.clientY - p.mouseY) * scaleY
      const rangeX = p.xMaxAtStart - p.xMinAtStart
      const rangeZ = p.zMaxAtStart - p.zMinAtStart
      const dDataX = -(dxPx / (W - padL - padR)) * rangeX
      const dDataZ = (dyPx / (H - padT - padB)) * rangeZ
      setLockedRange({
        xMin: p.xMinAtStart + dDataX,
        xMax: p.xMaxAtStart + dDataX,
        zMin: p.zMinAtStart + dDataZ,
        zMax: p.zMaxAtStart + dDataZ,
      })
      return
    }

    // 범위 드래그 중
    if (dragRangeRef.current) {
      const svgX = (e.clientX - rect.left) / rect.width * W
      const curMm = svgXToMm(svgX)
      setDragRange({ startMm: dragRangeRef.current.startMm, curMm })
      return
    }

    const ratioX = (e.clientX - rect.left) / rect.width
    const svgX = ratioX * W
    const dataX = xMin + ((svgX - padL) / (W - padL - padR)) * xr

    let best = -1, bestDist = Infinity
    for (let i = 0; i < x.length; i++) {
      const zi = z[i]
      if (zi === null || Number.isNaN(zi as number)) continue
      const dist = Math.abs(x[i] - dataX)
      if (dist < bestDist) { bestDist = dist; best = i }
    }
    if (best < 0) { setTooltip(null); return }
    const dz = z[best] as number
    setTooltip({ svgX: sx(x[best]), svgY: sy(dz), dx: x[best], dz })
  }, [x, z, xMin, xr, zMin, zr, dragRangeRef])

  const handleMouseDown = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button === 2) {
      // 우클릭: 팬
      e.preventDefault()
      const curXMin = lockedRange?.xMin ?? xMinAuto
      const curXMax = lockedRange?.xMax ?? xMaxAuto
      const curZMin = lockedRange?.zMin ?? zMinAuto
      const curZMax = lockedRange?.zMax ?? zMaxAuto
      panRef.current = { mouseX: e.clientX, mouseY: e.clientY, xMinAtStart: curXMin, xMaxAtStart: curXMax, zMinAtStart: curZMin, zMaxAtStart: curZMax }
      setIsPanning(true)
      setTooltip(null)
      return
    }
    if (e.button === 0 && onRangeDrag) {
      // 좌클릭: 범위 드래그 시작
      e.preventDefault()
      const svgX = getSvgXFromEvent(e)
      const startMm = svgXToMm(svgX)
      dragRangeRef.current = { startMm }
      setDragRange({ startMm, curMm: startMm })
      setTooltip(null)
    }
  }, [lockedRange, xMinAuto, xMaxAuto, zMinAuto, zMaxAuto, onRangeDrag])

  const handleMouseUp = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button === 2) {
      panRef.current = null
      setIsPanning(false)
      return
    }
    if (e.button === 0 && dragRangeRef.current && dragRange && onRangeDrag) {
      const from = Math.min(dragRange.startMm, dragRange.curMm)
      const to = Math.max(dragRange.startMm, dragRange.curMm)
      if (to - from > 0.001) onRangeDrag(from, to)
      dragRangeRef.current = null
      setDragRange(null)
    }
  }, [dragRange, onRangeDrag])

  const handleMouseLeave = useCallback(() => {
    panRef.current = null
    setIsPanning(false)
    dragRangeRef.current = null
    setDragRange(null)
    setTooltip(null)
  }, [])

  const handleWheel = useCallback((e: React.WheelEvent<SVGSVGElement>) => {
    e.preventDefault()
    const rect = e.currentTarget.getBoundingClientRect()
    const svgX = (e.clientX - rect.left) / rect.width * W
    const svgY = (e.clientY - rect.top) / rect.height * H

    const factor = e.deltaY < 0 ? ZOOM_IN : ZOOM_OUT

    const curXMin = lockedRange?.xMin ?? xMinAuto
    const curXMax = lockedRange?.xMax ?? xMaxAuto
    const curZMin = lockedRange?.zMin ?? zMinAuto
    const curZMax = lockedRange?.zMax ?? zMaxAuto

    const pivotX = curXMin + ((svgX - padL) / (W - padL - padR)) * (curXMax - curXMin)
    const pivotZ = curZMin + (1 - (svgY - padT) / (H - padT - padB)) * (curZMax - curZMin)

    const nearYAxis = svgX < padL + 8
    const nearXAxis = svgY > H - padB - 8

    let newXMin = curXMin, newXMax = curXMax
    let newZMin = curZMin, newZMax = curZMax

    if (!nearYAxis) {
      newXMin = pivotX + (curXMin - pivotX) * factor
      newXMax = pivotX + (curXMax - pivotX) * factor
    }
    if (!nearXAxis) {
      newZMin = pivotZ + (curZMin - pivotZ) * factor
      newZMax = pivotZ + (curZMax - pivotZ) * factor
    }

    setLockedRange({ xMin: newXMin, xMax: newXMax, zMin: newZMin, zMax: newZMax })
  }, [lockedRange, xMinAuto, xMaxAuto, zMinAuto, zMaxAuto])

  const fixRange = useCallback(() => {
    setLockedRange({ xMin, xMax, zMin, zMax })
    setFixedByUser(true)
  }, [xMin, xMax, zMin, zMax])

  const resetZoom = useCallback(() => {
    setLockedRange(null)
    setFixedByUser(false)
  }, [])

  const isDraggingRange = dragRangeRef.current !== null
  const cursor = isPanning ? 'grabbing' : isDraggingRange ? 'col-resize' : onRangeDrag ? 'crosshair' : 'crosshair'

  const tipW = 110, tipH = 32
  const tipX = tooltip ? (tooltip.svgX + tipW + 6 > W ? tooltip.svgX - tipW - 6 : tooltip.svgX + 6) : 0
  const tipY = tooltip ? Math.max(padT, Math.min(tooltip.svgY - tipH / 2, H - padB - tipH)) : 0

  const btnX = W - padR - 40, btnY = padT

  // 범위 드래그 시각화
  const dragX1 = dragRange ? sx(Math.min(dragRange.startMm, dragRange.curMm)) : 0
  const dragX2 = dragRange ? sx(Math.max(dragRange.startMm, dragRange.curMm)) : 0

  return (
    <svg
      width="100%"
      viewBox={`0 0 ${W} ${H}`}
      style={{ background: '#14161a', borderRadius: 4, cursor }}
      onMouseMove={handleMouseMove}
      onMouseDown={handleMouseDown}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseLeave}
      onWheel={handleWheel}
      onContextMenu={e => e.preventDefault()}
    >
      {/* 축 */}
      <line x1={padL} y1={H - padB} x2={W - padR} y2={H - padB} stroke="#555" strokeWidth={1} />
      <line x1={padL} y1={padT} x2={padL} y2={H - padB} stroke="#555" strokeWidth={1} />
      {/* 범위 라벨 */}
      <text x={4} y={padT + 8} fill="#999" fontSize={10}>{zMax.toFixed(3)}</text>
      <text x={4} y={H - padB} fill="#999" fontSize={10}>{zMin.toFixed(3)}</text>
      <text x={padL} y={H - 6} fill="#999" fontSize={10}>{xMin.toFixed(2)}</text>
      <text x={W - padR} y={H - 6} fill="#999" fontSize={10} textAnchor="end">{xMax.toFixed(2)} mm</text>

      {/* 범위 밴드 오버레이 */}
      {ranges?.map((r, i) => {
        const rx1 = Math.max(sx(r.fromMm), padL)
        const rx2 = Math.min(sx(r.toMm), W - padR)
        if (rx2 <= rx1) return null
        return (
          <g key={`range-${i}`}>
            <rect x={rx1} y={padT} width={rx2 - rx1} height={H - padT - padB}
              fill={r.color + '28'} stroke={r.color} strokeWidth={1}
              strokeDasharray="4 3" opacity={0.8} />
            <rect x={rx1 + 2} y={padT + 2} width={r.label.length * 6 + 4} height={11}
              fill="#1e233099" rx={2} />
            <text x={rx1 + 4} y={padT + 11} fill={r.color} fontSize={9}>{r.label}</text>
          </g>
        )
      })}

      {/* 프로파일 데이터 */}
      {mode === 'line'
        ? segments.map((pts, i) => (
            <polyline key={i} points={pts} fill="none" stroke="#00e5ff" strokeWidth={1.5} />
          ))
        : x.map((xi, i) => {
            const zi = z[i]
            if (zi === null || Number.isNaN(zi as number)) return null
            return <circle key={i} cx={sx(xi)} cy={sy(zi as number)} r={1.5} fill="#00e5ff" />
          })}

      {/* 라인피팅 오버레이 (주황색 점선) */}
      {lineFits?.map((lf, i) => {
        const x0 = sx(lf.fromMm), x1 = sx(lf.toMm)
        const y0 = sy(lf.slope * lf.fromMm + lf.intercept)
        const y1 = sy(lf.slope * lf.toMm   + lf.intercept)
        return (
          <line key={`lf-${i}`} x1={x0} y1={y0} x2={x1} y2={y1}
            stroke="#ff8f00" strokeWidth={1.5} strokeDasharray="5 3" opacity={0.85} />
        )
      })}

      {/* 피처 마커 (수직 점선 + 라벨) */}
      {features?.map((f, i) => {
        const fx = sx(f.sMm)
        const fz = sy(f.zMm)
        const label = f.label ?? `F${i}`
        const COLORS = ['#f44336', '#4caf50', '#2196f3', '#ff9800', '#9c27b0']
        const color = COLORS[i % COLORS.length]
        return (
          <g key={`feat-${i}`}>
            <line x1={fx} y1={padT} x2={fx} y2={H - padB}
              stroke={color} strokeWidth={1} strokeDasharray="4 3" opacity={0.7} />
            <circle cx={fx} cy={fz} r={4} fill={color} opacity={0.9} />
            <rect x={fx + 3} y={padT + 2 + i * 14} width={label.length * 6 + 4} height={12}
              fill="#1e2330cc" rx={2} />
            <text x={fx + 5} y={padT + 12 + i * 14} fill={color} fontSize={9}>{label}</text>
          </g>
        )
      })}

      {/* 드래그 중 범위 선택 시각화 */}
      {dragRange && (
        <rect x={dragX1} y={padT} width={Math.max(0, dragX2 - dragX1)} height={H - padT - padB}
          fill="#ffffff18" stroke="#ffffffaa" strokeWidth={1} strokeDasharray="3 2" />
      )}

      {/* 투명 히트 영역 */}
      <rect x={padL} y={padT} width={W - padL - padR} height={H - padT - padB} fill="transparent" />

      {/* Fix/Auto 버튼 */}
      {!fixedByUser ? (
        <g onClick={fixRange} style={{ cursor: 'pointer' }}>
          <rect x={btnX} y={btnY} width={38} height={14} rx={3} fill="#1a2030" stroke="#445" strokeWidth={1} />
          <text x={btnX + 19} y={btnY + 10} fill="#778" fontSize={9} textAnchor="middle" style={{ userSelect: 'none' }}>▶ Fix</text>
        </g>
      ) : (
        <g onClick={resetZoom} style={{ cursor: 'pointer' }}>
          <rect x={btnX} y={btnY} width={38} height={14} rx={3} fill="#1a3a2a" stroke="#00c853" strokeWidth={1} />
          <text x={btnX + 19} y={btnY + 10} fill="#00c853" fontSize={9} textAnchor="middle" style={{ userSelect: 'none' }}>↺ Auto</text>
        </g>
      )}

      {/* tooltip 오버레이 */}
      {!isPanning && !isDraggingRange && tooltip && (
        <>
          <line x1={tooltip.svgX} y1={padT} x2={tooltip.svgX} y2={H - padB}
            stroke="#ffffff44" strokeWidth={1} strokeDasharray="3 3" />
          <line x1={padL} y1={tooltip.svgY} x2={W - padR} y2={tooltip.svgY}
            stroke="#ffffff44" strokeWidth={1} strokeDasharray="3 3" />
          <circle cx={tooltip.svgX} cy={tooltip.svgY} r={3.5} fill="#fff" stroke="#00e5ff" strokeWidth={1.5} />
          <rect x={tipX} y={tipY} width={tipW} height={tipH} rx={3} fill="#1e2330ee" stroke="#00e5ff55" strokeWidth={1} />
          <text x={tipX + 6} y={tipY + 12} fill="#aaa" fontSize={9.5}>x</text>
          <text x={tipX + 16} y={tipY + 12} fill="#e0e0e0" fontSize={9.5}>{tooltip.dx.toFixed(4)} mm</text>
          <text x={tipX + 6} y={tipY + 24} fill="#aaa" fontSize={9.5}>z</text>
          <text x={tipX + 16} y={tipY + 24} fill="#00e5ff" fontSize={9.5}>{tooltip.dz.toFixed(4)}</text>
        </>
      )}
    </svg>
  )
}
