// Profile 형상 차트 — z(세로) vs x(가로). NaN은 갭. 점(scatter) 또는 선(line).
interface Props {
  x: number[]
  z: (number | null)[]
  mode?: 'line' | 'points'
  height?: number
}

export default function ProfileChart({ x, z, mode = 'line', height = 220 }: Props) {
  const W = 480, H = height
  const padL = 44, padR = 10, padT = 10, padB = 26

  // 유효 샘플 범위
  let xMin = Infinity, xMax = -Infinity, zMin = Infinity, zMax = -Infinity
  for (let i = 0; i < x.length; i++) {
    const zi = z[i]
    if (zi === null || Number.isNaN(zi as number)) continue
    if (x[i] < xMin) xMin = x[i]; if (x[i] > xMax) xMax = x[i]
    if ((zi as number) < zMin) zMin = zi as number
    if ((zi as number) > zMax) zMax = zi as number
  }
  if (!Number.isFinite(xMin)) return <div className="param-empty">유효 샘플 없음</div>

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

  return (
    <svg width="100%" viewBox={`0 0 ${W} ${H}`} style={{ background: '#14161a', borderRadius: 4 }}>
      {/* 축 */}
      <line x1={padL} y1={H - padB} x2={W - padR} y2={H - padB} stroke="#555" strokeWidth={1} />
      <line x1={padL} y1={padT} x2={padL} y2={H - padB} stroke="#555" strokeWidth={1} />
      {/* z 범위 라벨 */}
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
    </svg>
  )
}
