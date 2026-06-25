import type { Roi } from './RoiCanvas'

const svgStyle: React.CSSProperties = {
  position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none',
}

// 검색 ROI 내에서 찾은 라인과 중심점을 이미지 좌표계 위에 그림
export function LineCenterOverlay({ cx, cy, angleDeg, imgW, imgH, roi, label }: {
  cx: number; cy: number; angleDeg: number; imgW: number; imgH: number; roi?: Roi; label?: string
}) {
  const rad = (angleDeg * Math.PI) / 180
  const dx = Math.cos(rad), dy = Math.sin(rad)
  // 검색 ROI (회전 포함) — 라인을 ROI 로컬 프레임에서 사각형으로 클리핑
  const crx = ((roi?.xPct ?? 0) + (roi?.wPct ?? 1) / 2) * imgW
  const cry = ((roi?.yPct ?? 0) + (roi?.hPct ?? 1) / 2) * imgH
  const hw = ((roi?.wPct ?? 1) * imgW) / 2, hh = ((roi?.hPct ?? 1) * imgH) / 2
  const rr = ((roi?.angleDeg ?? 0) * Math.PI) / 180
  const rc = Math.cos(rr), rs = Math.sin(rr)
  // 라인 중심·방향을 ROI 로컬 좌표로 (월드→로컬 = R(-θ))
  const lcx = (cx - crx) * rc + (cy - cry) * rs
  const lcy = -(cx - crx) * rs + (cy - cry) * rc
  const ldx = dx * rc + dy * rs, ldy = -dx * rs + dy * rc

  const axisRange = (c: number, d: number, lo: number, hi: number): [number, number] | null => {
    if (Math.abs(d) < 1e-9) return c >= lo && c <= hi ? [-Infinity, Infinity] : null
    const t0 = (lo - c) / d, t1 = (hi - c) / d
    return d > 0 ? [t0, t1] : [t1, t0]
  }
  const xr = axisRange(lcx, ldx, -hw, hw)
  const yr = axisRange(lcy, ldy, -hh, hh)
  let x1 = cx, y1 = cy, x2 = cx, y2 = cy
  if (xr && yr) {
    const tmin = Math.max(xr[0], yr[0]), tmax = Math.min(xr[1], yr[1])
    if (tmin <= tmax) {
      const p = (t: number): [number, number] => {
        const lx = lcx + t * ldx, ly = lcy + t * ldy
        return [crx + lx * rc - ly * rs, cry + lx * rs + ly * rc]
      }
      ;[x1, y1] = p(tmin);[x2, y2] = p(tmax)
    }
  }
  const r = Math.max(2, Math.min(imgW, imgH) * 0.015)
  const fs = Math.max(8, Math.min(imgW, imgH) * 0.035)

  return (
    <svg viewBox={`0 0 ${imgW} ${imgH}`} preserveAspectRatio="none" style={svgStyle}>
      <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#00e676" strokeWidth={2} vectorEffect="non-scaling-stroke" />
      {/* 중심: 십자가만 */}
      <line x1={cx - r} y1={cy} x2={cx + r} y2={cy} stroke="#ff5252" strokeWidth={2} vectorEffect="non-scaling-stroke" />
      <line x1={cx} y1={cy - r} x2={cx} y2={cy + r} stroke="#ff5252" strokeWidth={2} vectorEffect="non-scaling-stroke" />
      {label && (
        <text x={cx + r * 1.3} y={cy - r * 0.8} fill="#ff5252" fontSize={fs} fontWeight="bold"
          stroke="#000" strokeWidth={fs * 0.18} paintOrder="stroke"
          style={{ userSelect: 'none' }}>{label}</text>
      )}
    </svg>
  )
}

const DIR_VEC: Record<string, [number, number]> = { lr: [1, 0], rl: [-1, 0], tb: [0, 1], bt: [0, -1] }

// 검색 ROI의 스캔 방향(방향성)을 화살표로 표시 — 회전과 함께 적용
export function ScanArrow({ roi, scanDir, imgW, imgH }: {
  roi?: Roi; scanDir: string; imgW: number; imgH: number
}) {
  if (!roi || !imgW || !imgH) return null
  const cx = (roi.xPct + roi.wPct / 2) * imgW
  const cy = (roi.yPct + roi.hPct / 2) * imgH
  const hw = (roi.wPct * imgW) / 2, hh = (roi.hPct * imgH) / 2
  const rr = ((roi.angleDeg ?? 0) * Math.PI) / 180
  const rc = Math.cos(rr), rs = Math.sin(rr)
  const [dlx, dly] = DIR_VEC[scanDir] ?? DIR_VEC.lr
  const dx = dlx * rc - dly * rs, dy = dlx * rs + dly * rc      // 월드 방향
  const ext = (Math.abs(dlx) > 0 ? hw : hh) * 0.85
  const ex = cx + dx * ext, ey = cy + dy * ext                 // 화살촉
  const sxp = cx - dx * ext, syp = cy - dy * ext               // 꼬리
  const ah = Math.max(4, Math.min(imgW, imgH) * 0.025)
  const px = -dy, py = dx                                      // 수직
  const b1x = ex - dx * ah + px * ah * 0.6, b1y = ey - dy * ah + py * ah * 0.6
  const b2x = ex - dx * ah - px * ah * 0.6, b2y = ey - dy * ah - py * ah * 0.6
  return (
    <svg viewBox={`0 0 ${imgW} ${imgH}`} preserveAspectRatio="none" style={svgStyle}>
      <line x1={sxp} y1={syp} x2={ex} y2={ey} stroke="#29b6f6" strokeWidth={2} vectorEffect="non-scaling-stroke" />
      <polygon points={`${ex},${ey} ${b1x},${b1y} ${b2x},${b2y}`} fill="#29b6f6" />
    </svg>
  )
}
