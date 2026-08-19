// 다중 시리즈 profile 차트 — 같은 x/z 축을 공유하는 여러 선을 겹쳐 그림 (Notch Measure V2의
// notch floor z / land left z / land right z 같은 절대 높이 비교용). NaN은 갭.
interface Series {
  label: string
  x: number[]
  z: (number | null)[]
  color: string
  bold?: boolean
}
interface Props {
  series: Series[]
  height?: number
  unit?: string
}

export default function NotchProfileChart({ series, height = 240, unit = 'mm' }: Props) {
  const W = 480, H = height
  const padL = 46, padR = 10, padT = 10, padB = 26

  let xMin = Infinity, xMax = -Infinity, zMin = Infinity, zMax = -Infinity
  for (const s of series) {
    for (let i = 0; i < s.x.length; i++) {
      const zi = s.z[i]
      if (zi === null || Number.isNaN(zi as number)) continue
      if (s.x[i] < xMin) xMin = s.x[i]; if (s.x[i] > xMax) xMax = s.x[i]
      if ((zi as number) < zMin) zMin = zi as number
      if ((zi as number) > zMax) zMax = zi as number
    }
  }
  if (!Number.isFinite(xMin)) return <div className="param-empty">유효 샘플 없음</div>

  const zPad = Math.max(1e-6, (zMax - zMin) * 0.08)
  zMin -= zPad; zMax += zPad
  const xr = xMax - xMin || 1, zr = zMax - zMin || 1
  const sx = (v: number) => padL + ((v - xMin) / xr) * (W - padL - padR)
  const sy = (v: number) => padT + (1 - (v - zMin) / zr) * (H - padT - padB)

  const toSegments = (x: number[], z: (number | null)[]) => {
    const segs: string[] = []
    let cur: string[] = []
    for (let i = 0; i < x.length; i++) {
      const zi = z[i]
      if (zi === null || Number.isNaN(zi as number)) { if (cur.length) { segs.push(cur.join(' ')); cur = [] } continue }
      cur.push(`${sx(x[i]).toFixed(1)},${sy(zi as number).toFixed(1)}`)
    }
    if (cur.length) segs.push(cur.join(' '))
    return segs
  }

  return (
    <div>
      <svg width="100%" viewBox={`0 0 ${W} ${H}`} style={{ background: '#14161a', borderRadius: 4 }}>
        <line x1={padL} y1={H - padB} x2={W - padR} y2={H - padB} stroke="#555" strokeWidth={1} />
        <line x1={padL} y1={padT} x2={padL} y2={H - padB} stroke="#555" strokeWidth={1} />
        <text x={4} y={padT + 8} fill="#999" fontSize={10}>{zMax.toFixed(3)}</text>
        <text x={4} y={H - padB} fill="#999" fontSize={10}>{zMin.toFixed(3)}</text>
        <text x={padL} y={H - 6} fill="#999" fontSize={10}>{xMin.toFixed(2)}</text>
        <text x={W - padR} y={H - 6} fill="#999" fontSize={10} textAnchor="end">{xMax.toFixed(2)} mm</text>
        {series.map((s, si) => toSegments(s.x, s.z).map((pts, i) => (
          <polyline key={`${si}-${i}`} points={pts} fill="none" stroke={s.color}
            strokeWidth={s.bold ? 2 : 1.2} opacity={s.bold ? 1 : 0.85} />
        )))}
      </svg>
      <div style={{ display: 'flex', gap: 14, flexWrap: 'wrap', marginTop: 6, fontSize: 11, opacity: 0.85 }}>
        {series.map((s, i) => (
          <span key={i} style={{ display: 'inline-flex', alignItems: 'center', gap: 5 }}>
            <span style={{ width: 12, height: s.bold ? 3 : 2, background: s.color, display: 'inline-block', borderRadius: 1 }} />
            {s.label}
          </span>
        ))}
        <span style={{ marginLeft: 'auto', opacity: 0.6 }}>z ({unit})</span>
      </div>
    </div>
  )
}
