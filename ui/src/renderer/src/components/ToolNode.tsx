import { useState, useContext } from 'react'
import { Handle, Position } from '@xyflow/react'
import type { NodeProps } from '@xyflow/react'
import { TOOL_DEF_MAP, PORT_COLORS } from '../types/tools'
import { HoveredEdgeContext } from './hoveredEdge'

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
  elapsedMs?: number
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
  if (toolType === 'HeightMeasure' && result.measures) {
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

  // Image / HeightMap preview (로더/필터)
  if (result.preview) {
    return (
      <div className="tool-node-result">
        <img
          src={`data:image/jpeg;base64,${result.preview}`}
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
  const [hoveredPort, setHoveredPort] = useState<string | null>(null)
  const he = useContext(HoveredEdgeContext)

  // 직접 hover 했거나, hover된 엣지에 연결된 핸들이면 강조
  const isPortLit = (pid: string) =>
    hoveredPort === pid ||
    (!!he && ((he.source === id && (he.sourceHandle ?? 'output-0') === pid) ||
              (he.target === id && (he.targetHandle ?? 'input-0') === pid)))

  if (!def) return null

  const hasResult = !!result
  const statusDotClass = hasResult
    ? (result!.ok !== false && result!.pass !== false ? 'pass' : 'fail')
    : ''

  const portRows = Math.max(def.inputs.length, def.outputs.length)

  return (
    <div className={`tool-node ${selected ? 'selected' : ''}`}>
      {/* 헤더 */}
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

      {/* 포트 행 — 핸들과 라벨을 같은 행에 두어 높이를 맞춤 */}
      <div className="tool-node-ports">
        {Array.from({ length: portRows }).map((_, i) => {
          const inT = def.inputs[i]
          const outT = def.outputs[i]
          return (
            <div className="port-row" key={i}>
              {inT !== undefined && (
                <Handle
                  type="target" position={Position.Left} id={`input-${i}`}
                  className={`th-in${isPortLit(`input-${i}`) ? ' port-handle-hover' : ''}`}
                  onMouseEnter={() => setHoveredPort(`input-${i}`)}
                  onMouseLeave={() => setHoveredPort(null)}
                  style={{ background: PORT_COLORS[inT], border: '2px solid #111' }}
                />
              )}
              <span className="port-slot left">
                {inT !== undefined && (
                  <span
                    className={`port-label ${isPortLit(`input-${i}`) ? 'port-label-hover' : ''}`}
                    style={{ color: PORT_COLORS[inT] }}
                    onMouseEnter={() => setHoveredPort(`input-${i}`)}
                    onMouseLeave={() => setHoveredPort(null)}
                  >{def.inputLabels?.[i] ?? inT}</span>
                )}
              </span>
              <span className="port-slot right">
                {outT !== undefined && (
                  <span
                    className={`port-label ${isPortLit(`output-${i}`) ? 'port-label-hover' : ''}`}
                    style={{ color: PORT_COLORS[outT] }}
                    onMouseEnter={() => setHoveredPort(`output-${i}`)}
                    onMouseLeave={() => setHoveredPort(null)}
                  >{def.outputLabels?.[i] ?? outT}</span>
                )}
              </span>
              {outT !== undefined && (
                <Handle
                  type="source" position={Position.Right} id={`output-${i}`}
                  className={`th-out${isPortLit(`output-${i}`) ? ' port-handle-hover' : ''}`}
                  onMouseEnter={() => setHoveredPort(`output-${i}`)}
                  onMouseLeave={() => setHoveredPort(null)}
                  style={{ background: PORT_COLORS[outT], border: '2px solid #111' }}
                />
              )}
            </div>
          )
        })}
      </div>

      {/* 결과창 (토글) */}
      {result && resultOpen && <ResultArea toolType={toolType} result={result} />}
    </div>
  )
}
