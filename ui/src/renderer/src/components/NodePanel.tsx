import { useState, useRef, useCallback } from 'react'
import ParamPanel from './ParamPanel'
import PlaneFitEditor, { type PlaneFitROI } from './PlaneFitEditor'

interface NodeResult {
  preview?: string
  heightDiff?: number
  thicknessMm?: number
  minMm?: number
  maxMm?: number
  pass?: boolean
  ok?: boolean
  msg?: string
}

interface Props {
  nodeId: string
  toolType: string
  label: string
  params: Record<string, unknown>
  result?: NodeResult
  upstreamPreview?: string
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  onClose: () => void
}

function ResultView({ toolType, result }: { toolType: string; result?: NodeResult }) {
  if (!result) {
    return <div className="param-empty">실행 후 결과가 여기에 표시됩니다</div>
  }

  return (
    <div className="node-result-view">
      {result.preview && (
        <div className="node-result-image-wrap">
          <img
            src={`data:image/png;base64,${result.preview}`}
            className="node-result-image"
            alt="output"
          />
        </div>
      )}

      {(toolType === 'LineFitHeight' || toolType === 'PlaneFit') && result.heightDiff !== undefined && (
        <div className="node-result-measures">
          <div className="node-result-row">
            <span className="node-result-label">Height Diff</span>
            <span className="node-result-val">{result.heightDiff.toFixed(4)} mm</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">Qz</span>
            <span className="node-result-val">{(result as { Qz?: number }).Qz?.toFixed(4) ?? '—'} mm</span>
          </div>
        </div>
      )}

      {toolType === 'ThicknessMeasure' && result.thicknessMm !== undefined && (
        <div className="node-result-measures">
          <div className="node-result-row">
            <span className="node-result-label">Thickness</span>
            <span className="node-result-val">{result.thicknessMm.toFixed(4)} mm</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">Min / Max</span>
            <span className="node-result-val">{result.minMm?.toFixed(3)} / {result.maxMm?.toFixed(3)} mm</span>
          </div>
        </div>
      )}

      {result.pass !== undefined && (
        <div className={`node-result-badge ${result.pass ? 'pass' : 'fail'}`}>
          {result.pass ? 'PASS' : 'FAIL'}
        </div>
      )}

      {result.ok !== undefined && result.pass === undefined && (
        <div className={`node-result-badge ${result.ok ? 'pass' : 'fail'}`}>
          {result.ok ? 'OK' : 'FAIL'}
        </div>
      )}

      {result.msg && <div className="node-result-msg">{result.msg}</div>}
    </div>
  )
}

export default function NodePanel({ nodeId, toolType, label, params, result, upstreamPreview, onParamChange, onClose }: Props) {
  const [tab, setTab] = useState<'params' | 'result'>('params')
  const [width, setWidth] = useState(280)
  const dragStartRef = useRef<{ mx: number; w: number } | null>(null)

  const onResizeMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault()
    dragStartRef.current = { mx: e.clientX, w: width }

    const onMove = (ev: MouseEvent) => {
      if (!dragStartRef.current) return
      const dx = dragStartRef.current.mx - ev.clientX   // drag left = wider
      setWidth(Math.max(220, Math.min(600, dragStartRef.current.w + dx)))
    }
    const onUp = () => {
      dragStartRef.current = null
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }, [width])

  return (
    <div className="node-panel" style={{ width }}>
      <div className="node-panel-resize-handle" onMouseDown={onResizeMouseDown} />
      <div className="node-panel-header">
        <span>{label}</span>
        <button className="param-close" onClick={onClose}>✕</button>
      </div>

      <div className="node-panel-tabs">
        <button
          className={`node-panel-tab ${tab === 'params' ? 'active' : ''}`}
          onClick={() => setTab('params')}
        >
          파라미터
        </button>
        <button
          className={`node-panel-tab ${tab === 'result' ? 'active' : ''}`}
          onClick={() => setTab('result')}
        >
          결과 {result && <span className={`tab-dot ${result.ok === false ? 'fail' : 'pass'}`} />}
        </button>
      </div>

      {tab === 'params' && (
        <div className="node-panel-body">
          {toolType === 'PlaneFit' ? (
            <PlaneFitEditor
              rois={(params.rois as PlaneFitROI[]) ?? []}
              algorithm={(params.algorithm as string) ?? 'LeastSquares'}
              ransacThreshold={(params.ransacThreshold as number) ?? 0.05}
              ransacIterations={(params.ransacIterations as number) ?? 200}
              preview={upstreamPreview ?? (result as { preview?: string } | undefined)?.preview}
              onChange={(rois, algo, threshold, iterations) =>
                onParamChange(nodeId, { ...params, rois, algorithm: algo, ransacThreshold: threshold, ransacIterations: iterations })
              }
            />
          ) : (
            <ParamPanel
              nodeId={nodeId}
              toolType={toolType}
              label={label}
              params={params}
              onParamChange={onParamChange}
              onClose={onClose}
              embedded
            />
          )}
        </div>
      )}

      {tab === 'result' && (
        <div className="node-panel-body">
          <ResultView toolType={toolType} result={result} />
        </div>
      )}
    </div>
  )
}
