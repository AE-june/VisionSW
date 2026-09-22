import { useState, useRef, useCallback, useEffect, useMemo } from 'react'
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
import ProfileChart, { type CaliperFeature, type CaliperLineFit, type ProfileAnnotation } from './ProfileChart'
import ProfileCaliperEditor from './ProfileCaliperEditor'
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
  upstreamProfileNodeId?: string
  upstreamProfileMeta?: { label: string; n: number }[]
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

// ── ProfileCaliper 결과 뷰 ───────────────────────────────────────────────
// ROI element 단일 중립색 (많아도 안 정신없게). 구분은 R0/R1 라벨로.
const CALIPER_COLOR = '#9aa7b4'

interface CaliperElement {
  type?: 'point' | 'line'
  fromMm?: number; toMm?: number
  [k: string]: unknown
}
interface CaliperMeasurementDef {
  combo?: string; metric?: string
  refA?: number; refB?: number
  nominalMm?: number; plusMm?: number; minusMm?: number
  nominal?: number; plus?: number; minus?: number
  [k: string]: unknown
}

// 엔진 fetchProfile 응답에 실려오는 per-profile caliper 분석 (온디맨드 오버레이)
interface CaliperElemView {
  type: string; valid: boolean
  sMm: number; zMm: number
  slope: number; intercept: number; rmse: number
  fromMm: number; toMm: number
}
interface CaliperMeasView { value: number; unit: string; hasDecision: boolean; pass: boolean }
interface CaliperFetched { elems: CaliperElemView[]; meas: CaliperMeasView[] }

/** 라인 z=ms+b 위로 점(s0,z0)의 수선의 발 */
function perpFoot(s0: number, z0: number, m: number, b: number): { s: number; z: number } {
  const sf = (s0 + m * (z0 - b)) / (m * m + 1)
  return { s: sf, z: m * sf + b }
}

function ProfileCaliperResult({ x, z, mode, params, measurements, decisions, caliper, rawOpen, onToggleRaw }: {
  x: number[]; z: (number | null)[]; mode: 'line' | 'points'
  params: Record<string, unknown>
  measurements?: NodeMeasurement[]; decisions?: NodeDecision[]
  caliper?: CaliperFetched
  rawOpen: boolean; onToggleRaw: () => void
}) {
  const elements = (params.elements as CaliperElement[] | undefined) ?? []
  const measDefs = (params.measurements as CaliperMeasurementDef[] | undefined) ?? []

  // fetchProfile로 받은 이 프로파일의 caliper 분석 우선. 없으면 plain 이름(폴백).
  const fc = caliper

  // plain-name 폴백 맵 (fc 없을 때만 사용). fc가 있으면 빌드 스킵.
  const mmap = useMemo<Record<string, number>>(() => {
    if (fc) return {}
    const m: Record<string, number> = {}
    for (const x of measurements ?? []) if (x.valid) m[x.name] = x.value
    return m
  }, [fc, measurements])
  const munit = useMemo<Record<string, string>>(() => {
    if (fc) return {}
    const m: Record<string, string> = {}
    for (const x of measurements ?? []) m[x.name] = x.unit
    return m
  }, [fc, measurements])
  const dmap = useMemo<Record<string, NodeDecision>>(() => {
    // dmap['allPass'] 폴백은 fc 유무와 무관하게 사용되므로 fc가 있어도 빌드
    const m: Record<string, NodeDecision> = {}
    for (const d of decisions ?? []) m[d.name] = d
    return m
  }, [decisions])

  // 측정 행 확장 상태 (부수값 펼침)
  const [expandedMeas, setExpandedMeas] = useState<Record<number, boolean>>({})
  const toggleMeasRow = (i: number) => setExpandedMeas(m => ({ ...m, [i]: !m[i] }))

  // element 좌표 해석 — fc 우선, 폴백은 mmap
  interface ElemPoint { kind: 'point'; sMm: number; zMm: number }
  interface ElemLine { kind: 'line'; slope: number; intercept: number; rmse: number; fromMm: number; toMm: number }
  const resolved: (ElemPoint | ElemLine | null)[] = elements.map((el, i) => {
    if (fc) {
      const ev = fc.elems[i]
      if (!ev || !ev.valid) return null
      if (ev.type === 'line')
        return { kind: 'line', slope: ev.slope, intercept: ev.intercept, rmse: ev.rmse, fromMm: ev.fromMm, toMm: ev.toMm }
      return { kind: 'point', sMm: ev.sMm, zMm: ev.zMm }
    }
    if (el.type === 'point') {
      const sMm = mmap[`elem[${i}].sMm`]
      const zMm = mmap[`elem[${i}].zMm`]
      if (sMm === undefined) return null
      return { kind: 'point', sMm, zMm: zMm ?? 0 }
    }
    if (el.type === 'line') {
      const slope = mmap[`elem[${i}].slope`]
      const intercept = mmap[`elem[${i}].intercept`]
      if (slope === undefined || intercept === undefined) return null
      return {
        kind: 'line', slope, intercept,
        rmse: mmap[`elem[${i}].rmse`] ?? 0,
        fromMm: mmap[`elem[${i}].fromMm`] ?? el.fromMm ?? 0,
        toMm: mmap[`elem[${i}].toMm`] ?? el.toMm ?? 0,
      }
    }
    return null
  })

  // point features
  const features: CaliperFeature[] = []
  resolved.forEach((r, i) => {
    if (r && r.kind === 'point') features.push({ sMm: r.sMm, zMm: r.zMm, kind: 'point', label: `R${i}` })
  })

  // line fits
  const lineFits: CaliperLineFit[] = []
  resolved.forEach(r => {
    if (r && r.kind === 'line') lineFits.push({ fromMm: r.fromMm, toMm: r.toMm, slope: r.slope, intercept: r.intercept })
  })

  // 측정값/판정 헬퍼 — fc 우선, 폴백은 mmap/dmap
  const measValue = (i: number): number | undefined => {
    const md = measDefs[i]
    if (fc && md?.combo === 'pl') {
      const a = resolved[md.refA ?? 0]
      const b = resolved[md.refB ?? 0]
      const pt = a?.kind === 'point' ? a : b?.kind === 'point' ? b : null
      const ln = a?.kind === 'line' ? a : b?.kind === 'line' ? b : null
      if (pt && ln) {
        if (md.metric === 'zDist') return Math.abs(pt.zMm - (ln.slope * pt.sMm + ln.intercept))
        return Math.abs(ln.slope * pt.sMm - pt.zMm + ln.intercept) / Math.sqrt(ln.slope * ln.slope + 1)
      }
    }
    return fc ? fc.meas[i]?.value : mmap[`meas[${i}]`]
  }
  const measDecPass = (i: number): boolean | undefined =>
    fc ? (fc.meas[i]?.hasDecision ? fc.meas[i].pass : undefined) : dmap[`meas[${i}]`]?.pass
  const measUnit = (i: number): string | undefined => fc ? fc.meas[i]?.unit : munit[`meas[${i}]`]
  const measHasDec = (i: number): boolean => fc ? !!fc.meas[i]?.hasDecision : !!dmap[`meas[${i}]`]
  const decColor = (i: number): string => {
    const pass = measDecPass(i)
    if (pass === undefined) return '#bbb'  // 공차 없음 → 중립 회색
    return pass ? '#4caf50' : '#f44336'
  }

  // annotations from measurement definitions
  const annotations: ProfileAnnotation[] = []
  measDefs.forEach((md, i) => {
    const combo = md.combo ?? ''
    const a = resolved[md.refA ?? -1]
    const b = resolved[md.refB ?? -1]
    const val = measValue(i)
    const isDeg = (md.metric === 'angle' || md.metric === 'tilt') || measUnit(i) === 'deg'
    const uStr = isDeg ? '°' : 'mm'
    const label = `M${i}` + (val !== undefined ? ` ${val.toFixed(2)}${uStr}` : '')
    const color = decColor(i)
    if (combo === 'pp') {
      if (a?.kind === 'point' && b?.kind === 'point')
        annotations.push({ kind: 'segment', s1: a.sMm, z1: a.zMm, s2: b.sMm, z2: b.zMm, label, color })
    } else if (combo === 'pl') {
      // point = refA, line = refB (or swapped)
      const pt = a?.kind === 'point' ? a : b?.kind === 'point' ? b : null
      const ln = a?.kind === 'line' ? a : b?.kind === 'line' ? b : null
      if (pt && ln) {
        if (md.metric === 'zDist') {
          const zOnLine = ln.slope * pt.sMm + ln.intercept
          annotations.push({ kind: 'perp', s1: pt.sMm, z1: pt.zMm, s2: pt.sMm, z2: zOnLine, label, color })
        } else {
          const foot = perpFoot(pt.sMm, pt.zMm, ln.slope, ln.intercept)
          annotations.push({ kind: 'perp', s1: pt.sMm, z1: pt.zMm, s2: foot.s, z2: foot.z, label, color, lineSlope: ln.slope, lineIntercept: ln.intercept })
        }
      }
    } else if (combo === 'll') {
      const l1 = a?.kind === 'line' ? a : null
      const l2 = b?.kind === 'line' ? b : null
      if (l1 && l2) {
        const dm = l1.slope - l2.slope
        let si: number, zi: number
        if (Math.abs(dm) < 1e-12) {
          const sCenter = (Math.max(l1.fromMm, l2.fromMm) + Math.min(l1.toMm, l2.toMm)) / 2
          si = isFinite(sCenter) ? sCenter : (l1.fromMm + l1.toMm) / 2
          zi = l1.slope * si + l1.intercept
        } else {
          si = (l2.intercept - l1.intercept) / dm
          zi = l1.slope * si + l1.intercept
        }
        annotations.push({ kind: 'angle', s1: si, z1: zi, s2: si, z2: zi, label, color })
      }
    } else {
      if (a?.kind === 'point')
        annotations.push({ kind: 'segment', s1: a.sMm, z1: a.zMm, s2: a.sMm, z2: a.zMm, label, color })
    }
  })

  // verdict — fc가 있으면 이 프로파일의 measurement 판정만 집계, 없으면 dmap['allPass'] 폴백
  const allPassDec = dmap['allPass']
  let failCount = 0, totalMeas = 0
  measDefs.forEach((_md, i) => {
    if (measHasDec(i)) {
      totalMeas++
      if (measDecPass(i) === false) failCount++
    }
  })
  const overallPass = fc ? failCount === 0 : (allPassDec ? allPassDec.pass : failCount === 0)
  const vColor = overallPass ? '#4caf50' : '#f44336'

  return (
    <div>
      {/* 종합 판정 배너 */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: 10, padding: '10px 12px', borderRadius: 5, marginBottom: 10,
        background: vColor + '18', border: `1px solid ${vColor}55`,
      }}>
        <span style={{ fontSize: 20, fontWeight: 700, color: vColor }}>{overallPass ? '✓' : '✕'}</span>
        <span style={{ fontSize: 16, fontWeight: 700, color: vColor }}>{overallPass ? 'PASS' : 'FAIL'}</span>
        <span style={{ marginLeft: 'auto', color: '#999', fontSize: 11 }}>
          measurement {failCount} / {totalMeas} out of tolerance
        </span>
      </div>

      {/* 프로파일 차트 */}
      <div className="rv-sec-title" style={{ color: '#999', fontSize: 11, textTransform: 'uppercase', letterSpacing: '.8px', margin: '0 0 6px' }}>Profile</div>
      <ProfileChart x={x} z={z} mode={mode}
        features={features} lineFits={lineFits} annotations={annotations} />

      {/* 측정 목록 — 기본은 실측값만, 확장 시 부수값 표시 */}
      <div className="rv-sec-title" style={{ color: '#999', fontSize: 11, textTransform: 'uppercase', letterSpacing: '.8px', margin: '12px 0 6px' }}>Measurements</div>
      <div>
        {measDefs.map((md, i) => {
          const dec = dmap[`meas[${i}]`]
          const val = measValue(i)
          const isDeg = (md.metric === 'angle' || md.metric === 'tilt') || measUnit(i) === 'deg'
          const uStr = isDeg ? '°' : 'mm'
          const pass = measDecPass(i)
          const c = pass === undefined ? '#bbb' : pass ? '#4caf50' : '#f44336'
          const nominal = dec?.nominal ?? md.nominalMm ?? md.nominal
          const plus = md.plusMm ?? md.plus
          const minus = md.minusMm ?? md.minus
          const tol = dec?.tolerance
          const measured = dec?.measured ?? val
          const dev = (measured !== undefined && nominal !== undefined) ? measured - nominal : undefined
          const targetStr = nominal !== undefined
            ? (plus !== undefined || minus !== undefined
                ? `${nominal.toFixed(2)} +${(plus ?? 0).toFixed(2)}/-${(minus ?? 0).toFixed(2)}`
                : tol !== undefined ? `${nominal.toFixed(2)} ±${tol.toFixed(2)}` : nominal.toFixed(2))
            : undefined
          // 라인 관련 rmse (refA/refB 중 라인 element)
          const lineRmse = ((): number | undefined => {
            for (const ref of [md.refA, md.refB]) {
              const r = ref !== undefined ? resolved[ref] : null
              if (r && r.kind === 'line') return r.rmse
            }
            return undefined
          })()
          const open = !!expandedMeas[i]
          return (
            <div key={i} style={{ borderBottom: '1px solid #2a2a2a' }}>
              {/* 기본 행: M{i} + combo·metric + 실측값 + PASS/FAIL */}
              <div onClick={() => toggleMeasRow(i)} style={{
                display: 'flex', alignItems: 'center', gap: 8, padding: '5px 6px', cursor: 'pointer',
              }}>
                <span style={{ color: '#555', fontSize: 10, display: 'inline-block', width: 10, transform: open ? 'rotate(90deg)' : 'none' }}>▶</span>
                <span style={{ color: '#ddd', fontWeight: 600, minWidth: 26 }}>M{i}</span>
                <small style={{ color: '#666', fontWeight: 400 }}>{md.combo ?? ''}{md.metric ? '·' + md.metric : ''}</small>
                <span style={{ marginLeft: 'auto', fontFamily: 'monospace', fontSize: 12, fontWeight: 700, color: c }}>
                  {val !== undefined ? `${val.toFixed(2)}${uStr}` : '—'}
                </span>
                {pass !== undefined && (
                  <span style={{
                    fontSize: 10, padding: '1px 6px', borderRadius: 3, fontWeight: 600,
                    background: (pass ? '#4caf50' : '#f44336') + '22', color: pass ? '#4caf50' : '#f44336',
                    border: `1px solid ${pass ? '#4caf50' : '#f44336'}66`,
                  }}>{pass ? 'PASS' : 'FAIL'}</span>
                )}
              </div>
              {/* 부수값 (확장 시): target, deviation, refs, rmse */}
              {open && (
                <div style={{ padding: '2px 6px 8px 26px', display: 'flex', flexDirection: 'column', gap: 3, fontSize: 10, color: '#999' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                    <span>target</span>
                    <span style={{ fontFamily: 'monospace', color: '#bbb' }}>{targetStr !== undefined ? `${targetStr}${uStr}` : '—'}</span>
                  </div>
                  <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                    <span>deviation</span>
                    <span style={{ fontFamily: 'monospace', color: c }}>{dev !== undefined ? `${dev >= 0 ? '+' : ''}${dev.toFixed(2)}${uStr}` : '—'}</span>
                  </div>
                  <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                    <span>refs</span>
                    <span style={{ fontFamily: 'monospace', color: '#bbb' }}>R{md.refA ?? 0}→R{md.refB ?? 0}</span>
                  </div>
                  {lineRmse !== undefined && (
                    <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                      <span>rmse</span>
                      <span style={{ fontFamily: 'monospace', color: '#bbb' }}>{lineRmse.toFixed(3)}</span>
                    </div>
                  )}
                </div>
              )}
            </div>
          )
        })}
        {measDefs.length === 0 && <div className="param-empty" style={{ fontSize: 11 }}>측정 정의 없음</div>}
      </div>

      {/* element 원시값 (접힘) */}
      <div onClick={onToggleRaw} style={{
        display: 'flex', alignItems: 'center', gap: 6, padding: 6, cursor: 'pointer',
        color: '#888', fontSize: 11, background: '#1a1a1a', borderRadius: 4, marginTop: 8,
      }}>
        <span style={{ color: '#555', fontSize: 10, display: 'inline-block', transform: rawOpen ? 'rotate(90deg)' : 'none' }}>▶</span>
        <span>Element raw values ({elements.length})</span>
      </div>
      {rawOpen && (
        <div style={{ paddingTop: 6 }}>
          {elements.map((el, i) => {
            const color = CALIPER_COLOR
            const r = resolved[i]
            let vals: React.ReactNode
            if (el.type === 'point') {
              vals = r && r.kind === 'point'
                ? <><span>s=<em style={{ fontStyle: 'normal', color: '#ddd' }}>{r.sMm.toFixed(3)}mm</em></span><span>z=<em style={{ fontStyle: 'normal', color: '#ddd' }}>{r.zMm.toFixed(3)}mm</em></span></>
                : <span style={{ color: '#666' }}>—</span>
            } else {
              vals = r && r.kind === 'line'
                ? <><span>slope=<em style={{ fontStyle: 'normal', color: '#ddd' }}>{r.slope.toFixed(4)}</em></span><span>intercept=<em style={{ fontStyle: 'normal', color: '#ddd' }}>{r.intercept.toFixed(3)}</em></span><span>rmse=<em style={{ fontStyle: 'normal', color: '#ddd' }}>{r.rmse.toFixed(3)}</em></span></>
                : <span style={{ color: '#666' }}>—</span>
            }
            return (
              <div key={i} style={{
                display: 'flex', gap: 8, padding: '4px 6px', background: '#1e1e1e', borderRadius: 3,
                marginBottom: 4, borderLeft: `3px solid ${color}`,
              }}>
                <span style={{ fontSize: 10, fontWeight: 700, minWidth: 24, color }}>R{i}</span>
                <div style={{ fontFamily: 'monospace', fontSize: 11, color: '#aaa', display: 'flex', gap: 12 }}>{vals}</div>
              </div>
            )
          })}
        </div>
      )}
    </div>
  )
}

/**
 * 결과 탭의 행별 프로파일 뷰(슬라이더 + 온디맨드 fetch + 차트)를 소유하는 격리 컴포넌트.
 * 슬라이더 조작 시 이 컴포넌트만 리렌더되어 ResultView 전체 서브트리 리렌더를 피한다.
 * 오버레이(ExtractProfile/CloudToProfiles)가 선택 행을 필요로 하는 경우에만 onRowChange로 상위에 통지한다.
 */
function ResultProfileSection({ toolType, nodeId, params, meta, metaLen, profiles, profileCount, measurements, decisions, onRowChange }: {
  toolType: string; nodeId: string; params: Record<string, unknown>
  meta?: { label: string; n: number }[]; metaLen: number
  profiles?: { label: string; n: number; x: number[]; z: (number | null)[] }[]
  profileCount?: number
  measurements?: NodeMeasurement[]; decisions?: NodeDecision[]
  onRowChange?: (row: number) => void
}) {
  const [profRow, setProfRow] = useState(0)
  const [profMode, setProfMode] = useState<'line' | 'points'>('points')
  const [profData, setProfData] = useState<{ x: number[]; z: (number | null)[]; label: string; n: number; caliper?: CaliperFetched } | null>(null)
  const [profLoading, setProfLoading] = useState(false)
  const [caliperRawOpen, setCaliperRawOpen] = useState(false)

  // profileMeta가 있으면 온디맨드 fetch — 구독 먼저 걸고 요청 (응답 유실 방지, stale 가드)
  useEffect(() => {
    if (!meta || metaLen === 0) return
    const api = window.electronAPI
    if (!api?.onEngineEvent || !api?.engineFetchProfile) return
    const clampedRow = Math.min(profRow, metaLen - 1)
    const unsub = api.onEngineEvent((raw: unknown) => {
      const d = raw as { event?: string; nodeId?: string; profileIdx?: number; x?: number[]; z?: (number | null)[]; label?: string; n?: number; error?: string; caliper?: CaliperFetched }
      if (d.event !== 'profileData' || d.nodeId !== nodeId) return
      if (typeof d.profileIdx === 'number' && d.profileIdx !== clampedRow) return  // stale 응답 무시
      setProfLoading(false)
      if (d.error) { setProfData(null); return }
      setProfData({ x: d.x ?? [], z: d.z ?? [], label: d.label ?? '', n: d.n ?? 0, caliper: d.caliper })
    })
    setProfLoading(true)
    api.engineFetchProfile(nodeId, clampedRow)
    return unsub
  }, [profRow, meta, metaLen, nodeId])

  // 선택 행 변경을 상위(오버레이)에 통지
  const setRow = (r: number) => {
    setProfRow(r)
    onRowChange?.(r)
  }

  const useMeta = meta && metaLen > 0
  const useOld = !useMeta && profiles && profiles.length > 0
  if (!useMeta && !useOld) return null

  const totalRows = useMeta ? metaLen : profiles!.length
  const idx = Math.min(profRow, totalRows - 1)
  const curMeta = useMeta ? meta![idx] : { label: profiles![idx].label, n: profiles![idx].n }
  const chartData = useMeta ? profData : profiles![idx]

  return (
    <div className="node-result-measures">
      <div className="node-result-row" style={{ fontWeight: 600, opacity: 0.8 }}>
        <span className="node-result-label">프로파일</span>
        <span className="node-result-val">{profileCount ?? totalRows}개</span>
      </div>
      <div className="param-row">
        <span className="param-label">행</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, flex: 1 }}>
          <button
            style={{ padding: '1px 6px', fontSize: 12, lineHeight: 1.4, cursor: 'pointer', flexShrink: 0 }}
            disabled={idx === 0}
            onClick={() => setRow(idx - 1)}
          >◀</button>
          <input type="range" min={0} max={totalRows - 1} step={1} value={idx}
            style={{ flex: 1 }}
            onChange={e => setRow(parseInt(e.target.value))} />
          <button
            style={{ padding: '1px 6px', fontSize: 12, lineHeight: 1.4, cursor: 'pointer', flexShrink: 0 }}
            disabled={idx === totalRows - 1}
            onClick={() => setRow(idx + 1)}
          >▶</button>
          <input
            type="number"
            min={0}
            max={totalRows - 1}
            value={idx}
            style={{ width: 52, fontSize: 12, textAlign: 'center', flexShrink: 0 }}
            onChange={e => {
              const v = parseInt(e.target.value)
              if (!isNaN(v)) setRow(Math.max(0, Math.min(totalRows - 1, v)))
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
      {chartData && toolType === 'ProfileCaliper' && (
        <ProfileCaliperResult
          x={chartData.x} z={chartData.z} mode={profMode}
          params={params} measurements={measurements} decisions={decisions}
          caliper={useMeta ? profData?.caliper : undefined}
          rawOpen={caliperRawOpen} onToggleRaw={() => setCaliperRawOpen(o => !o)} />
      )}
      {chartData && toolType !== 'ProfileCaliper' && (
        <ProfileChart x={chartData.x} z={chartData.z} mode={profMode} />
      )}
    </div>
  )
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
  // 프로파일 슬라이더 상태는 ResultProfileSection이 소유한다. 여기서는 오버레이용으로 선택 행만 동기화 유지.
  const [overlayProfRow, setOverlayProfRow] = useState(0)

  const meta = result?.profileMeta
  const metaLen = meta?.length ?? 0

  const extractProfLabel = (() => {
    const useMeta = meta && metaLen > 0
    if (useMeta) {
      const idx = Math.min(overlayProfRow, metaLen - 1)
      return meta![idx]?.label ?? ''
    }
    if (result?.profiles && result.profiles.length > 0) {
      const idx = Math.min(overlayProfRow, result.profiles.length - 1)
      return result.profiles[idx]?.label ?? ''
    }
    return ''
  })()

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

  // ExtractProfile / CloudToProfiles: 선택된 프로파일 위치 오버레이
  const extractProfileOverlay = (toolType === 'ExtractProfile' || toolType === 'CloudToProfiles') && result?.imgW && result?.imgH
    ? (() => {
        const rowMatch = extractProfLabel.match(/^row:(\d+)$/)
        const colMatch = extractProfLabel.match(/^col:(\d+)$/)
        if (rowMatch) {
          const row = parseInt(rowMatch[1])
          return (
            <svg viewBox={`0 0 ${result.imgW} ${result.imgH}`} preserveAspectRatio="none"
              style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
              <line x1={0} y1={row} x2={result.imgW} y2={row}
                stroke="#00e5ff" strokeWidth={2} vectorEffect="non-scaling-stroke" />
            </svg>
          )
        }
        if (colMatch) {
          const col = parseInt(colMatch[1])
          return (
            <svg viewBox={`0 0 ${result.imgW} ${result.imgH}`} preserveAspectRatio="none"
              style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
              <line x1={col} y1={0} x2={col} y2={result.imgH}
                stroke="#00e5ff" strokeWidth={2} vectorEffect="non-scaling-stroke" />
            </svg>
          )
        }
        return undefined
      })()
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
              canvasHeight={(toolType === 'ExtractProfile' || toolType === 'CloudToProfiles') && (meta || result.profiles) ? 200 : 360}
              rois={toolType === 'HeightMeasure' ? measureRois : undefined}
              roiTypeLabel={() => 'ROI'}
              overlay={lineOverlay ?? alignOverlay ?? measureOverlay ?? lineFitOverlay ?? extractProfileOverlay}
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

      {/* 행별 Profile 형상 차트 (CloudToProfiles, ExtractProfile) — NotchMeasureV2 제외.
          슬라이더 상태를 별도 컴포넌트로 격리하여 조작 시 ResultView 전체 리렌더를 방지한다. */}
      {toolType !== 'NotchMeasureV2' && (
        <ResultProfileSection
          toolType={toolType}
          nodeId={nodeId}
          params={params}
          meta={meta}
          metaLen={metaLen}
          profiles={result.profiles}
          profileCount={result.profileCount}
          measurements={result.measurements}
          decisions={result.decisions}
          onRowChange={(toolType === 'ExtractProfile' || toolType === 'CloudToProfiles')
            ? setOverlayProfRow : undefined}
        />
      )}

      {/* 범용 측정값 테이블 — 커스텀 렌더 없는 툴(RegionMeasure, LineFit 등).
          ProfileCaliper는 자체 결과뷰(ProfileCaliperResult)가 처리 + per-profile(prof[j].*) 원시값은 숨김 */}
      {result.measurements && result.measurements.length > 0
        && !['PlaneFit', 'Align', 'HeightMeasure', 'ProfileCaliper'].includes(toolType) && (
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

export default function NodePanel({ nodeId, toolType, label, params, result, upstreamPreview, upstreamZMin, upstreamZMax, upstreamResX, upstreamResY, upstreamOriginCol, upstreamOriginRow, upstreamCloud, upstreamProfileNodeId, upstreamProfileMeta, width, onWidthChange, onParamChange, onRun, pinned, onTogglePin, onClose }: Props) {
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
          ) : toolType === 'ProfileCaliper' ? (
            <ProfileCaliperEditor
              params={params}
              onChange={(next) => onParamChange(nodeId, next)}
              resultMeasurements={result?.measurements}
              resultDecisions={result?.decisions}
              resultProfiles={result?.profiles}
              nodeId={nodeId}
              profileMeta={result?.profileMeta}
              upstreamNodeId={upstreamProfileNodeId}
              upstreamProfileMeta={upstreamProfileMeta}
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
