import { useRef, useState, useCallback } from 'react'

export interface MeasureResult {
  id: string
  tool: string
  ok: boolean
  msg: string
  pass?: boolean
  // Generic named measurements / decisions
  measurements?: { name: string; value: number; unit: string; valid: boolean }[]
  decisions?: { name: string; pass: boolean; reason: string; measured?: number; nominal?: number; tolerance?: number }[]
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

  const meas = r.measurements ?? []
  const decs = (r.decisions ?? []).filter(d => d.name !== 'allPass')

  if (meas.length === 0 && decs.length === 0)
    return <Row label={r.tool} value="OK" pass />

  return <>
    {meas.filter(m => m.valid).map((m, i) => (
      <Row key={`m-${i}`}
        label={m.name}
        value={`${m.value.toFixed(m.unit === 'pts' ? 0 : 4)}${m.unit ? ' ' + m.unit : ''}`}
        pass
      />
    ))}
    {decs.map((d, i) => (
      <Row key={`d-${i}`} label={d.name} value={d.pass ? 'PASS' : 'FAIL'} pass={d.pass} />
    ))}
  </>
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
