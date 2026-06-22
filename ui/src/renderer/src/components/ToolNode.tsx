import { useState } from 'react'
import { Handle, Position } from '@xyflow/react'
import type { NodeProps } from '@xyflow/react'
import { TOOL_DEF_MAP, PORT_COLORS } from '../types/tools'

interface HeightMeasure {
  distance: number; pointCount: number; pass: boolean
}

interface NodeResult {
  preview?: string    // base64 PNG
  heightDiff?: number
  thicknessMm?: number
  pass?: boolean
  ok?: boolean
  msg?: string
  // PlaneFit
  planeA?: number; rmse?: number; tiltDeg?: number
  // HeightFromPlane
  measures?: HeightMeasure[]; allPass?: boolean
}

interface ToolNodeData {
  label: string
  toolType: string
  result?: NodeResult
  onRun?: (nodeId: string) => void
  [key: string]: unknown
}

function ResultArea({ toolType, result }: { toolType: string; result: NodeResult }) {
  // PlaneFit: 평면 피팅 품질
  if (toolType === 'PlaneFit' && result.planeA !== undefined) {
    return (
      <div className="tool-node-result">
        <div className="tool-node-measure pass">
          <span className="measure-label">RMSE</span>
          <span className="measure-value">{result.rmse?.toFixed(4)} mm</span>
          <span className="measure-badge">{result.tiltDeg?.toFixed(2)}°</span>
        </div>
      </div>
    )
  }

  // HeightFromPlane: ROI별 수직거리
  if (toolType === 'HeightFromPlane' && result.measures) {
    return (
      <div className="tool-node-result">
        {result.measures.map((m, i) => (
          <div key={i} className={`tool-node-measure ${m.pass ? 'pass' : 'fail'}`}>
            <span className="measure-label">#{i + 1}</span>
            <span className="measure-value">
              {m.pointCount === 0 ? '—' : `${m.distance.toFixed(4)} mm`}
            </span>
            <span className={`measure-badge ${m.pass ? 'pass' : 'fail'}`}>
              {m.pass ? 'PASS' : 'FAIL'}
            </span>
          </div>
        ))}
      </div>
    )
  }

  // LineFitHeight measurement
  if (toolType === 'LineFitHeight' && result.heightDiff !== undefined) {
    const pass = result.pass !== false
    return (
      <div className="tool-node-result">
        <div className={`tool-node-measure ${pass ? 'pass' : 'fail'}`}>
          <span className="measure-label">ΔH</span>
          <span className="measure-value">{result.heightDiff.toFixed(3)} mm</span>
          <span className={`measure-badge ${pass ? 'pass' : 'fail'}`}>{pass ? 'PASS' : 'FAIL'}</span>
        </div>
      </div>
    )
  }

  // Image / ZMap preview (로더/필터)
  if (result.preview) {
    return (
      <div className="tool-node-result">
        <img
          src={`data:image/png;base64,${result.preview}`}
          className="tool-node-preview"
          alt="result"
        />
      </div>
    )
  }

  // ThicknessMeasure
  if (toolType === 'ThicknessMeasure' && result.thicknessMm !== undefined) {
    const pass = result.pass === true
    return (
      <div className="tool-node-result">
        <div className={`tool-node-measure ${pass ? 'pass' : 'fail'}`}>
          <span className="measure-label">T</span>
          <span className="measure-value">{result.thicknessMm.toFixed(3)} mm</span>
          <span className={`measure-badge ${pass ? 'pass' : 'fail'}`}>{pass ? 'PASS' : 'FAIL'}</span>
        </div>
      </div>
    )
  }

  // 결과는 있지만 표시할 데이터가 없으면 상태 도트만 표시
  if (result.ok !== undefined) {
    return (
      <div className="tool-node-result">
        <div className={`tool-node-status-dot ${result.ok ? 'pass' : 'fail'}`} />
      </div>
    )
  }

  return null
}

export default function ToolNode({ id, data, selected }: NodeProps) {
  const { label, toolType, result, onRun } = data as ToolNodeData
  const def = TOOL_DEF_MAP[toolType]
  const [resultOpen, setResultOpen] = useState(false)


  if (!def) return null

  const hasInputs  = def.inputs.length > 0
  const hasOutputs = def.outputs.length > 0

  const hasResult = !!result
  const statusDotClass = hasResult
    ? (result!.ok !== false && result!.pass !== false ? 'pass' : 'fail')
    : ''

  return (
    <div className={`tool-node ${selected ? 'selected' : ''}`}>

      {/* 입력 핸들 (왼쪽) */}
      {def.inputs.map((portType, i) => {
        const pct = def.inputs.length === 1 ? 50 : (i + 1) / (def.inputs.length + 1) * 100
        return (
          <Handle
            key={`input-${i}`}
            type="target"
            position={Position.Left}
            id={`input-${i}`}
            style={{ top: `${pct}%`, background: PORT_COLORS[portType], border: '2px solid #111' }}
          />
        )
      })}

      {/* 노드 본체 */}
      <div className="tool-node-header">
        <span>{label}</span>
        <div className="tool-node-header-actions">
          {hasResult && (
            <button
              className="tool-node-toggle-btn"
              title={resultOpen ? '결과 접기' : '결과 펼치기'}
              onClick={e => { e.stopPropagation(); setResultOpen(v => !v) }}
            >
              <span className={`tool-node-status-dot ${statusDotClass}`} />
              {resultOpen ? '▲' : '▼'}
            </button>
          )}
          {onRun && (
            <button
              className="tool-node-run-btn"
              title="이 노드만 실행"
              onClick={e => { e.stopPropagation(); onRun(id) }}
            >▶</button>
          )}
        </div>
      </div>
      <div className="tool-node-body">
        <div className="port-col left">
          {hasInputs && def.inputs.map((t, i) => (
            <span key={i} className="port-label" style={{ color: PORT_COLORS[t] }}>● {t}</span>
          ))}
        </div>
        <div className="port-col right">
          {hasOutputs && def.outputs.map((t, i) => (
            <span key={i} className="port-label" style={{ color: PORT_COLORS[t] }}>{t} ●</span>
          ))}
        </div>
      </div>

      {/* 결과창 (토글) */}
      {result && resultOpen && <ResultArea toolType={toolType} result={result} />}

      {/* 출력 핸들 (오른쪽) */}
      {def.outputs.map((portType, i) => {
        const pct = def.outputs.length === 1 ? 50 : (i + 1) / (def.outputs.length + 1) * 100
        return (
          <Handle
            key={`output-${i}`}
            type="source"
            position={Position.Right}
            id={`output-${i}`}
            style={{ top: `${pct}%`, background: PORT_COLORS[portType], border: '2px solid #111' }}
          />
        )
      })}
    </div>
  )
}
