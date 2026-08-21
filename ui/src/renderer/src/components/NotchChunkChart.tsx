// NotchMeasureV2 횡단면 차트 — y(lateral mm) vs z(absolute mm).
// 파란 점/선: envelope 원본, 주황 선: land 피팅 곡선,
// 초록 수직선: notchLoY / notchHiY, 노란 수직선: floorCenterY.
import { useState, useCallback, useRef } from 'react'

interface Props {
  x: number[]          // lateral position (mm)
  z: number[]          // envelope absolute z (mm)
  yFit: (number | null)[]  // land fit curve z at each x (mm)
  notchLoY: number
  notchHiY: number
  floorCenterY: number
  floorZRelUm: number  // 바닥 상대높이 µm (표시용)
  floorZmm: number     // 바닥 절대 z (mm) — 수평선 표시용
  leftLandZmm?: number  // 좌 랜드 실측 z mean/median (mm)
  rightLandZmm?: number // 우 랜드 실측 z mean/median (mm)
  leftEdgeZmm?: number  // 좌 랜드 기울기 기반 시작점 z (mm)
  rightEdgeZmm?: number // 우 랜드 기울기 기반 시작점 z (mm)
  leftEdgeYmm?: number  // 좌 edge 포인트 lateral 위치 (mm)
  rightEdgeYmm?: number // 우 edge 포인트 lateral 위치 (mm)
  height?: number
}

const ZOOM_IN = 0.85
const ZOOM_OUT = 1 / ZOOM_IN

export default function NotchChunkChart({
  x, z, yFit, notchLoY, notchHiY, floorCenterY, floorZRelUm, floorZmm, leftLandZmm, rightLandZmm, leftEdgeZmm, rightEdgeZmm, leftEdgeYmm, rightEdgeYmm, height = 240
}: Props) {
  const W = 480, H = height
  const padL = 52, padR = 10, padT = 12, padB = 28

  const [mode, setMode] = useState<'line' | 'points'>('points')
  const [lockedRange, setLockedRange] = useState<{ xMin: number; xMax: number; zMin: number; zMax: number } | null>(null)
  const [fixedByUser, setFixedByUser] = useState(false)
  const [tooltip, setTooltip] = useState<{ svgX: number; svgY: number; dx: number; dz: number } | null>(null)
  const [isPanning, setIsPanning] = useState(false)
  const panRef = useRef<{ mouseX: number; mouseY: number; xMinAtStart: number; xMaxAtStart: number; zMinAtStart: number; zMaxAtStart: number } | null>(null)

  if (x.length === 0) return <div className="param-empty">데이터 없음</div>

  // auto 범위
  let xMinAuto = Infinity, xMaxAuto = -Infinity, zMinAuto = Infinity, zMaxAuto = -Infinity
  for (let i = 0; i < x.length; i++) {
    if (x[i] < xMinAuto) xMinAuto = x[i]; if (x[i] > xMaxAuto) xMaxAuto = x[i]
    if (z[i] < zMinAuto) zMinAuto = z[i]; if (z[i] > zMaxAuto) zMaxAuto = z[i]
  }
  // yFit도 범위 포함
  for (const v of yFit) { if (v === null) continue; if (v < zMinAuto) zMinAuto = v; if (v > zMaxAuto) zMaxAuto = v }
  // 약간 여백
  const zPad = (zMaxAuto - zMinAuto) * 0.08 || 0.001
  zMinAuto -= zPad; zMaxAuto += zPad

  const xMin = lockedRange?.xMin ?? xMinAuto
  const xMax = lockedRange?.xMax ?? xMaxAuto
  const zMin = lockedRange?.zMin ?? zMinAuto
  const zMax = lockedRange?.zMax ?? zMaxAuto
  const xr = xMax - xMin || 1, zr = zMax - zMin || 1

  const sx = (v: number) => padL + ((v - xMin) / xr) * (W - padL - padR)
  const sy = (v: number) => padT + (1 - (v - zMin) / zr) * (H - padT - padB)

  // 줌 비율에 따라 점 크기 조절 — 축소 시 작게, 확대 시 크게
  const xrAuto = xMaxAuto - xMinAuto || 1
  const dotScale = Math.sqrt(xrAuto / xr)
  const envDotR  = Math.max(0.5, Math.min(3,   1.5 * dotScale))
  const edgeDotR = Math.max(2,   Math.min(8,   5   * dotScale))

  // envelope 선분 (NaN 없어서 끊기 없음)
  const envPts = x.map((xi, i) => `${sx(xi).toFixed(1)},${sy(z[i]).toFixed(1)}`).join(' ')

  // fit curve 선분 (null = 끊기)
  const fitSegs: string[] = []
  let cur: string[] = []
  for (let i = 0; i < x.length; i++) {
    const v = yFit[i]
    if (v === null) { if (cur.length) { fitSegs.push(cur.join(' ')); cur = [] } continue }
    cur.push(`${sx(x[i]).toFixed(1)},${sy(v).toFixed(1)}`)
  }
  if (cur.length) fitSegs.push(cur.join(' '))

  // 수직 마커 SVG x 좌표
  const sxLoY = sx(notchLoY), sxHiY = sx(notchHiY), sxFloor = sx(floorCenterY)

  const handleMouseMove = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect()
    if (panRef.current) {
      const p = panRef.current
      const scaleX = W / rect.width, scaleY = H / rect.height
      const dxPx = (e.clientX - p.mouseX) * scaleX
      const dyPx = (e.clientY - p.mouseY) * scaleY
      const dX = -(dxPx / (W - padL - padR)) * (p.xMaxAtStart - p.xMinAtStart)
      const dZ = (dyPx / (H - padT - padB)) * (p.zMaxAtStart - p.zMinAtStart)
      setLockedRange({ xMin: p.xMinAtStart + dX, xMax: p.xMaxAtStart + dX, zMin: p.zMinAtStart + dZ, zMax: p.zMaxAtStart + dZ })
      return
    }
    const svgX = (e.clientX - rect.left) / rect.width * W
    const dataX = xMin + ((svgX - padL) / (W - padL - padR)) * xr
    let best = -1, bestDist = Infinity
    for (let i = 0; i < x.length; i++) {
      const d = Math.abs(x[i] - dataX)
      if (d < bestDist) { bestDist = d; best = i }
    }
    if (best < 0) { setTooltip(null); return }
    setTooltip({ svgX: sx(x[best]), svgY: sy(z[best]), dx: x[best], dz: z[best] })
  }, [x, z, xMin, xr, zMin, zr])

  const handleMouseDown = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button !== 2) return
    e.preventDefault()
    panRef.current = { mouseX: e.clientX, mouseY: e.clientY, xMinAtStart: lockedRange?.xMin ?? xMinAuto, xMaxAtStart: lockedRange?.xMax ?? xMaxAuto, zMinAtStart: lockedRange?.zMin ?? zMinAuto, zMaxAtStart: lockedRange?.zMax ?? zMaxAuto }
    setIsPanning(true); setTooltip(null)
  }, [lockedRange, xMinAuto, xMaxAuto, zMinAuto, zMaxAuto])

  const handleMouseUp = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button !== 2) return; panRef.current = null; setIsPanning(false)
  }, [])

  const handleMouseLeave = useCallback(() => { panRef.current = null; setIsPanning(false); setTooltip(null) }, [])

  const handleWheel = useCallback((e: React.WheelEvent<SVGSVGElement>) => {
    e.preventDefault()
    const rect = e.currentTarget.getBoundingClientRect()
    const svgX = (e.clientX - rect.left) / rect.width * W
    const svgY = (e.clientY - rect.top) / rect.height * H
    const factor = e.deltaY < 0 ? ZOOM_IN : ZOOM_OUT
    const curXMin = lockedRange?.xMin ?? xMinAuto, curXMax = lockedRange?.xMax ?? xMaxAuto
    const curZMin = lockedRange?.zMin ?? zMinAuto, curZMax = lockedRange?.zMax ?? zMaxAuto
    const pivotX = curXMin + ((svgX - padL) / (W - padL - padR)) * (curXMax - curXMin)
    const pivotZ = curZMin + (1 - (svgY - padT) / (H - padT - padB)) * (curZMax - curZMin)
    const nearYAxis = svgX < padL + 8, nearXAxis = svgY > H - padB - 8
    let nXMin = curXMin, nXMax = curXMax, nZMin = curZMin, nZMax = curZMax
    if (!nearYAxis) { nXMin = pivotX + (curXMin - pivotX) * factor; nXMax = pivotX + (curXMax - pivotX) * factor }
    if (!nearXAxis) { nZMin = pivotZ + (curZMin - pivotZ) * factor; nZMax = pivotZ + (curZMax - pivotZ) * factor }
    setLockedRange({ xMin: nXMin, xMax: nXMax, zMin: nZMin, zMax: nZMax })
  }, [lockedRange, xMinAuto, xMaxAuto, zMinAuto, zMaxAuto])

  const fixRange = useCallback(() => { setLockedRange({ xMin, xMax, zMin, zMax }); setFixedByUser(true) }, [xMin, xMax, zMin, zMax])
  const resetZoom = useCallback(() => { setLockedRange(null); setFixedByUser(false) }, [])

  const tipW = 118, tipH = 32
  const tipX = tooltip ? (tooltip.svgX + tipW + 6 > W ? tooltip.svgX - tipW - 6 : tooltip.svgX + 6) : 0
  const tipY = tooltip ? Math.max(padT, Math.min(tooltip.svgY - tipH / 2, H - padB - tipH)) : 0
  const btnX = W - padR - 40, btnY = padT

  return (
    <div>
      {/* 범례 + 표시 모드 */}
      <div style={{ display: 'flex', gap: 14, alignItems: 'center', fontSize: 10, color: '#888', marginBottom: 4, flexWrap: 'wrap' }}>
        <span><span style={{ color: '#00e5ff' }}>●</span> envelope</span>
        <span><span style={{ color: '#ff9800' }}>—</span> land fit</span>
        <span><span style={{ color: '#4caf50' }}>|</span> notch 경계</span>
        <span><span style={{ color: '#ffeb3b' }}>|</span> floor 중심</span>
        <span><span style={{ color: '#ff5722' }}>—</span> floor z</span>
        <span><span style={{ color: '#ffeb3b' }}>●</span> land edge</span>
        <span style={{ marginLeft: 'auto', color: '#aaa' }}>바닥 rel: {floorZRelUm.toFixed(1)} µm</span>
        <select style={{ background: '#1a1d24', border: '1px solid #333', color: '#ccc', borderRadius: 3, fontSize: 10, padding: '1px 4px' }}
          value={mode} onChange={e => setMode(e.target.value as 'line' | 'points')}>
          <option value="points">점</option>
          <option value="line">선</option>
        </select>
      </div>
      <svg width="100%" viewBox={`0 0 ${W} ${H}`}
        style={{ background: '#14161a', borderRadius: 4, cursor: isPanning ? 'grabbing' : 'crosshair' }}
        onMouseMove={handleMouseMove} onMouseDown={handleMouseDown}
        onMouseUp={handleMouseUp} onMouseLeave={handleMouseLeave}
        onWheel={handleWheel} onContextMenu={e => e.preventDefault()}>

        <defs>
          <clipPath id="plotClip">
            <rect x={padL} y={padT} width={W - padL - padR} height={H - padT - padB} />
          </clipPath>
        </defs>

        {/* 축 */}
        <line x1={padL} y1={H - padB} x2={W - padR} y2={H - padB} stroke="#555" strokeWidth={1} />
        <line x1={padL} y1={padT} x2={padL} y2={H - padB} stroke="#555" strokeWidth={1} />
        {/* 라벨 */}
        <text x={4} y={padT + 8} fill="#777" fontSize={9}>{zMax.toFixed(4)}</text>
        <text x={4} y={H - padB} fill="#777" fontSize={9}>{zMin.toFixed(4)}</text>
        <text x={padL} y={H - 8} fill="#777" fontSize={9}>{xMin.toFixed(2)}</text>
        <text x={W - padR} y={H - 8} fill="#777" fontSize={9} textAnchor="end">{xMax.toFixed(2)} mm</text>

        {/* 데이터 요소 — 플롯 영역 밖 클리핑 */}
        <g clipPath="url(#plotClip)">
          {/* notch 영역 하이라이트 */}
          <rect x={sxLoY} y={padT} width={sxHiY - sxLoY} height={H - padT - padB}
            fill="#1a2a1a" opacity={0.5} />

          {/* notch 경계 수직선 */}
          <line x1={sxLoY} y1={padT} x2={sxLoY} y2={H - padB} stroke="#4caf50" strokeWidth={1} strokeDasharray="4 3" opacity={0.8} />
          <line x1={sxHiY} y1={padT} x2={sxHiY} y2={H - padB} stroke="#4caf50" strokeWidth={1} strokeDasharray="4 3" opacity={0.8} />

          {/* floor 중심 수직선 */}
          <line x1={sxFloor} y1={padT} x2={sxFloor} y2={H - padB} stroke="#ffeb3b" strokeWidth={1} strokeDasharray="3 3" opacity={0.7} />

          {/* 바닥 z 수평선 */}
          {(() => {
            const syFloor = sy(floorZmm)
            return (
              <>
                <line x1={padL} y1={syFloor} x2={W - padR} y2={syFloor} stroke="#ff5722" strokeWidth={1} strokeDasharray="5 3" opacity={0.8} />
                <text x={padL + 2} y={syFloor - 2} fill="#ff5722" fontSize={8} opacity={0.9}>floor</text>
              </>
            )
          })()}

          {/* land fit curve (주황) */}
          {fitSegs.map((pts, i) => (
            <polyline key={i} points={pts} fill="none" stroke="#ff9800" strokeWidth={1.5} opacity={0.9} />
          ))}

          {/* envelope (파랑) */}
          {mode === 'line'
            ? <polyline points={envPts} fill="none" stroke="#00e5ff" strokeWidth={1.5} />
            : x.map((xi, i) => (
                <circle key={i} cx={sx(xi)} cy={sy(z[i])} r={envDotR} fill="#00e5ff" />
              ))}

          {/* 좌 land edge */}
          {leftEdgeZmm !== undefined && leftEdgeYmm !== undefined && (() => {
            const cx = sx(leftEdgeYmm), cy = sy(leftEdgeZmm)
            return (
              <>
                <circle cx={cx} cy={cy} r={edgeDotR} fill="#ffeb3b" stroke="#14161a" strokeWidth={1.5} opacity={0.95} />
                <text x={cx - 6} y={cy - 6} fill="#ffeb3b" fontSize={8} textAnchor="middle" opacity={0.9}>L</text>
              </>
            )
          })()}

          {/* 우 land edge */}
          {rightEdgeZmm !== undefined && rightEdgeYmm !== undefined && (() => {
            const cx = sx(rightEdgeYmm), cy = sy(rightEdgeZmm)
            return (
              <>
                <circle cx={cx} cy={cy} r={edgeDotR} fill="#ffeb3b" stroke="#14161a" strokeWidth={1.5} opacity={0.95} />
                <text x={cx + 6} y={cy - 6} fill="#ffeb3b" fontSize={8} textAnchor="middle" opacity={0.9}>R</text>
              </>
            )
          })()}

          {/* tooltip 십자선 */}
          {!isPanning && tooltip && (
            <>
              <line x1={tooltip.svgX} y1={padT} x2={tooltip.svgX} y2={H - padB}
                stroke="#ffffff33" strokeWidth={1} strokeDasharray="3 3" />
              <line x1={padL} y1={tooltip.svgY} x2={W - padR} y2={tooltip.svgY}
                stroke="#ffffff33" strokeWidth={1} strokeDasharray="3 3" />
              <circle cx={tooltip.svgX} cy={tooltip.svgY} r={3.5} fill="#fff" stroke="#00e5ff" strokeWidth={1.5} />
            </>
          )}
        </g>

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

        {/* tooltip 텍스트 박스 (클립 밖 — 항상 완전히 표시) */}
        {!isPanning && tooltip && (
          <>
            <rect x={tipX} y={tipY} width={tipW} height={tipH} rx={3} fill="#1e2330ee" stroke="#00e5ff55" strokeWidth={1} />
            <text x={tipX + 6} y={tipY + 12} fill="#aaa" fontSize={9.5}>y</text>
            <text x={tipX + 16} y={tipY + 12} fill="#e0e0e0" fontSize={9.5}>{tooltip.dx.toFixed(4)} mm</text>
            <text x={tipX + 6} y={tipY + 24} fill="#aaa" fontSize={9.5}>z</text>
            <text x={tipX + 16} y={tipY + 24} fill="#00e5ff" fontSize={9.5}>{tooltip.dz.toFixed(5)} mm</text>
          </>
        )}
      </svg>
    </div>
  )
}
