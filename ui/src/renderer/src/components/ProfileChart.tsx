// Profile 형상 차트 — z(세로) vs x(가로). NaN은 갭. 점(scatter) 또는 선(line).
import { useState, useCallback, useRef } from 'react'

interface Props {
  x: number[]
  z: (number | null)[]
  mode?: 'line' | 'points'
  height?: number
}

const ZOOM_IN = 0.85
const ZOOM_OUT = 1 / ZOOM_IN

export default function ProfileChart({ x, z, mode = 'line', height = 220 }: Props) {
  const W = 480, H = height
  const padL = 44, padR = 10, padT = 10, padB = 26

  const [tooltip, setTooltip] = useState<{ svgX: number; svgY: number; dx: number; dz: number } | null>(null)
  const [lockedRange, setLockedRange] = useState<{ xMin: number; xMax: number; zMin: number; zMax: number } | null>(null)
  // Fix 버튼 클릭 여부 — 휠 줌만 한 상태는 false (버튼 여전히 "▶ Fix")
  const [fixedByUser, setFixedByUser] = useState(false)
  const [isPanning, setIsPanning] = useState(false)
  // 팬 시작 정보 (ref — 렌더 불필요)
  const panRef = useRef<{ mouseX: number; mouseY: number; xMinAtStart: number; xMaxAtStart: number; zMinAtStart: number; zMaxAtStart: number } | null>(null)

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

  // 선분: NaN 경계에서 끊음 → 여러 polyline
  const segments: string[] = []
  let cur: string[] = []
  for (let i = 0; i < x.length; i++) {
    const zi = z[i]
    if (zi === null || Number.isNaN(zi as number)) { if (cur.length) { segments.push(cur.join(' ')); cur = [] } continue }
    cur.push(`${sx(x[i]).toFixed(1)},${sy(zi as number).toFixed(1)}`)
  }
  if (cur.length) segments.push(cur.join(' '))

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
      const dDataZ = (dyPx / (H - padT - padB)) * rangeZ  // SVG Y 반전
      setLockedRange({
        xMin: p.xMinAtStart + dDataX,
        xMax: p.xMaxAtStart + dDataX,
        zMin: p.zMinAtStart + dDataZ,
        zMax: p.zMaxAtStart + dDataZ,
      })
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
  }, [x, z, xMin, xr, zMin, zr])

  const handleMouseDown = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button !== 2) return  // 우클릭만
    e.preventDefault()
    const curXMin = lockedRange?.xMin ?? xMinAuto
    const curXMax = lockedRange?.xMax ?? xMaxAuto
    const curZMin = lockedRange?.zMin ?? zMinAuto
    const curZMax = lockedRange?.zMax ?? zMaxAuto
    panRef.current = { mouseX: e.clientX, mouseY: e.clientY, xMinAtStart: curXMin, xMaxAtStart: curXMax, zMinAtStart: curZMin, zMaxAtStart: curZMax }
    setIsPanning(true)
    setTooltip(null)
  }, [lockedRange, xMinAuto, xMaxAuto, zMinAuto, zMaxAuto])

  const handleMouseUp = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button !== 2) return
    panRef.current = null
    setIsPanning(false)
  }, [])

  const handleMouseLeave = useCallback(() => {
    panRef.current = null
    setIsPanning(false)
    setTooltip(null)
  }, [])

  // 마우스 휠: 위치 기반 축 판별 후 확대/축소 (fixedByUser 건드리지 않음)
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
    // fixedByUser는 유지 — 휠 줌은 Fix 상태에 영향 없음
  }, [lockedRange, xMinAuto, xMaxAuto, zMinAuto, zMaxAuto])

  // Fix: 현재 표시 범위(휠/팬 포함)를 확정 고정
  const fixRange = useCallback(() => {
    setLockedRange({ xMin, xMax, zMin, zMax })
    setFixedByUser(true)
  }, [xMin, xMax, zMin, zMax])

  // Auto: 고정 해제
  const resetZoom = useCallback(() => {
    setLockedRange(null)
    setFixedByUser(false)
  }, [])

  const cursor = isPanning ? 'grabbing' : 'crosshair'

  // tooltip 박스 위치
  const tipW = 110, tipH = 32
  const tipX = tooltip ? (tooltip.svgX + tipW + 6 > W ? tooltip.svgX - tipW - 6 : tooltip.svgX + 6) : 0
  const tipY = tooltip ? Math.max(padT, Math.min(tooltip.svgY - tipH / 2, H - padB - tipH)) : 0

  // 버튼 (우상단)
  const btnX = W - padR - 40, btnY = padT

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

      {mode === 'line'
        ? segments.map((pts, i) => (
            <polyline key={i} points={pts} fill="none" stroke="#00e5ff" strokeWidth={1.5} />
          ))
        : x.map((xi, i) => {
            const zi = z[i]
            if (zi === null || Number.isNaN(zi as number)) return null
            return <circle key={i} cx={sx(xi)} cy={sy(zi as number)} r={1.5} fill="#00e5ff" />
          })}

      {/* 투명 히트 영역 */}
      <rect x={padL} y={padT} width={W - padL - padR} height={H - padT - padB} fill="transparent" />

      {/* Fix 버튼: fixedByUser 아닐 때 항상 표시 (휠 줌 후에도 Fix 가능) */}
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
      {!isPanning && tooltip && (
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
