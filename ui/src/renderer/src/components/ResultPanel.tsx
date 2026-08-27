import { useRef, useState, useCallback } from 'react'

export interface MeasureResult {
  id: string
  tool: string
  ok: boolean
  msg: string
  pass?: boolean
  // Generic named measurements / decisions
  measurements?: { name: string; value: number; unit: string; valid: boolean; elemIndex?: number }[]
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
  const [sel, setSel] = useState<number | 'all'>('all')
  if (!r.ok) return <Row label={r.tool} value={r.msg || 'Fail'} pass={false} />

  const measAll = r.measurements ?? []
  const decs = (r.decisions ?? []).filter(d => d.name !== 'allPass')

  if (measAll.length === 0 && decs.length === 0)
    return <Row label={r.tool} value="OK" pass />

  // 브로드캐스트 원소 인덱스 목록 (>=0). 있으면 드롭다운으로 원소 선택.
  const elems = Array.from(new Set(
    measAll.map(m => m.elemIndex ?? -1).filter(idx => idx >= 0)
  )).sort((a, b) => a - b)
  const hasElems = elems.length > 0

  const meas = hasElems && sel !== 'all'
    ? measAll.filter(m => (m.elemIndex ?? -1) === sel)
    : measAll

  return <>
    {hasElems && (
      <div className="result-row">
        <span className="result-label">ROI</span>
        <select className="result-elem-select" value={String(sel)}
          onChange={e => setSel(e.target.value === 'all' ? 'all' : Number(e.target.value))}>
          <option value="all">전체 ({elems.length})</option>
          {elems.map(idx => <option key={idx} value={idx}>roi{idx}</option>)}
        </select>
      </div>
    )}
    {meas.filter(m => m.valid).map((m, i) => (
      <Row key={`m-${i}`}
        label={hasElems && sel === 'all' && (m.elemIndex ?? -1) >= 0
          ? `roi${m.elemIndex}.${m.name}` : m.name}
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
