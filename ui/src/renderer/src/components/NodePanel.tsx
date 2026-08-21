import { useState, useRef, useCallback, useEffect } from 'react'
import ParamPanel, { NumField } from './ParamPanel'
import { getViewState, patchViewState } from './viewStore'
import PlaneFitEditor, { type PlaneFitROI } from './PlaneFitEditor'
import HeightFromPlaneEditor, { type HeightFromPlaneSettings } from './HeightFromPlaneEditor'
import LineCenterEditor, { type LineCenterSettings } from './LineCenterEditor'
import NoiseFilterEditor from './NoiseFilterEditor'
import RowStretchEditor from './RowStretchEditor'
import ThresholdEditor, { type ThresholdSettings } from './ThresholdEditor'
import CreateRoiEditor, { type CreateRoiSettings } from './CreateRoiEditor'
import { LineCenterOverlay } from './lineCenterViz'
import ImageViewer from './ImageViewer'
import PlaneView3D from './PlaneView3D'
import ProfileChart from './ProfileChart'
import NotchProfileChart from './NotchProfileChart'
import NotchChunkChart from './NotchChunkChart'
import RoiCanvas, { type Roi } from './RoiCanvas'

interface NodeMeasurement { name: string; value: number; unit: string; valid: boolean }
interface NodeDecision { name: string; pass: boolean; reason: string; measured?: number; nominal?: number; tolerance?: number }

// HeightMeasure 오버레이 렌더용 내부 표현
interface HeightMeasure {
  cx: number; cy: number
  distance: number; pointCount: number; pass: boolean
}

interface NodeResult {
  preview?: string
  ok?: boolean
  msg?: string
  // Generic named measurements / decisions
  measurements?: NodeMeasurement[]
  decisions?: NodeDecision[]
  // 3D 포인트클라우드 (PlaneFit overlay, HeightMapToCloud, ExposureMergeCloud)
  cloud?: [number, number, number][]
  // 행별 Profile (CloudToProfiles, ExtractProfile)
  profileCount?: number
  profileMeta?: { label: string; n: number }[]  // 온디맨드 로딩용 메타
  profiles?: { label: string; n: number; x: number[]; z: (number | null)[] }[] // 구형 호환
  // NotchMeasureV2 chunk 시각화용 — 온디맨드 fetch
  notchChunkCount?: number
  // LineCenter — 찾은 모든 라인 (overlay에서 직렬화)
  lines?: { cx: number; cy: number; cxMm: number; cyMm: number; angleDeg: number; roiIndex: number; pointCount: number;
            p0x?: number; p0y?: number; p1x?: number; p1y?: number }[]
  imgW?: number; imgH?: number
  originCol?: number; originRow?: number
  // HeightMap z 범위 + 분해능
  zMin?: number; zMax?: number
  xResMm?: number; yResMm?: number
  // 단계별 미리보기
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
  upstreamCloud?: [number, number, number][]
  width: number
  onWidthChange: (w: number) => void
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  onRun?: (nodeId: string) => void
  pinned?: boolean
  onTogglePin?: () => void
  onClose: () => void
}

// ── 헬퍼: measurements/decisions 이름 조회 ───────────────────────────────
function getMeas(measurements: NodeMeasurement[] | undefined, name: string): number | undefined {
  return measurements?.find(m => m.name === name)?.value
}
function getDec(decisions: NodeDecision[] | undefined, name: string): boolean | undefined {
  return decisions?.find(d => d.name === name)?.pass
}
function extractHeightMeasures(
  measurements: NodeMeasurement[] | undefined,
  decisions: NodeDecision[] | undefined
): HeightMeasure[] {
  const result: HeightMeasure[] = []
  for (let i = 1; ; i++) {
    const cx = getMeas(measurements, `d${i}_cx`)
    if (cx === undefined) break
    result.push({
      cx,
      cy:         getMeas(measurements, `d${i}_cy`) ?? 0,
      distance:   getMeas(measurements, `d${i}_distance`) ?? 0,
      pointCount: Math.round(getMeas(measurements, `d${i}_pointCount`) ?? 0),
      pass:       getDec(decisions, `d${i}_pass`) ?? true,
    })
  }
  return result
}

function ResultView({ toolType, result, rois, nodeId, params, onParamChange, originCol, originRow, viewKey, upstreamCloud }: {
  toolType: string; result?: NodeResult; rois?: Roi[]
  nodeId: string; params: Record<string, unknown>
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  originCol?: number; originRow?: number; viewKey?: string
  upstreamCloud?: [number, number, number][]
}) {
  const [stageIdx, setStageIdx] = useState(0)
  const [cloudView, setCloudView] = useState(toolType === 'HeightMapToCloud' || toolType === 'ExposureMergeCloud')
  const [profRow, setProfRow] = useState(0)
  const [profMode, setProfMode] = useState<'line' | 'points'>('points')
  const [profData, setProfData] = useState<{ x: number[]; z: (number | null)[]; label: string; n: number } | null>(null)
  const [profLoading, setProfLoading] = useState(false)

  const meta = result?.profileMeta
  const metaLen = meta?.length ?? 0

  // profileMeta가 있으면 온디맨드 fetch
  useEffect(() => {
    if (!meta || metaLen === 0) return
    const clampedRow = Math.min(profRow, metaLen - 1)
    const api = window.electronAPI
    if (!api?.engineFetchProfile) return
    setProfLoading(true)
    api.engineFetchProfile(nodeId, clampedRow)
  }, [profRow, meta, metaLen, nodeId])

  // profileData 이벤트 수신
  useEffect(() => {
    if (!meta || metaLen === 0) return
    const api = window.electronAPI
    if (!api?.onEngineEvent) return
    const unsub = api.onEngineEvent((raw: unknown) => {
      const d = raw as { event?: string; nodeId?: string; profileIdx?: number; x?: number[]; z?: (number | null)[]; label?: string; n?: number; error?: string }
      if (d.event !== 'profileData' || d.nodeId !== nodeId) return
      setProfLoading(false)
      if (d.error) { setProfData(null); return }
      setProfData({ x: d.x ?? [], z: d.z ?? [], label: d.label ?? '', n: d.n ?? 0 })
    })
    return unsub
  }, [meta, metaLen, nodeId])

  const [chunkRow, setChunkRow] = useState(0)
  const [notchView, setNotchView] = useState<'inputCloud' | 'outputCloud' | 'measurements' | 'envelope'>('measurements')
  const [notchEnvData, setNotchEnvData] = useState<{
    x: number[]; z: number[]; yFit: (number | null)[]
    notchLoY: number; notchHiY: number; floorCenterY: number; floorZRelUm: number; floorZmm: number
    leftLandZmm?: number; rightLandZmm?: number
    leftEdgeZmm?: number; rightEdgeZmm?: number
    leftEdgeYmm?: number; rightEdgeYmm?: number
  } | null>(null)
  const [notchEnvLoading, setNotchEnvLoading] = useState(false)
  const notchChunkCount = result?.notchChunkCount ?? 0

  // NotchMeasureV2 chunk envelope 온디맨드 fetch
  useEffect(() => {
    if (!notchChunkCount) return
    const api = window.electronAPI
    if (!api?.engineFetchNotchEnv) return
    setNotchEnvLoading(true)
    api.engineFetchNotchEnv(nodeId, Math.min(chunkRow, notchChunkCount - 1))
  }, [chunkRow, notchChunkCount, nodeId])

  useEffect(() => {
    if (!notchChunkCount) return
    const api = window.electronAPI
    if (!api?.onEngineEvent) return
    const unsub = api.onEngineEvent((raw: unknown) => {
      const d = raw as { event?: string; nodeId?: string; chunkIdx?: number; x?: number[]; z?: number[]; yFit?: (number | null)[]; notchLoY?: number; notchHiY?: number; floorCenterY?: number; floorZRelUm?: number; floorZmm?: number; leftLandZmm?: number; rightLandZmm?: number; leftEdgeZmm?: number; rightEdgeZmm?: number; leftEdgeYmm?: number; rightEdgeYmm?: number; error?: string }
      if (d.event !== 'notchEnvData' || d.nodeId !== nodeId) return
      setNotchEnvLoading(false)
      if (d.error) { setNotchEnvData(null); return }
      setNotchEnvData({ x: d.x ?? [], z: d.z ?? [], yFit: d.yFit ?? [], notchLoY: d.notchLoY ?? 0, notchHiY: d.notchHiY ?? 0, floorCenterY: d.floorCenterY ?? 0, floorZRelUm: d.floorZRelUm ?? 0, floorZmm: d.floorZmm ?? 0, leftLandZmm: d.leftLandZmm, rightLandZmm: d.rightLandZmm, leftEdgeZmm: d.leftEdgeZmm, rightEdgeZmm: d.rightEdgeZmm, leftEdgeYmm: d.leftEdgeYmm, rightEdgeYmm: d.rightEdgeYmm })
    })
    return unsub
  }, [notchChunkCount, nodeId])

  const zMin = result?.zMin
  const zMax = result?.zMax
  if (!result) {
    return <div className="param-empty">실행 후 결과가 여기에 표시됩니다</div>
  }

  const hasCloud = !!result.cloud && result.cloud.length > 0
  const showCloud = hasCloud && cloudView

  const stages = result.stages
  const sel = stages && stages.length ? stages[Math.min(stageIdx, stages.length - 1)] : null
  const dispPreview = sel ? sel.preview : result.preview
  const dispZMin = sel ? sel.zMin : zMin
  const dispZMax = sel ? sel.zMax : zMax
  const dispResX = sel ? sel.xResMm : result.xResMm
  const dispResY = sel ? sel.yResMm : result.yResMm

  const oPctX = originCol != null && result.imgW ? originCol / result.imgW : 0
  const oPctY = originRow != null && result.imgH ? originRow / result.imgH : 0
  const measureRois = (rois ?? []).filter(r => r.type === 'measure')
    .map(r => ({ ...r, xPct: r.xPct + oPctX, yPct: r.yPct + oPctY }))

  const searchRois = (rois ?? []).filter(r => r.type === 'search')
  const lineOverlay = toolType === 'LineCenter' && result.imgW && result.lines
    ? <>{result.lines.map((l, i) => (
        <LineCenterOverlay key={i} cx={l.cx} cy={l.cy} angleDeg={l.angleDeg}
          imgW={result.imgW!} imgH={result.imgH!} roi={searchRois[l.roiIndex]}
          label={`${l.roiIndex + 1}`} />
      ))}</>
    : undefined

  // LineFit: 검출 라인 세그먼트(끝점 p0→p1) + 중심점 그리기
  const lineFitOverlay = toolType === 'LineFit' && result.imgW && result.imgH && result.lines && result.lines.length
    ? <svg viewBox={`0 0 ${result.imgW} ${result.imgH}`} preserveAspectRatio="none"
        style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
        {result.lines.map((l, i) => {
          const hasEnds = l.p0x !== undefined && (l.p0x !== 0 || l.p0y !== 0 || l.p1x !== 0 || l.p1y !== 0)
          const r = Math.max(2, Math.min(result.imgW!, result.imgH!) * 0.012)
          return (
            <g key={i}>
              {hasEnds && (
                <line x1={l.p0x} y1={l.p0y} x2={l.p1x} y2={l.p1y}
                  stroke="#00e676" strokeWidth={2} vectorEffect="non-scaling-stroke" />
              )}
              <line x1={l.cx - r} y1={l.cy} x2={l.cx + r} y2={l.cy} stroke="#ff5252" strokeWidth={2} vectorEffect="non-scaling-stroke" />
              <line x1={l.cx} y1={l.cy - r} x2={l.cx} y2={l.cy + r} stroke="#ff5252" strokeWidth={2} vectorEffect="non-scaling-stroke" />
            </g>
          )
        })}
      </svg>
    : undefined

  // Align 오버레이: measurements에서 offCol/offRow 조회
  const offCol = getMeas(result.measurements, 'offCol')
  const offRow = getMeas(result.measurements, 'offRow')
  const alignOverlay = toolType === 'Align' && result.imgW && offCol !== undefined
    ? <svg viewBox={`0 0 ${result.imgW} ${result.imgH!}`} preserveAspectRatio="none"
        style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
        <line x1={0} y1={offRow!} x2={result.imgW} y2={offRow!}
          stroke="#ffca28" strokeWidth={1} strokeDasharray="6 4" vectorEffect="non-scaling-stroke" />
        <line x1={offCol} y1={0} x2={offCol} y2={result.imgH!}
          stroke="#ffca28" strokeWidth={1} strokeDasharray="6 4" vectorEffect="non-scaling-stroke" />
        <circle cx={offCol} cy={offRow!} r={Math.max(3, Math.min(result.imgW, result.imgH!) * 0.012)}
          fill="none" stroke="#ffca28" strokeWidth={2} vectorEffect="non-scaling-stroke" />
      </svg>
    : undefined

  // HeightMeasure 오버레이: measurements/decisions에서 ROI별 데이터 추출
  const heightMeasures = toolType === 'HeightMeasure'
    ? extractHeightMeasures(result.measurements, result.decisions)
    : undefined
  const measureOverlay = toolType === 'HeightMeasure' && result.imgW && heightMeasures && heightMeasures.length > 0 && dispResX && dispResY
    ? <svg viewBox={`0 0 ${result.imgW} ${result.imgH!}`} preserveAspectRatio="none"
        style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
        {heightMeasures.map((m, i) => {
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

  // PlaneFit 평면 파라미터
  const planeA = getMeas(result.measurements, 'planeA')
  const planeB = getMeas(result.measurements, 'planeB')
  const planeC = getMeas(result.measurements, 'planeC')

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
            <option value="image">HeightMap 이미지</option>
          </select>
        </div>
      )}
      {showCloud && (
        toolType === 'PlaneFit' && planeA !== undefined
          ? <PlaneView3D a={planeA} b={planeB!} c={planeC!} points={result.cloud!} />
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
              overlay={lineOverlay ?? alignOverlay ?? measureOverlay ?? lineFitOverlay}
              overlayFor={(_roi, idx) => {
                const m = heightMeasures?.[idx]
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

      {toolType === 'PlaneFit' && planeA !== undefined && (
        <div className="node-result-measures">
          <div className="node-result-row">
            <span className="node-result-label">평면식</span>
            <span className="node-result-val">z = {planeA.toFixed(5)}·x + {planeB!.toFixed(5)}·y + {planeC!.toFixed(4)}</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">RMSE</span>
            <span className="node-result-val">{getMeas(result.measurements, 'rmse')?.toFixed(4)} mm</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">기울기</span>
            <span className="node-result-val">{getMeas(result.measurements, 'tiltDeg')?.toFixed(3)}°</span>
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

      {toolType === 'Align' && offCol !== undefined && (
        <div className="node-result-measures">
          <div className="node-result-row">
            <span className="node-result-label">원점 (px)</span>
            <span className="node-result-val">({offCol.toFixed(1)}, {offRow?.toFixed(1) ?? '—'})</span>
          </div>
          <div className="node-result-row">
            <span className="node-result-label">이동량 (mm)</span>
            <span className="node-result-val">({getMeas(result.measurements, 'offXMm')?.toFixed(3) ?? '—'}, {getMeas(result.measurements, 'offYMm')?.toFixed(3) ?? '—'})</span>
          </div>
        </div>
      )}

      {toolType === 'HeightMeasure' && heightMeasures && heightMeasures.length > 0 && (
        <div className="node-result-measures">
          {heightMeasures.map((m, i) => (
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

      {/* 라인 밴드 모드 — 런타임 생성 ROI 목록 (result.lines 외곽선 기반). 정적 ROI와 동일 형식 + mm/px 병기 */}
      {toolType === 'CreateROI' && (() => {
        const bandLines = result.lines ?? []
        if (bandLines.length === 0) return null
        const idxs = Array.from(new Set(bandLines.map(l => l.roiIndex))).sort((a, b) => a - b)
        const side = (params.bandSide as string) ?? 'both'
        const labels = side === 'left' ? ['left'] : side === 'right' ? ['right'] : ['left', 'right']
        const rx = result.xResMm ?? upstreamResX ?? 1
        const ry = result.yResMm ?? upstreamResY ?? 1
        const oc = result.originCol ?? upstreamOriginCol ?? 0
        const or = result.originRow ?? upstreamOriginRow ?? 0
        const dist = (ax: number, ay: number, bx: number, by: number) => Math.hypot(bx - ax, by - ay)
        return (
          <div className="node-result-measures">
            <div className="node-result-row" style={{ fontWeight: 600, opacity: 0.8 }}>
              <span className="node-result-label">라인 밴드 ROI</span>
              <span className="node-result-val">{idxs.length}개</span>
            </div>
            {idxs.map((ri, k) => {
              const es = bandLines.filter(l => l.roiIndex === ri && l.p0x !== undefined)
              if (es.length < 4) return null
              // 코너 px (c0,c1,c2,c3 순). c0-c1=폭변, c1-c2=길이변.
              const cpx = es.slice(0, 4).map(e => ({ x: e.p0x!, y: e.p0y! }))
              const cenPx = { x: cpx.reduce((s, p) => s + p.x, 0) / 4, y: cpx.reduce((s, p) => s + p.y, 0) / 4 }
              const lenPx = dist(cpx[1].x, cpx[1].y, cpx[2].x, cpx[2].y)
              const widPx = dist(cpx[0].x, cpx[0].y, cpx[1].x, cpx[1].y)
              const angPx = Math.atan2(cpx[2].y - cpx[1].y, cpx[2].x - cpx[1].x) * 180 / Math.PI
              // mm (코너를 mm로 변환 후 계산 — 비등방 정확)
              const cmm = cpx.map(p => ({ x: (p.x - oc) * rx, y: (p.y - or) * ry }))
              const cenMm = { x: cmm.reduce((s, p) => s + p.x, 0) / 4, y: cmm.reduce((s, p) => s + p.y, 0) / 4 }
              const lenMm = dist(cmm[1].x, cmm[1].y, cmm[2].x, cmm[2].y)
              const widMm = dist(cmm[0].x, cmm[0].y, cmm[1].x, cmm[1].y)
              const angMm = Math.atan2(cmm[2].y - cmm[1].y, cmm[2].x - cmm[1].x) * 180 / Math.PI
              return (
                <div className="node-result-row" key={ri} style={{ flexDirection: 'column', alignItems: 'flex-start', gap: 2 }}>
                  <span className="node-result-label">{k + 1}. {labels[k] ?? `밴드${ri}`} · {angMm.toFixed(1)}°</span>
                  <span className="node-result-val">중심({cenMm.x.toFixed(2)}, {cenMm.y.toFixed(2)})mm · {lenMm.toFixed(2)}×{widMm.toFixed(2)}mm</span>
                  <span className="node-result-val" style={{ opacity: 0.6, fontSize: 10 }}>중심({cenPx.x.toFixed(0)}, {cenPx.y.toFixed(0)})px · {lenPx.toFixed(0)}×{widPx.toFixed(0)}px · {angPx.toFixed(1)}°</span>
                </div>
              )
            })}
          </div>
        )
      })()}

      {toolType === 'CreateROI' && (() => {
        const roiList = (params.rois as Roi[]) ?? []
        if (roiList.length === 0) return null
        const hasMm = !!result.xResMm && !!result.yResMm && !!result.imgW && !!result.imgH
        const fx = hasMm ? result.imgW! * result.xResMm! : (result.imgW ?? 0)
        const fy = hasMm ? result.imgH! * result.yResMm! : (result.imgH ?? 0)
        const u = hasMm ? ' mm' : ' px'
        const n = (v: number) => v.toFixed(hasMm ? 2 : 0)
        return (
          <div className="node-result-measures">
            {roiList.map((r, i) => {
              const shape = r.shape ?? 'rect'
              const label = shape === 'circle' ? '원' : shape === 'polygon' ? '폴리곤' : '사각'
              const ang = (r.angleDeg ?? 0) !== 0 ? ` · ${(r.angleDeg ?? 0).toFixed(1)}°` : ''
              let detail: string
              if (shape === 'polygon') {
                const pts = r.points ?? []
                detail = `꼭짓점 ${pts.length}개 · bbox ${n(r.wPct * fx)}×${n(r.hPct * fy)}${u}`
              } else if (shape === 'circle') {
                const cx = (r.xPct + r.wPct / 2) * fx, cy = (r.yPct + r.hPct / 2) * fy
                const rad = (r.wPct * fx) / 2
                detail = `중심(${n(cx)}, ${n(cy)})${u} · R ${n(rad)}${u}`
              } else {
                detail = `(${n(r.xPct * fx)}, ${n(r.yPct * fy)}) · ${n(r.wPct * fx)}×${n(r.hPct * fy)}${u}`
              }
              return (
                <div className="node-result-row" key={r.id ?? i}>
                  <span className="node-result-label">{i + 1}. {label}{ang}</span>
                  <span className="node-result-val">{detail}</span>
                </div>
              )
            })}
          </div>
        )
      })()}

      {/* NotchMeasureV2 통합 결과창 — 4개 드롭박스 뷰 전환 */}
      {toolType === 'NotchMeasureV2' && (() => {
        const byLabel = (label: string) => result.profiles?.find(p => p.label === label)
        const depthL   = byLabel('depth_left_um')
        const depthR   = byLabel('depth_right_um')
        const depthC   = byLabel('depth_combined_um')
        const depthLE  = byLabel('depth_left_edge_um')
        const depthRE  = byLabel('depth_right_edge_um')
        const floorZ   = byLabel('notch_floor_z_mm')
        const landL    = byLabel('land_left_z_mm')
        const landR    = byLabel('land_right_z_mm')
        const clampedChunk = Math.min(chunkRow, Math.max(0, notchChunkCount - 1))
        const hasCloud = !!result?.cloud && result.cloud.length > 0
        const hasProfiles = !!result?.profiles && result.profiles.length > 0
        return (
          <div className="node-result-measures">
            <div className="node-result-row" style={{ fontWeight: 600, opacity: 0.8 }}>
              <span className="node-result-label">NotchMeasureV2 결과</span>
              <select className="param-select" value={notchView}
                onChange={e => setNotchView(e.target.value as 'inputCloud' | 'outputCloud' | 'measurements' | 'envelope')}
                style={{ fontSize: 11 }}>
                <option value="inputCloud">입력 pointcloud</option>
                <option value="outputCloud">최종 출력 pointcloud</option>
                <option value="measurements">각 높이 측정 그래프</option>
                <option value="envelope">프로파일별 그래프</option>
              </select>
            </div>

            {/* 입력 pointcloud */}
            {notchView === 'inputCloud' && (
              upstreamCloud && upstreamCloud.length > 0
                ? <PlaneView3D points={upstreamCloud} showPlane={false} />
                : <div className="param-empty" style={{ fontSize: 11 }}>상류 노드 cloud 없음</div>
            )}

            {/* 최종 출력 pointcloud */}
            {notchView === 'outputCloud' && (
              hasCloud
                ? <PlaneView3D points={result!.cloud!} showPlane={false} />
                : <div className="param-empty" style={{ fontSize: 11 }}>출력 cloud 없음 (먼저 실행하세요)</div>
            )}

            {/* 각 높이 측정 그래프 */}
            {notchView === 'measurements' && hasProfiles && (
              <>
                {depthL && depthR && depthC && (
                  <NotchProfileChart series={[
                    { label: 'depth left',     x: depthL.x, z: depthL.z, color: '#eb6834', bold: true },
                    { label: 'depth right',    x: depthR.x, z: depthR.z, color: '#1baf7a', bold: true },
                    { label: 'depth combined', x: depthC.x, z: depthC.z, color: '#3987e5' },
                  ]} unit="µm" />
                )}
                {depthLE && depthRE && (
                  <NotchProfileChart series={[
                    { label: 'depth left (edge)',  x: depthLE.x, z: depthLE.z, color: '#00bcd4', bold: true },
                    { label: 'depth right (edge)', x: depthRE.x, z: depthRE.z, color: '#e040fb', bold: true },
                  ]} unit="µm" />
                )}
                {floorZ && landL && landR && (
                  <NotchProfileChart series={[
                    { label: 'floor z',    x: floorZ.x, z: floorZ.z, color: '#3987e5', bold: true },
                    { label: 'land left',  x: landL.x,  z: landL.z,  color: '#eb6834' },
                    { label: 'land right', x: landR.x,  z: landR.z,  color: '#1baf7a' },
                  ]} unit="mm" />
                )}
                {!depthL && !floorZ && (
                  <div className="param-empty" style={{ fontSize: 11 }}>측정 결과 없음 (먼저 실행하세요)</div>
                )}
              </>
            )}
            {notchView === 'measurements' && !hasProfiles && (
              <div className="param-empty" style={{ fontSize: 11 }}>측정 결과 없음 (먼저 실행하세요)</div>
            )}

            {/* 프로파일별 그래프 */}
            {notchView === 'envelope' && (
              notchChunkCount > 0 ? (
                <>
                  <div className="param-row">
                    <span className="param-label">chunk</span>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 6, flex: 1 }}>
                      <button className="param-btn" style={{ padding: '1px 6px' }}
                        onClick={() => setChunkRow(r => Math.max(0, r - 1))}>◀</button>
                      <input type="range" min={0} max={notchChunkCount - 1} step={1} value={clampedChunk}
                        style={{ flex: 1 }} onChange={e => setChunkRow(parseInt(e.target.value))} />
                      <input type="number" min={0} max={notchChunkCount - 1} value={clampedChunk}
                        style={{ width: 48, textAlign: 'right', background: '#1a1d24', border: '1px solid #333', color: '#ccc', borderRadius: 3, fontSize: 11 }}
                        onChange={e => { const v = parseInt(e.target.value); if (!isNaN(v)) setChunkRow(Math.max(0, Math.min(notchChunkCount - 1, v))) }} />
                      <button className="param-btn" style={{ padding: '1px 6px' }}
                        onClick={() => setChunkRow(r => Math.min(notchChunkCount - 1, r + 1))}>▶</button>
                    </div>
                  </div>
                  {notchEnvLoading && <div className="param-empty" style={{ fontSize: 11 }}>로딩 중…</div>}
                  {notchEnvData && (
                    <NotchChunkChart
                      x={notchEnvData.x} z={notchEnvData.z} yFit={notchEnvData.yFit}
                      notchLoY={notchEnvData.notchLoY} notchHiY={notchEnvData.notchHiY}
                      floorCenterY={notchEnvData.floorCenterY} floorZRelUm={notchEnvData.floorZRelUm}
                      floorZmm={notchEnvData.floorZmm}
                      leftLandZmm={notchEnvData.leftLandZmm}
                      rightLandZmm={notchEnvData.rightLandZmm}
                      leftEdgeZmm={notchEnvData.leftEdgeZmm}
                      rightEdgeZmm={notchEnvData.rightEdgeZmm}
                      leftEdgeYmm={notchEnvData.leftEdgeYmm}
                      rightEdgeYmm={notchEnvData.rightEdgeYmm}
                    />
                  )}
                </>
              ) : (
                <div className="param-empty" style={{ fontSize: 11 }}>프로파일 없음 (먼저 실행하세요)</div>
              )
            )}
          </div>
        )
      })()}

      {/* 행별 Profile 형상 차트 (CloudToProfiles, ExtractProfile) — NotchMeasureV2 제외 */}
      {toolType !== 'NotchMeasureV2' && (() => {
        const useMeta = meta && metaLen > 0
        const useOld = !useMeta && result.profiles && result.profiles.length > 0
        if (!useMeta && !useOld) return null

        const totalRows = useMeta ? metaLen : result.profiles!.length
        const idx = Math.min(profRow, totalRows - 1)
        const curMeta = useMeta ? meta![idx] : { label: result.profiles![idx].label, n: result.profiles![idx].n }
        const chartData = useMeta ? profData : result.profiles![idx]

        return (
          <div className="node-result-measures">
            <div className="node-result-row" style={{ fontWeight: 600, opacity: 0.8 }}>
              <span className="node-result-label">프로파일</span>
              <span className="node-result-val">{result.profileCount ?? totalRows}개</span>
            </div>
            <div className="param-row">
              <span className="param-label">행</span>
              <div style={{ display: 'flex', alignItems: 'center', gap: 4, flex: 1 }}>
                <button
                  style={{ padding: '1px 6px', fontSize: 12, lineHeight: 1.4, cursor: 'pointer', flexShrink: 0 }}
                  disabled={idx === 0}
                  onClick={() => setProfRow(idx - 1)}
                >◀</button>
                <input type="range" min={0} max={totalRows - 1} step={1} value={idx}
                  style={{ flex: 1 }}
                  onChange={e => setProfRow(parseInt(e.target.value))} />
                <button
                  style={{ padding: '1px 6px', fontSize: 12, lineHeight: 1.4, cursor: 'pointer', flexShrink: 0 }}
                  disabled={idx === totalRows - 1}
                  onClick={() => setProfRow(idx + 1)}
                >▶</button>
                <input
                  type="number"
                  min={0}
                  max={totalRows - 1}
                  value={idx}
                  style={{ width: 52, fontSize: 12, textAlign: 'center', flexShrink: 0 }}
                  onChange={e => {
                    const v = parseInt(e.target.value)
                    if (!isNaN(v)) setProfRow(Math.max(0, Math.min(totalRows - 1, v)))
                  }}
                />
                <span className="node-result-val" style={{ whiteSpace: 'nowrap', minWidth: 72, textAlign: 'right' }}>
                  {curMeta.label || ''} ({curMeta.n}점)
                </span>
              </div>
            </div>
            <div className="param-row">
              <span className="param-label">표시</span>
              <select className="param-select" value={profMode}
                onChange={e => setProfMode(e.target.value as 'line' | 'points')}>
                <option value="points">점</option>
                <option value="line">선</option>
              </select>
            </div>
            {profLoading && <div className="param-empty" style={{ fontSize: 11 }}>로딩 중…</div>}
            {chartData && <ProfileChart x={chartData.x} z={chartData.z} mode={profMode} />}
          </div>
        )
      })()}

      {/* 범용 측정값 테이블 — 커스텀 렌더 없는 툴(RegionMeasure, LineFit 등) */}
      {result.measurements && result.measurements.length > 0
        && !['PlaneFit', 'Align', 'HeightMeasure'].includes(toolType) && (
        <div className="node-result-measures">
          {result.measurements.map((m, i) => (
            <div className={`node-result-row ${m.valid ? '' : 'fail-val'}`} key={`${m.name}-${i}`}>
              <span className="node-result-label">{m.name}</span>
              <span className="node-result-val">
                {Number.isFinite(m.value) ? m.value.toFixed(4) : '—'}{m.unit ? ` ${m.unit}` : ''}
              </span>
            </div>
          ))}
        </div>
      )}

      {result.msg && <div className="node-result-msg">{result.msg}</div>}
    </div>
  )
}

export default function NodePanel({ nodeId, toolType, label, params, result, upstreamPreview, upstreamZMin, upstreamZMax, upstreamResX, upstreamResY, upstreamOriginCol, upstreamOriginRow, upstreamCloud, width, onWidthChange, onParamChange, onRun, pinned, onTogglePin, onClose }: Props) {
  const [tab, setTab] = useState<'params' | 'result'>(() => getViewState(nodeId).tab ?? 'params')
  useEffect(() => { patchViewState(nodeId, { tab }) }, [nodeId, tab])
  const dragStartRef = useRef<{ mx: number; w: number } | null>(null)

  const onResizeMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault()
    dragStartRef.current = { mx: e.clientX, w: width }

    const onMove = (ev: MouseEvent) => {
      if (!dragStartRef.current) return
      const dx = dragStartRef.current.mx - ev.clientX
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
          ) : toolType === 'Threshold' ? (
            <ThresholdEditor
              channel={(params.channel as number) ?? 0}
              thresholdMode={(params.thresholdMode as 'mm' | 'raw') ?? 'mm'}
              thresholdMm={(params.thresholdMm as number) ?? 0}
              thresholdRaw={(params.thresholdRaw as number) ?? 0}
              keepAbove={(params.keepAbove as boolean) ?? true}
              preview={upstreamPreview ?? result?.preview}
              zMin={upstreamZMin ?? result?.zMin}
              zMax={upstreamZMax ?? result?.zMax}
              resXMm={upstreamResX ?? result?.xResMm}
              resYMm={upstreamResY ?? result?.yResMm}
              viewKey={nodeId}
              onChange={(next: ThresholdSettings) => onParamChange(nodeId, { ...params, ...next })}
            />
          ) : toolType === 'CreateROI' ? (
            <>
              <div className="node-result-measures" style={{ marginBottom: 8 }}>
                <div className="node-result-row" style={{ fontWeight: 600, opacity: 0.8 }}>
                  라인 밴드 (포트1 Line 연결 시)
                </div>
                <NumField label="폭(mm)" step={0.5}
                  value={(params.bandWidthMm as number) ?? 5}
                  onChange={v => onParamChange(nodeId, { ...params, bandWidthMm: v })} />
                <NumField label="오프셋(mm)" step={0.5}
                  value={(params.bandOffsetMm as number) ?? 3}
                  onChange={v => onParamChange(nodeId, { ...params, bandOffsetMm: v })} />
                <div className="param-row">
                  <span className="param-label">방향</span>
                  <select className="param-select" value={(params.bandSide as string) ?? 'both'}
                    onChange={e => onParamChange(nodeId, { ...params, bandSide: e.target.value })}>
                    <option value="both">both</option>
                    <option value="left">left</option>
                    <option value="right">right</option>
                  </select>
                </div>
                <div className="param-row">
                  <span className="param-label">길이</span>
                  <select className="param-select" value={(params.bandLenMode as string) ?? 'line'}
                    onChange={e => onParamChange(nodeId, { ...params, bandLenMode: e.target.value })}>
                    <option value="line">라인 실제</option>
                    <option value="fixed">고정</option>
                  </select>
                </div>
                {(params.bandLenMode as string) === 'fixed' && (
                  <NumField label="고정길이(mm)" step={1}
                    value={(params.bandLengthMm as number) ?? 10}
                    onChange={v => onParamChange(nodeId, { ...params, bandLengthMm: v })} />
                )}
              </div>
              <CreateRoiEditor
                rois={(params.rois as Roi[]) ?? []}
                preview={upstreamPreview ?? result?.preview}
                zMin={upstreamZMin ?? result?.zMin}
                zMax={upstreamZMax ?? result?.zMax}
                resXMm={upstreamResX ?? result?.xResMm}
                resYMm={upstreamResY ?? result?.yResMm}
                originCol={upstreamOriginCol}
                originRow={upstreamOriginRow}
                viewKey={nodeId}
                overlay={result?.lines && result.lines.length && result.imgW && result.imgH ? (
                  <svg viewBox={`0 0 ${result.imgW} ${result.imgH}`} preserveAspectRatio="none"
                    style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
                    {result.lines.map((l, i) => (
                      (l.p0x !== undefined && l.p1x !== undefined) ? (
                        <line key={i} x1={l.p0x} y1={l.p0y} x2={l.p1x} y2={l.p1y}
                          stroke={l.roiIndex === 0 ? '#00e5ff' : '#ffca28'} strokeWidth={2}
                          vectorEffect="non-scaling-stroke" />
                      ) : null
                    ))}
                  </svg>
                ) : undefined}
                onChange={(next: CreateRoiSettings) => onParamChange(nodeId, { ...params, ...next })}
              />
            </>
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
          ) : toolType === 'RowStretch' ? (
            <RowStretchEditor
              params={params}
              preview={upstreamPreview}
              zMin={upstreamZMin}
              zMax={upstreamZMax}
              resXMm={upstreamResX}
              resYMm={upstreamResY}
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
          originCol={upstreamOriginCol} originRow={upstreamOriginRow} viewKey={`${nodeId}:result`}
          upstreamCloud={upstreamCloud} />
      </div>
    </div>
  )
}
