import { useState, useRef, useCallback } from 'react'
import ParamPanel from './ParamPanel'
import PlaneFitEditor, { type PlaneFitROI } from './PlaneFitEditor'
import HeightFromPlaneEditor, { type HeightFromPlaneSettings } from './HeightFromPlaneEditor'
import ImageViewer from './ImageViewer'
import PlaneView3D from './PlaneView3D'
import type { Roi } from './RoiCanvas'

interface HeightMeasure {
  cx: number; cy: number; z: number
  distance: number; pointCount: number; pass: boolean
}

interface NodeResult {
  preview?: string
  heightDiff?: number
  thicknessMm?: number
  minMm?: number
  maxMm?: number
  pass?: boolean
  ok?: boolean
  msg?: string
  // PlaneFit
  planeA?: number; planeB?: number; planeC?: number
  rmse?: number; tiltDeg?: number; refPointCount?: number; inlierCount?: number
  cloud?: [number, number, number][]
  // HeightFromPlane
  measures?: HeightMeasure[]; allPass?: boolean
  // ZMap 실제 z 범위
  zMin?: number; zMax?: number
}

interface Props {
  nodeId: string
  toolType: string
  label: string
  params: Record<string, unknown>
  result?: NodeResult
  upstreamPreview?: string
  upstreamZMin?: number
  upstreamZMax?: number
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  onClose: () => void
}

function ResultView({ toolType, result, rois }: { toolType: string; result?: NodeResult; rois?: Roi[] }) {
  const zMin = result?.zMin
  const zMax = result?.zMax
  if (!result) {
    return <div className="param-empty">실행 후 결과가 여기에 표시됩니다</div>
  }

  // HeightFromPlane: 측정 ROI(읽기전용) 위에 거리 치수를 오버레이
  const measureRois = (rois ?? []).filter(r => r.type === 'measure')

  return (
    <div className="node-result-view">
      {result.preview && (
        <div className="node-result-image-wrap">
          <ImageViewer
            preview={result.preview}
            zMin={zMin}
            zMax={zMax}
            rois={toolType === 'HeightMeasure' ? measureRois : undefined}
            roiTypeLabel={() => 'ROI'}
            overlayFor={(_roi, idx) => {
              const m = result.measures?.[idx]
              return m ? (
                <span className={`pfe-roi-result ${m.pass ? 'pass' : 'fail'}`}>
                  {m.pointCount === 0 ? '빈 ROI' : `${m.distance.toFixed(4)} mm`}
                </span>
              ) : null
            }}
          />
        </div>
      )}

      {toolType === 'PlaneFit' && result.planeA !== undefined && (
        <div className="node-result-measures">
          <div className="node-result-row">
            <span className="node-result-label">평면식</span>
            <span className="node-result-val">z = {result.planeA.toFixed(5)}·x + {result.planeB!.toFixed(5)}·y + {result.planeC!.toFixed(4)}</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">RMSE</span>
            <span className="node-result-val">{result.rmse?.toFixed(4)} mm</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">기울기</span>
            <span className="node-result-val">{result.tiltDeg?.toFixed(3)}°</span>
          </div>
        </div>
      )}

      {toolType === 'PlaneFit' && result.cloud && result.cloud.length > 0 && (
        <PlaneView3D
          a={result.planeA!} b={result.planeB!} c={result.planeC!}
          points={result.cloud}
        />
      )}

      {toolType === 'HeightMeasure' && result.measures && result.measures.length > 0 && (
        <div className="node-result-measures">
          {result.measures.map((m, i) => (
            <div className="node-result-row" key={i}>
              <span className="node-result-label">ROI {i + 1}</span>
              <span className={`node-result-val ${m.pass ? '' : 'fail-val'}`}>
                {m.pointCount === 0 ? '빈 ROI' : `${m.distance.toFixed(4)} mm`}
              </span>
            </div>
          ))}
        </div>
      )}

      {result.msg && <div className="node-result-msg">{result.msg}</div>}
    </div>
  )
}

export default function NodePanel({ nodeId, toolType, label, params, result, upstreamPreview, upstreamZMin, upstreamZMax, onParamChange, onClose }: Props) {
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
              maxCloudPoints={(params.maxCloudPoints as number) ?? 200000}
              preview={upstreamPreview ?? result?.preview}
              zMin={upstreamZMin ?? result?.zMin}
              zMax={upstreamZMax ?? result?.zMax}
              onChange={(next) => onParamChange(nodeId, { ...params, ...next })}
            />
          ) : toolType === 'HeightMeasure' ? (
            <HeightFromPlaneEditor
              rois={(params.rois as Roi[]) ?? []}
              aggregation={(params.aggregation as string) ?? 'Mean'}
              highTailPct={(params.highTailPct as number) ?? 20}
              useTolerance={(params.useTolerance as boolean) ?? false}
              nominalMm={(params.nominalMm as number) ?? 0}
              toleranceMm={(params.toleranceMm as number) ?? 0.05}
              preview={upstreamPreview ?? result?.preview}
              zMin={upstreamZMin ?? result?.zMin}
              zMax={upstreamZMax ?? result?.zMax}
              onChange={(next: HeightFromPlaneSettings) =>
                onParamChange(nodeId, { ...params, ...next })
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
          <ResultView toolType={toolType} result={result} rois={params.rois as Roi[]} />
        </div>
      )}
    </div>
  )
}
