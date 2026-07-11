import { useState, useRef, useCallback, useEffect } from 'react'
import ParamPanel from './ParamPanel'
import { getViewState, patchViewState } from './viewStore'
import PlaneFitEditor, { type PlaneFitROI } from './PlaneFitEditor'
import HeightFromPlaneEditor, { type HeightFromPlaneSettings } from './HeightFromPlaneEditor'
import LineCenterEditor, { type LineCenterSettings } from './LineCenterEditor'
import NoiseFilterEditor from './NoiseFilterEditor'
import { LineCenterOverlay } from './lineCenterViz'
import ImageViewer from './ImageViewer'
import PlaneView3D from './PlaneView3D'
import RoiCanvas, { type Roi } from './RoiCanvas'

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
  // 단계별 미리보기 (ExposureMerge 등) — 결과창 드롭다운으로 선택 조회
  stages?: { name: string; preview: string; zMin?: number; zMax?: number; xResMm?: number; yResMm?: number }[]
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
  pinned?: boolean
  onTogglePin?: () => void
  onClose: () => void
}

function ResultView({ toolType, result, rois, nodeId, params, onParamChange, originCol, originRow, viewKey }: {
  toolType: string; result?: NodeResult; rois?: Roi[]
  nodeId: string; params: Record<string, unknown>
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  originCol?: number; originRow?: number; viewKey?: string
}) {
  const [stageIdx, setStageIdx] = useState(0)
  // cloud 출력 노드: 3D↔2D를 드롭다운으로 전환. ZMapToCloud는 3D, PlaneFit은 2D를 기본으로.
  const [cloudView, setCloudView] = useState(toolType === 'ZMapToCloud')
  const zMin = result?.zMin
  const zMax = result?.zMax
  if (!result) {
    return <div className="param-empty">실행 후 결과가 여기에 표시됩니다</div>
  }

  // cloud를 가진 모든 노드(PlaneFit/ZMapToCloud 등)에서 이미지/3D를 드롭다운으로 전환 (따로 쌓지 않음)
  const hasCloud = !!result.cloud && result.cloud.length > 0
  const showCloud = hasCloud && cloudView

  // 단계별 미리보기가 있으면 선택된 단계를, 없으면 기본 결과 프리뷰를 표시
  const stages = result.stages
  const sel = stages && stages.length ? stages[Math.min(stageIdx, stages.length - 1)] : null
  const dispPreview = sel ? sel.preview : result.preview
  const dispZMin = sel ? sel.zMin : zMin
  const dispZMax = sel ? sel.zMax : zMax
  const dispResX = sel ? sel.xResMm : result.xResMm
  const dispResY = sel ? sel.yResMm : result.yResMm

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

  // HeightMeasure: 각 ROI에서 실제 측정된 대표 점 위치를 십자 마커로 표시.
  // cx,cy는 원점 기준 상대 mm → 픽셀(원점 오프셋 더함)로 변환.
  const measureOverlay = toolType === 'HeightMeasure' && result.imgW && result.measures && dispResX && dispResY
    ? <svg viewBox={`0 0 ${result.imgW} ${result.imgH!}`} preserveAspectRatio="none"
        style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
        {result.measures.map((m, i) => {
          if (m.pointCount === 0) return null
          const px = m.cx / dispResX! + (originCol ?? 0)
          const py = m.cy / dispResY! + (originRow ?? 0)
          const r = Math.max(3, Math.min(result.imgW!, result.imgH!) * 0.006)
          return (
            <g key={i}>
              <circle cx={px} cy={py} r={r} fill="none" stroke="#ff4081" strokeWidth={2} vectorEffect="non-scaling-stroke" />
              <line x1={px - r * 1.8} y1={py} x2={px + r * 1.8} y2={py} stroke="#ff4081" strokeWidth={1} vectorEffect="non-scaling-stroke" />
              <line x1={px} y1={py - r * 1.8} x2={px} y2={py + r * 1.8} stroke="#ff4081" strokeWidth={1} vectorEffect="non-scaling-stroke" />
            </g>
          )
        })}
      </svg>
    : undefined

  return (
    <div className="node-result-view">
      {stages && stages.length > 0 && (
        <div className="node-stage-select">
          <span>단계</span>
          <select className="param-select" value={Math.min(stageIdx, stages.length - 1)}
            onChange={e => setStageIdx(parseInt(e.target.value))}>
            {stages.map((s, i) => <option key={i} value={i}>{s.name}</option>)}
          </select>
        </div>
      )}
      {hasCloud && (
        <div className="node-stage-select">
          <span>보기</span>
          <select className="param-select" value={showCloud ? 'cloud' : 'image'}
            onChange={e => setCloudView(e.target.value === 'cloud')}>
            <option value="cloud">3D 포인트클라우드</option>
            <option value="image">2D 이미지</option>
          </select>
        </div>
      )}
      {showCloud && (
        toolType === 'PlaneFit'
          ? <PlaneView3D a={result.planeA!} b={result.planeB!} c={result.planeC!} points={result.cloud!} />
          : <PlaneView3D points={result.cloud!} showPlane={false} />
      )}
      {dispPreview && !showCloud && (
        <div className="node-result-image-wrap">
          {toolType === 'ExposureMerge' ? (
            <RoiCanvas
              rois={(params.reflRois as Roi[]) ?? []}
              roiTypes={[{ type: 'refl', label: '리플렉션 ROI' }]}
              preview={dispPreview}
              zMin={dispZMin}
              zMax={dispZMax}
              resXMm={dispResX}
              resYMm={dispResY}
              viewKey={viewKey}
              onChange={rois => onParamChange(nodeId, { ...params, reflRois: rois })}
            />
          ) : (
            <ImageViewer
              preview={dispPreview}
              zMin={dispZMin}
              zMax={dispZMax}
              resXMm={dispResX}
              resYMm={dispResY}
              viewKey={viewKey}
              canvasHeight={360}
              rois={toolType === 'HeightMeasure' ? measureRois : undefined}
              roiTypeLabel={() => 'ROI'}
              overlay={lineOverlay ?? alignOverlay ?? measureOverlay}
              overlayFor={(_roi, idx) => {
                const m = result.measures?.[idx]
                return m ? (
                  <span className={`pfe-roi-result ${m.pass ? 'pass' : 'fail'}`}>
                    {m.pointCount === 0 ? '빈 ROI' : `${m.distance.toFixed(4)} mm`}
                  </span>
                ) : null
              }}
            />
          )}
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
                <option value={-1}>선택 안함 (X 변환 없음)</option>
                {found.map(l => <option key={l.roiIndex} value={l.roiIndex}>라인 {l.roiIndex + 1}</option>)}
              </select>
              <span className="node-result-val">{xRoi >= 0 && xLine ? `${xLine.cxMm.toFixed(3)} mm` : '—'}</span>
            </div>
            <div className="param-row">
              <span className="param-label">Y ← 라인</span>
              <select className="param-select" value={yRoi}
                onChange={e => setSel('yRoi', parseInt(e.target.value))}>
                <option value={-1}>선택 안함 (Y 변환 없음)</option>
                {found.map(l => <option key={l.roiIndex} value={l.roiIndex}>라인 {l.roiIndex + 1}</option>)}
              </select>
              <span className="node-result-val">{yRoi >= 0 && yLine ? `${yLine.cyMm.toFixed(3)} mm` : '—'}</span>
            </div>
            <div className="param-empty" style={{ fontSize: 10 }}>
              X 출력 = 선택 라인의 x, Y 출력 = 선택 라인의 y. '선택 안함'이면 다음 좌표계 변환에서 그 축은 변환하지 않습니다.
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
                {m.pointCount === 0
                  ? '빈 ROI'
                  : `${m.distance.toFixed(4)} mm @ (${m.cx.toFixed(2)}, ${m.cy.toFixed(2)}) mm`}
              </span>
            </div>
          ))}
        </div>
      )}

      {result.msg && <div className="node-result-msg">{result.msg}</div>}
    </div>
  )
}

export default function NodePanel({ nodeId, toolType, label, params, result, upstreamPreview, upstreamZMin, upstreamZMax, upstreamResX, upstreamResY, upstreamOriginCol, upstreamOriginRow, width, onWidthChange, onParamChange, onRun, pinned, onTogglePin, onClose }: Props) {
  const [tab, setTab] = useState<'params' | 'result'>(() => getViewState(nodeId).tab ?? 'params')
  // 선택 탭을 노드별로 세션 유지 (패널 토글·노드 전환에도 복원)
  useEffect(() => { patchViewState(nodeId, { tab }) }, [nodeId, tab])
  const dragStartRef = useRef<{ mx: number; w: number } | null>(null)

  const onResizeMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault()
    dragStartRef.current = { mx: e.clientX, w: width }

    const onMove = (ev: MouseEvent) => {
      if (!dragStartRef.current) return
      const dx = dragStartRef.current.mx - ev.clientX   // drag left = wider
      // 최대 폭: 창 너비에서 최소 여백(160px)만 남기고 최대한 넓게
      const maxW = Math.max(600, window.innerWidth - 160)
      onWidthChange(Math.max(220, Math.min(maxW, dragStartRef.current.w + dx)))
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
          {onTogglePin && (
            <button className="node-panel-pin" title={pinned ? '고정 해제' : '패널 고정'} onClick={onTogglePin}>
              {pinned ? '📌' : '📍'}
            </button>
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
              viewKey={nodeId}
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
              viewKey={nodeId}
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
              viewKey={nodeId}
              onChange={(next: HeightFromPlaneSettings) =>
                onParamChange(nodeId, { ...params, ...next })
              }
            />
          ) : toolType === 'NoiseFilter' ? (
            <NoiseFilterEditor
              params={params}
              preview={upstreamPreview ?? result?.preview}
              zMin={upstreamZMin ?? result?.zMin}
              zMax={upstreamZMax ?? result?.zMax}
              resXMm={upstreamResX ?? result?.xResMm}
              resYMm={upstreamResY ?? result?.yResMm}
              originCol={upstreamOriginCol}
              originRow={upstreamOriginRow}
              viewKey={nodeId}
              onChange={(next) => onParamChange(nodeId, next)}
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
          originCol={upstreamOriginCol} originRow={upstreamOriginRow} viewKey={`${nodeId}:result`} />
      </div>
    </div>
  )
}
