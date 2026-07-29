import { useRef, useState, useCallback } from 'react'

export interface MeasureResult {
  id: string
  tool: string
  ok: boolean
  msg: string
  heightDiff?: number
  thicknessMm?: number
  minMm?: number
  maxMm?: number
  pass?: boolean
  // PlaneFit
  planeA?: number; rmse?: number; tiltDeg?: number
  // HeightFromPlane
  measures?: { distance: number; pointCount: number; pass: boolean }[]
  allPass?: boolean
}

export interface LogEntry {
  level: 'info' | 'error' | 'warn'
  msg: string
}

interface Props {
  results: MeasureResult[]
  logs: LogEntry[]
  overallPass: boolean | null
  onClear: () => void
}

function Row({ label, value, pass }: { label: string; value: string; pass: boolean }) {
  return (
    <div className="result-row">
      <span className="result-label">{label}</span>
      <span className={`result-value ${pass ? 'pass-color' : 'fail-color'}`}>{value}</span>
    </div>
  )
}

// 노드 1개의 결과 → 행 묶음
function ResultRows({ r }: { r: MeasureResult }) {
  if (!r.ok) return <Row label={r.tool} value={r.msg || 'Fail'} pass={false} />

  if (r.tool === 'PlaneFit') {
    if (r.planeA === undefined) return <Row label="PlaneFit" value="OK" pass />
    return <>
      <Row label="Plane RMSE" value={`${r.rmse?.toFixed(4)} mm`} pass />
      <Row label="기울기" value={`${r.tiltDeg?.toFixed(3)}°`} pass />
    </>
  }
  if (r.tool === 'HeightMeasure') {
    if (!r.measures || r.measures.length === 0) return <Row label="Height" value="—" pass={false} />
    return <>
      {r.measures.map((m, i) => (
        <Row key={i}
          label={`Meas ${i + 1}`}
          value={m.pointCount === 0 ? '빈 ROI' : `${m.distance.toFixed(4)} mm`}
          pass={m.pass}
        />
      ))}
    </>
  }
  return <Row label={r.tool} value="OK" pass />
}

export default function ResultPanel({ results, logs, overallPass, onClear }: Props) {
  const statusClass = overallPass === null ? '' : overallPass ? 'pass' : 'fail'
  const statusText = overallPass === null ? 'READY' : overallPass ? 'PASS' : 'FAIL'

  const [height, setHeight] = useState(180)
  const dragRef = useRef<{ startY: number; startH: number } | null>(null)

  const onResizeStart = useCallback((e: React.MouseEvent) => {
    e.preventDefault()
    dragRef.current = { startY: e.clientY, startH: height }
    const onMove = (ev: MouseEvent) => {
      if (!dragRef.current) return
      const delta = dragRef.current.startY - ev.clientY  // 위로 끌면 커짐
      const next = Math.min(window.innerHeight - 120, Math.max(80, dragRef.current.startH + delta))
      setHeight(next)
    }
    const onUp = () => {
      dragRef.current = null
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }, [height])

  return (
    <div className="result-panel" style={{ height }}>
      <div className="result-resize-handle" onMouseDown={onResizeStart} />
      <div className="result-left">
        <div className="panel-header">측정 결과</div>
        <div className={`result-status ${statusClass}`}>{statusText}</div>
        <div className="result-list">
          {results.map((r) => (
            <ResultRows key={r.id} r={r} />
          ))}
          {results.length === 0 && (
            <div className="result-row">
              <span className="result-label" style={{ color: '#444' }}>결과 없음</span>
            </div>
          )}
        </div>
      </div>
      <div className="result-right">
        <div className="panel-header">로그</div>
        <div className="log-area">
          {logs.map((l, i) => (
            <div key={i} className={`log-line log-${l.level}`}>[{l.level.toUpperCase()}] {l.msg}</div>
          ))}
        </div>
        <button className="btn-clear" onClick={onClear}>Clear</button>
      </div>
    </div>
  )
}
