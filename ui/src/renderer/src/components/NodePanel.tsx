import { useState, useRef, useCallback } from 'react'
import ParamPanel from './ParamPanel'
import PlaneFitEditor, { type PlaneFitROI } from './PlaneFitEditor'
import HeightFromPlaneEditor, { type HeightFromPlaneSettings } from './HeightFromPlaneEditor'
import LineCenterEditor, { type LineCenterSettings } from './LineCenterEditor'
import { LineCenterOverlay } from './lineCenterViz'
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
  // LineCenter — 찾은 모든 라인
  lines?: { cx: number; cy: number; cxMm: number; cyMm: number; angleDeg: number; roiIndex: number; pointCount: number }[]
  imgW?: number; imgH?: number
  // Align (좌표정렬)
  offCol?: number; offRow?: number; offXMm?: number; offYMm?: number
  // ZMap 실제 z 범위 + 분해능
  zMin?: number; zMax?: number
  xResMm?: number; yResMm?: number
  elapsedMs?: number
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
  upstreamResX?: number
  upstreamResY?: number
  upstreamOriginCol?: number
  upstreamOriginRow?: number
  width: number
  onWidthChange: (w: number) => void
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  onRun?: (nodeId: string) => void
  onClose: () => void
}

function ResultView({ toolType, result, rois, nodeId, params, onParamChange, originCol, originRow }: {
  toolType: string; result?: NodeResult; rois?: Roi[]
  nodeId: string; params: Record<string, unknown>
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  originCol?: number; originRow?: number
}) {
  const zMin = result?.zMin
  const zMax = result?.zMax
  if (!result) {
    return <div className="param-empty">실행 후 결과가 여기에 표시됩니다</div>
  }

  // HeightFromPlane: 측정 ROI(읽기전용) 위에 거리 치수를 오버레이.
  // 저장 좌표는 Align 원점 기준 상대값이므로, 미리보기(절대 좌표) 위에 그릴 땐 원점을 더한다.
  const oPctX = originCol != null && result.imgW ? originCol / result.imgW : 0
  const oPctY = originRow != null && result.imgH ? originRow / result.imgH : 0
  const measureRois = (rois ?? []).filter(r => r.type === 'measure')
    .map(r => ({ ...r, xPct: r.xPct + oPctX, yPct: r.yPct + oPctY }))

  // LineCenter 결과: 찾은 라인 + 중심(십자가)만 표시 (ROI 박스/화살표 없음)
  // 라인을 검색 ROI로 클리핑하기 위해 roiIndex로 해당 ROI 참조 (그리진 않음)
  const searchRois = (rois ?? []).filter(r => r.type === 'search')
  const lineOverlay = toolType === 'LineCenter' && result.imgW && result.lines
    ? <>{result.lines.map((l, i) => (
        <LineCenterOverlay key={i} cx={l.cx} cy={l.cy} angleDeg={l.angleDeg}
          imgW={result.imgW!} imgH={result.imgH!} roi={searchRois[l.roiIndex]}
          label={`${l.roiIndex + 1}`} />
      ))}</>
    : undefined

  // Align: 검출된 기준점(=새 원점)을 십자선으로 표시
  const alignOverlay = toolType === 'Align' && result.imgW && result.offCol !== undefined
    ? <svg viewBox={`0 0 ${result.imgW} ${result.imgH!}`} preserveAspectRatio="none"
        style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
        <line x1={0} y1={result.offRow} x2={result.imgW} y2={result.offRow}
          stroke="#ffca28" strokeWidth={1} strokeDasharray="6 4" vectorEffect="non-scaling-stroke" />
        <line x1={result.offCol} y1={0} x2={result.offCol} y2={result.imgH!}
          stroke="#ffca28" strokeWidth={1} strokeDasharray="6 4" vectorEffect="non-scaling-stroke" />
        <circle cx={result.offCol} cy={result.offRow} r={Math.max(3, Math.min(result.imgW, result.imgH!) * 0.012)}
          fill="none" stroke="#ffca28" strokeWidth={2} vectorEffect="non-scaling-stroke" />
      </svg>
    : undefined

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
            overlay={lineOverlay ?? alignOverlay}
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

      {toolType === 'LineCenter' && searchRois.length > 0 && (() => {
        const found = result.lines ?? []
        const xRoi = (params.xRoi as number) ?? 0
        const yRoi = (params.yRoi as number) ?? 0
        const xLine = found.find(l => l.roiIndex === xRoi)
        const yLine = found.find(l => l.roiIndex === yRoi)
        const setSel = (key: 'xRoi' | 'yRoi', v: number) => onParamChange(nodeId, { ...params, [key]: v })
        return (
          <div className="node-result-measures">
            {searchRois.map((_r, i) => {
              const l = found.find(ln => ln.roiIndex === i)
              return (
                <div className="node-result-row" key={i}>
                  <span className="node-result-label">라인 {i + 1}</span>
                  <span className={`node-result-val ${l ? '' : 'fail-val'}`}>
                    {l ? `(${l.cxMm.toFixed(3)}, ${l.cyMm.toFixed(3)}) mm · ${l.angleDeg.toFixed(1)}°` : '검색 실패'}
                  </span>
                </div>
              )
            })}

            <div className="param-section">출력 좌표 선택</div>
            <div className="param-row">
              <span className="param-label">X ← 라인</span>
              <select className="param-select" value={xRoi}
                onChange={e => setSel('xRoi', parseInt(e.target.value))}>
                {found.map(l => <option key={l.roiIndex} value={l.roiIndex}>라인 {l.roiIndex + 1}</option>)}
              </select>
              <span className="node-result-val">{xLine ? `${xLine.cxMm.toFixed(3)} mm` : '—'}</span>
            </div>
            <div className="param-row">
              <span className="param-label">Y ← 라인</span>
              <select className="param-select" value={yRoi}
                onChange={e => setSel('yRoi', parseInt(e.target.value))}>
                {found.map(l => <option key={l.roiIndex} value={l.roiIndex}>라인 {l.roiIndex + 1}</option>)}
              </select>
              <span className="node-result-val">{yLine ? `${yLine.cyMm.toFixed(3)} mm` : '—'}</span>
            </div>
            <div className="param-empty" style={{ fontSize: 10 }}>
              X 출력 = 선택 라인의 x, Y 출력 = 선택 라인의 y. 같은 라인을 고르면 그 점의 (x,y).
            </div>
          </div>
        )
      })()}

      {toolType === 'Align' && result.offCol !== undefined && (
        <div className="node-result-measures">
          <div className="node-result-row">
            <span className="node-result-label">원점 (px)</span>
            <span className="node-result-val">({result.offCol.toFixed(1)}, {result.offRow!.toFixed(1)})</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">이동량 (mm)</span>
            <span className="node-result-val">({result.offXMm!.toFixed(3)}, {result.offYMm!.toFixed(3)})</span>
          </div>
        </div>
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

export default function NodePanel({ nodeId, toolType, label, params, result, upstreamPreview, upstreamZMin, upstreamZMax, upstreamResX, upstreamResY, upstreamOriginCol, upstreamOriginRow, width, onWidthChange, onParamChange, onRun, onClose }: Props) {
  const [tab, setTab] = useState<'params' | 'result'>('params')
  const dragStartRef = useRef<{ mx: number; w: number } | null>(null)

  const onResizeMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault()
    dragStartRef.current = { mx: e.clientX, w: width }

    const onMove = (ev: MouseEvent) => {
      if (!dragStartRef.current) return
      const dx = dragStartRef.current.mx - ev.clientX   // drag left = wider
      onWidthChange(Math.max(220, Math.min(600, dragStartRef.current.w + dx)))
    }
    const onUp = () => {
      dragStartRef.current = null
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }, [width, onWidthChange])

  return (
    <div className="node-panel" style={{ width }}>
      <div className="node-panel-resize-handle" onMouseDown={onResizeMouseDown} />
      <div className="node-panel-header">
        <span>{label}</span>
        <span className="node-panel-header-actions">
          {onRun && (
            <button className="node-panel-run" title="이 노드 실행" onClick={() => onRun(nodeId)}>▶ 실행</button>
          )}
          <button className="param-close" onClick={onClose}>✕</button>
        </span>
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

      <div className="node-panel-body" style={{ display: tab === 'params' ? undefined : 'none' }}>
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
              resXMm={upstreamResX ?? result?.xResMm}
              resYMm={upstreamResY ?? result?.yResMm}
              originCol={upstreamOriginCol}
              originRow={upstreamOriginRow}
              onChange={(next) => onParamChange(nodeId, { ...params, ...next })}
            />
          ) : toolType === 'LineCenter' ? (
            <LineCenterEditor
              rois={(params.rois as Roi[]) ?? []}
              threshold={(params.threshold as number) ?? 1}
              preview={upstreamPreview ?? result?.preview}
              zMin={upstreamZMin ?? result?.zMin}
              zMax={upstreamZMax ?? result?.zMax}
              resXMm={upstreamResX ?? result?.xResMm}
              resYMm={upstreamResY ?? result?.yResMm}
              onChange={(next: LineCenterSettings) =>
                onParamChange(nodeId, { ...params, ...next })
              }
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
              resXMm={upstreamResX ?? result?.xResMm}
              resYMm={upstreamResY ?? result?.yResMm}
              originCol={upstreamOriginCol}
              originRow={upstreamOriginRow}
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

      <div className="node-panel-body" style={{ display: tab === 'result' ? undefined : 'none' }}>
        <ResultView toolType={toolType} result={result} rois={params.rois as Roi[]}
          nodeId={nodeId} params={params} onParamChange={onParamChange}
          originCol={upstreamOriginCol} originRow={upstreamOriginRow} />
      </div>
    </div>
  )
}
