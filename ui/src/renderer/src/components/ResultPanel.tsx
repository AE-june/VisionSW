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

function formatResult(r: MeasureResult): string {
  if (r.tool === 'LineFitHeight' && r.heightDiff !== undefined)
    return `${r.heightDiff.toFixed(3)} mm`
  if (r.tool === 'ThicknessMeasure' && r.thicknessMm !== undefined)
    return `${r.thicknessMm.toFixed(3)} mm`
  return r.ok ? 'OK' : r.msg || 'Fail'
}

function toolLabel(r: MeasureResult): string {
  if (r.tool === 'LineFitHeight') return 'Height Diff'
  if (r.tool === 'ThicknessMeasure') return 'Thickness'
  return r.tool
}

function resultPass(r: MeasureResult): boolean {
  if (r.pass !== undefined) return r.pass
  return r.ok
}

export default function ResultPanel({ results, logs, overallPass, onClear }: Props) {
  const statusClass = overallPass === null ? '' : overallPass ? 'pass' : 'fail'
  const statusText = overallPass === null ? 'READY' : overallPass ? 'PASS' : 'FAIL'

  return (
    <div className="result-panel">
      <div className="result-left">
        <div className="panel-header">측정 결과</div>
        <div className={`result-status ${statusClass}`}>{statusText}</div>
        <div className="result-list">
          {results.map((r) => (
            <div key={r.id} className="result-row">
              <span className="result-label">{toolLabel(r)}</span>
              <span className={`result-value ${resultPass(r) ? 'pass-color' : 'fail-color'}`}>
                {formatResult(r)}
              </span>
            </div>
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
