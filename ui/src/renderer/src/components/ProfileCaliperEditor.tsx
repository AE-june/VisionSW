import { useEffect, useRef, useState, useCallback } from 'react'
import { NumField, Tip } from './ParamPanel'
import ProfileChart, { type CaliperFeature, type CaliperLineFit, type ProfileRange, type ProfileAnnotation } from './ProfileChart'

// ── 통합 데이터 모델 (C++ 측과 일치) ────────────────────────────────────────
interface ElementDef {
  fromMm: number; toMm: number       // s(폭) 범위
  zFromMm?: number; zToMm?: number   // z(높이) 범위 (미지정/0,0 = 전체 높이)
  type: 'point' | 'line'
  kind: string   // edge|ridge|valley|corner|maxZ|minZ|mean (point 전용)
  dir: string    // rising|falling|any (edge 전용)
  threshold: number; smoothWindow: number; nth: number
}
interface MeasurementDef {
  combo: 'pp' | 'pl' | 'll' | 'l' | 'p'
  metric: string  // pp:euclidean/deltaS/deltaZ pl:perpDist ll:angle/offset l:tilt/flatness p:absS/absZ
  refA: number; refB: number
  nominalMm: number; plusMm: number; minusMm: number
}

interface NodeMeasurement { name: string; value: number; unit: string; valid: boolean }
interface NodeDecision { name: string; pass: boolean }
interface ProfileData { label: string; n: number; x: number[]; z: (number | null)[] }

// 엔진 fetchProfile 응답에 실려오는 per-profile caliper 분석 (온디맨드 오버레이)
interface CaliperElemView {
  type: string; valid: boolean
  sMm: number; zMm: number
  slope: number; intercept: number; rmse: number
  fromMm: number; toMm: number
}
interface CaliperMeasView { value: number; unit: string; hasDecision: boolean; pass: boolean }
interface CaliperFetched { elems: CaliperElemView[]; meas: CaliperMeasView[] }
interface FetchedProfile extends ProfileData { caliper?: CaliperFetched }

interface Props {
  params: Record<string, unknown>
  onChange: (next: Record<string, unknown>) => void
  resultMeasurements?: NodeMeasurement[]
  resultDecisions?: NodeDecision[]
  resultProfiles?: ProfileData[]
  // 엔진이 프로파일을 profileMeta(온디맨드)로만 보내므로, 직접 x/z를 가져오기 위한 정보
  nodeId?: string
  profileMeta?: { label: string; n: number }[]
  // caliper 미실행 상태에서도 상류(ExtractProfile) 프로파일을 바로 표시하기 위한 fallback
  upstreamNodeId?: string
  upstreamProfileMeta?: { label: string; n: number }[]
}

// ROI element은 색상 구분 없이 단일 중립색 (ROI 많아도 안 정신없게). 구분은 R0/R1 라벨로.
const ELEM_COLOR = '#9aa7b4'

// combo → 허용 metric 목록
const COMBO_METRICS: Record<MeasurementDef['combo'], string[]> = {
  pp: ['euclidean', 'deltaS', 'deltaZ'],
  pl: ['perpDist', 'zDist'],
  ll: ['angle', 'offset', 'intersectS', 'intersectZ'],
  l: ['tilt', 'flatness'],
  p: ['absS', 'absZ'],
}

// combo → 슬롯 정의 (label, 요구 타입, 어느 ref에 바인딩)
type SlotType = 'point' | 'line'
interface SlotDef { label: string; type: SlotType; ref: 'refA' | 'refB' }
const COMBO_SLOTS: Record<MeasurementDef['combo'], SlotDef[]> = {
  pp: [{ label: 'point A', type: 'point', ref: 'refA' }, { label: 'point B', type: 'point', ref: 'refB' }],
  pl: [{ label: 'point', type: 'point', ref: 'refA' }, { label: 'line', type: 'line', ref: 'refB' }],
  ll: [{ label: 'line A', type: 'line', ref: 'refA' }, { label: 'line B', type: 'line', ref: 'refB' }],
  l: [{ label: 'line', type: 'line', ref: 'refA' }],
  p: [{ label: 'point', type: 'point', ref: 'refA' }],
}

const COMBO_LABELS: Record<MeasurementDef['combo'], string> = {
  pp: 'point · point',
  pl: 'point · line',
  ll: 'line · line',
  l: 'line',
  p: 'point',
}

function defaultMeasurement(): MeasurementDef {
  return { combo: 'pp', metric: 'euclidean', refA: 0, refB: 0, nominalMm: 0, plusMm: 0, minusMm: 0 }
}

/** 라인 z=ms+b 위로 점(s0,z0)의 수선의 발 */
function perpFoot(s0: number, z0: number, m: number, b: number): { s: number; z: number } {
  const sf = (s0 + m * (z0 - b)) / (m * m + 1)
  return { s: sf, z: m * sf + b }
}

// "+ 추가" 선택 메뉴: 추가할 element의 종류를 먼저 고르게 한다.
type ElemPreset = Pick<ElementDef, 'type' | 'kind' | 'dir' | 'threshold' | 'smoothWindow' | 'nth'>
const ELEM_ADD_OPTIONS: { label: string; preset: ElemPreset }[] = [
  { label: 'Point · Edge',           preset: { type: 'point', kind: 'edge',   dir: 'rising', threshold: 0.05, smoothWindow: 3, nth: 0 } },
  { label: 'Point · Ridge',          preset: { type: 'point', kind: 'ridge',  dir: 'any',    threshold: 0.05, smoothWindow: 3, nth: 0 } },
  { label: 'Point · Valley',         preset: { type: 'point', kind: 'valley', dir: 'any',    threshold: 0.05, smoothWindow: 3, nth: 0 } },
  { label: 'Point · Corner',         preset: { type: 'point', kind: 'corner', dir: 'any',    threshold: 0.05, smoothWindow: 3, nth: 0 } },
  { label: 'Point · Top (최고점)',    preset: { type: 'point', kind: 'maxZ',   dir: 'any',    threshold: 0.05, smoothWindow: 3, nth: 0 } },
  { label: 'Point · Bottom (최저점)', preset: { type: 'point', kind: 'minZ',   dir: 'any',    threshold: 0.05, smoothWindow: 3, nth: 0 } },
  { label: 'Point · Mean (구간평균)', preset: { type: 'point', kind: 'mean',   dir: 'any',    threshold: 0.05, smoothWindow: 3, nth: 0 } },
  { label: 'Line fit (직선)',         preset: { type: 'line',  kind: 'edge',   dir: 'any',    threshold: 0.05, smoothWindow: 3, nth: 0 } },
]

function elemDesc(e: ElementDef): string {
  const rng = `${e.fromMm.toFixed(1)}~${e.toMm.toFixed(1)}mm`
  if (e.type === 'line') return `Line fit ${rng}`
  const cap = e.kind.charAt(0).toUpperCase() + e.kind.slice(1)
  if (e.kind === 'edge') return `${cap} (${e.dir}) ${rng}`
  return `${cap} ${rng}`
}

// 첫 번째로 slot 타입에 맞는 element index (없으면 0)
function firstOfType(elements: ElementDef[], type: SlotType): number {
  const i = elements.findIndex(e => e.type === type)
  return i >= 0 ? i : 0
}

function SelectRow({ label, value, onChange, tooltip, children }: {
  label: string; value: string; onChange: (v: string) => void
  tooltip?: string; children: React.ReactNode
}) {
  return (
    <div className="param-row">
      <span className="param-label">{label}<Tip text={tooltip} /></span>
      <select className="param-select" value={value} onChange={e => onChange(e.target.value)}>
        {children}
      </select>
    </div>
  )
}

export default function ProfileCaliperEditor({
  params, onChange,
  resultMeasurements, resultDecisions, resultProfiles,
  nodeId, profileMeta,
  upstreamNodeId, upstreamProfileMeta,
}: Props) {
  const elements     = (params.elements     as ElementDef[])     ?? []
  const measurements = (params.measurements as MeasurementDef[]) ?? []

  const [activeElemIdx, setActiveElemIdx] = useState<number | null>(elements.length > 0 ? 0 : null)
  const [activeMeasIdx, setActiveMeasIdx] = useState<number | null>(null)
  const [rawExpanded, setRawExpanded] = useState(false)
  const [addMenuOpen, setAddMenuOpen] = useState(false)
  const addWrapRef = useRef<HTMLDivElement>(null)

  // 분석/표시할 프로파일(타일) 인덱스 — 로컬 state (params 미변경 → 슬라이더가 그래프 리렌더/재검사 유발 안 함)
  const [profSel, setProfSel] = useState<number>((params.profileIndex as number) ?? 0)
  const profileIndex = profSel
  // 프로파일 소스: caliper 자체 결과 우선, 없으면 상류(ExtractProfile) 사용 (미실행 상태 표시)
  const srcNodeId = (profileMeta?.length ? nodeId : upstreamNodeId) ?? nodeId
  const srcMeta   = profileMeta?.length ? profileMeta : upstreamProfileMeta
  const metaLen = srcMeta?.length ?? 0
  const clampedIdx = Math.max(0, Math.min(profileIndex, Math.max(0, metaLen - 1)))

  // 온디맨드 프로파일 데이터 (엔진 profileMeta → engineFetchProfile로 x/z + caliper 확보)
  const [fetchedProfile, setFetchedProfile] = useState<FetchedProfile | null>(null)
  useEffect(() => {
    if (!srcNodeId || metaLen === 0) { setFetchedProfile(null); return }
    const api = window.electronAPI as unknown as {
      onEngineEvent?: (cb: (d: unknown) => void) => (() => void)
      engineFetchProfile?: (nodeId: string, idx: number) => void
    }
    if (!api?.onEngineEvent || !api?.engineFetchProfile) return
    const unsub = api.onEngineEvent((raw: unknown) => {
      const d = raw as { event?: string; nodeId?: string; profileIdx?: number; x?: number[]; z?: (number | null)[]; label?: string; n?: number; error?: string; caliper?: CaliperFetched }
      if (d.event !== 'profileData' || d.nodeId !== srcNodeId) return
      if (typeof d.profileIdx === 'number' && d.profileIdx !== clampedIdx) return
      if (d.error) { setFetchedProfile(null); return }
      setFetchedProfile({ x: d.x ?? [], z: d.z ?? [], label: d.label ?? '', n: d.n ?? 0, caliper: d.caliper })
    })
    api.engineFetchProfile(srcNodeId, clampedIdx)
    return unsub
  }, [srcNodeId, metaLen, clampedIdx])

  // 메뉴 바깥 클릭 시 닫기
  useEffect(() => {
    if (!addMenuOpen) return
    const onDoc = (ev: MouseEvent) => {
      if (addWrapRef.current && !addWrapRef.current.contains(ev.target as Node)) setAddMenuOpen(false)
    }
    document.addEventListener('mousedown', onDoc)
    return () => document.removeEventListener('mousedown', onDoc)
  }, [addMenuOpen])

  const setElements = (a: ElementDef[])     => onChange({ ...params, elements: a })
  const setMeas     = (a: MeasurementDef[]) => onChange({ ...params, measurements: a })

  const setElem = (i: number, e: ElementDef) => { const a = [...elements]; a[i] = e; setElements(a) }
  const delElem = (i: number) => {
    setElements(elements.filter((_, j) => j !== i))
    if (activeElemIdx === i) setActiveElemIdx(null)
    else if (activeElemIdx !== null && activeElemIdx > i) setActiveElemIdx(activeElemIdx - 1)
  }
  // 선택한 preset으로 새 element 추가 → 펼치고 메뉴 닫기
  const addElement = (preset: ElemPreset) => {
    // 프로파일 범위가 있으면 가운데 1/3, 없으면 0~5mm
    let fromMm = 0, toMm = 5
    const xs = resultProfiles?.[0]?.x
    if (xs && xs.length > 1) {
      const lo = xs[0], hi = xs[xs.length - 1]
      if (hi > lo) { const span = hi - lo; fromMm = lo + span / 3; toMm = lo + (2 * span) / 3 }
    }
    const newEl: ElementDef = { fromMm, toMm, ...preset }
    const idx = elements.length
    setElements([...elements, newEl])
    setActiveElemIdx(idx)
    setAddMenuOpen(false)
  }

  const setMeasAt = (i: number, m: MeasurementDef) => { const a = [...measurements]; a[i] = m; setMeas(a) }
  const delMeas = (i: number) => {
    setMeas(measurements.filter((_, j) => j !== i))
    if (activeMeasIdx === i) setActiveMeasIdx(null)
    else if (activeMeasIdx !== null && activeMeasIdx > i) setActiveMeasIdx(activeMeasIdx - 1)
  }

  const color = (_i: number) => ELEM_COLOR

  // ── 결과 파싱 ───────────────────────────────────────────────────────────
  //  우선순위: fetchProfile로 받은 이 프로파일의 caliper 데이터(fc) → 오버레이/측정값을
  //  해당 프로파일 기준으로 그린다. 없으면(미실행/구버전) resultMeasurements의 plain 이름 폴백.
  const fc = fetchedProfile?.caliper

  // plain-name 폴백 맵 (fc 없을 때만 사용)
  const mmap: Record<string, number> = {}
  if (resultMeasurements) for (const m of resultMeasurements) if (m.valid) mmap[m.name] = m.value
  const decMap: Record<string, boolean> = {}
  if (resultDecisions) for (const d of resultDecisions) decMap[d.name] = d.pass

  // element 좌표 해석 — fc 우선, 폴백은 mmap
  type Pt = { kind: 'point'; sMm: number; zMm: number }
  type Ln = { kind: 'line'; slope: number; intercept: number; fromMm: number; toMm: number }
  const resolveElem = (i: number): Pt | Ln | null => {
    const el = elements[i]
    if (!el) return null
    if (fc) {
      const ev = fc.elems[i]
      if (!ev || !ev.valid) return null
      if (ev.type === 'line') return { kind: 'line', slope: ev.slope, intercept: ev.intercept, fromMm: ev.fromMm, toMm: ev.toMm }
      return { kind: 'point', sMm: ev.sMm, zMm: ev.zMm }
    }
    if (el.type === 'point') {
      const sMm = mmap[`elem[${i}].sMm`]
      if (sMm === undefined) return null
      return { kind: 'point', sMm, zMm: mmap[`elem[${i}].zMm`] ?? 0 }
    }
    if (el.type === 'line') {
      const slope = mmap[`elem[${i}].slope`]
      const intercept = mmap[`elem[${i}].intercept`]
      if (slope === undefined || intercept === undefined) return null
      return { kind: 'line', slope, intercept, fromMm: mmap[`elem[${i}].fromMm`] ?? el.fromMm, toMm: mmap[`elem[${i}].toMm`] ?? el.toMm }
    }
    return null
  }

  // measurement 값/판정 — fc 우선, 폴백은 mmap/decMap
  const measVal = (i: number): number | undefined => {
    const md = measurements[i]
    if (fc && md?.combo === 'pl') {
      const a = resolveElem(md.refA ?? 0)
      const b = resolveElem(md.refB ?? 0)
      const pt = a?.kind === 'point' ? a : b?.kind === 'point' ? b : null
      const ln = a?.kind === 'line' ? a : b?.kind === 'line' ? b : null
      if (pt && ln) {
        if (md.metric === 'zDist') return Math.abs(pt.zMm - (ln.slope * pt.sMm + ln.intercept))
        return Math.abs(ln.slope * pt.sMm - pt.zMm + ln.intercept) / Math.sqrt(ln.slope * ln.slope + 1)
      }
    }
    return fc ? fc.meas[i]?.value : mmap[`meas[${i}]`]
  }
  const measPass = (i: number): boolean | undefined =>
    fc ? (fc.meas[i]?.hasDecision ? fc.meas[i].pass : undefined) : decMap[`meas[${i}]`]

  const hasResult = fc !== undefined || resultMeasurements !== undefined

  // 차트 피처 (type==='point' element)
  const caliperFeatures: CaliperFeature[] | undefined = (() => {
    if (!hasResult) return undefined
    const feats: CaliperFeature[] = []
    elements.forEach((_e, i) => {
      const r = resolveElem(i)
      if (r?.kind === 'point') feats.push({ sMm: r.sMm, zMm: r.zMm, kind: 'point', label: `R${i}` })
    })
    return feats.length > 0 ? feats : undefined
  })()

  // 차트 라인피팅 (type==='line' element)
  const caliperLineFits: CaliperLineFit[] | undefined = (() => {
    if (!hasResult) return undefined
    const fits: CaliperLineFit[] = []
    elements.forEach((_e, i) => {
      const r = resolveElem(i)
      if (r?.kind === 'line') fits.push({ fromMm: r.fromMm, toMm: r.toMm, slope: r.slope, intercept: r.intercept })
    })
    return fits.length > 0 ? fits : undefined
  })()

  // 인덱스 정렬 유지(editRangeIndex ↔ activeElemIdx) 위해 필터 안 함
  const chartRanges: ProfileRange[] = elements
    .map((e, i) => ({ fromMm: e.fromMm, toMm: e.toMm, zFrom: e.zFromMm, zTo: e.zToMm, color: color(i), label: `R${i}` }))

  // 측정 주석 (pp→segment, pl→perp) — fetched caliper 좌표 우선
  const caliperAnnotations: ProfileAnnotation[] | undefined = (() => {
    if (!hasResult) return undefined
    const annos: ProfileAnnotation[] = []
    measurements.forEach((md, i) => {
      const a = resolveElem(md.refA)
      const b = resolveElem(md.refB)
      const val = measVal(i)
      const isDeg = md.metric === 'angle' || md.metric === 'tilt'
      const uStr = isDeg ? '°' : 'mm'
      const label = `M${i}` + (val !== undefined ? ` ${val.toFixed(2)}${uStr}` : '')
      const pass = measPass(i)
      const col = pass === undefined ? '#bbb' : pass ? '#4caf50' : '#f44336'
      if (md.combo === 'pp') {
        if (a?.kind === 'point' && b?.kind === 'point')
          annos.push({ kind: 'segment', s1: a.sMm, z1: a.zMm, s2: b.sMm, z2: b.zMm, label, color: col })
      } else if (md.combo === 'pl') {
        const pt = a?.kind === 'point' ? a : b?.kind === 'point' ? b : null
        const ln = a?.kind === 'line' ? a : b?.kind === 'line' ? b : null
        if (pt && ln) {
          if (md.metric === 'zDist') {
            // z축 거리: 같은 s에서 라인까지 세로선
            const zOnLine = ln.slope * pt.sMm + ln.intercept
            annos.push({ kind: 'perp', s1: pt.sMm, z1: pt.zMm, s2: pt.sMm, z2: zOnLine, label, color: col })
          } else {
            const foot = perpFoot(pt.sMm, pt.zMm, ln.slope, ln.intercept)
            annos.push({ kind: 'perp', s1: pt.sMm, z1: pt.zMm, s2: foot.s, z2: foot.z, label, color: col, lineSlope: ln.slope, lineIntercept: ln.intercept })
          }
        }
      }
    })
    return annos.length > 0 ? annos : undefined
  })()

  // 드래그 → active element 2D ROI(s+z 범위) 갱신
  const handleRoiDrag = useCallback((fromMm: number, toMm: number, zFrom: number, zTo: number) => {
    if (activeElemIdx === null) return
    const e = elements[activeElemIdx]
    if (!e) return
    setElem(activeElemIdx, { ...e, fromMm, toMm, zFromMm: zFrom, zToMm: zTo })
  }, [activeElemIdx, elements])  // eslint-disable-line react-hooks/exhaustive-deps

  const profileData = resultProfiles?.[0] ?? fetchedProfile ?? undefined

  // ── element accordion 토글 (한 번에 하나 + activeElemIdx) ────────────────
  const toggleElem = (i: number) => setActiveElemIdx(prev => (prev === i ? null : i))
  const toggleMeas = (i: number) => setActiveMeasIdx(prev => (prev === i ? null : i))

  // ── measurement combo 변경 시 metric/refs 리셋 ──────────────────────────
  const changeCombo = (i: number, m: MeasurementDef, combo: MeasurementDef['combo']) => {
    const metric = COMBO_METRICS[combo][0]
    const slots = COMBO_SLOTS[combo]
    const next: MeasurementDef = { ...m, combo, metric }
    // 슬롯별 호환 element로 ref 리셋
    next.refA = 0; next.refB = 0
    for (const s of slots) {
      const idx = firstOfType(elements, s.type)
      if (s.ref === 'refA') next.refA = idx
      else next.refB = idx
    }
    setMeasAt(i, next)
  }

  return (
    <div className="caliper-editor">
      {/* ① PROFILE (고정) ══════════════════════════════════════════════ */}
      <div className="cal-sec-header">
        <span>① Profile</span>
        <span className="cal-hint">
          {activeElemIdx !== null ? `드래그 → R${activeElemIdx} 2D ROI(폭·높이) 설정` : 'element 선택 후 드래그'}
        </span>
      </div>
      <div style={{ padding: 8 }}>
        {/* 프로파일(타일) 선택 — ExtractProfile이 여러 타일 출력 시 */}
        {metaLen > 1 && (
          <div className="param-row" style={{ marginBottom: 6 }}>
            <span className="param-label">프로파일</span>
            <div style={{ display: 'flex', alignItems: 'center', gap: 4, flex: 1 }}>
              <button className="param-btn" disabled={clampedIdx === 0}
                onClick={() => setProfSel(clampedIdx - 1)}>◀</button>
              <input type="range" min={0} max={metaLen - 1} step={1} value={clampedIdx}
                style={{ flex: 1 }}
                onChange={e => setProfSel(parseInt(e.target.value))} />
              <button className="param-btn" disabled={clampedIdx === metaLen - 1}
                onClick={() => setProfSel(clampedIdx + 1)}>▶</button>
              <input type="number" min={0} max={metaLen - 1} value={clampedIdx}
                style={{ width: 56, textAlign: 'center' }}
                onChange={e => { const v = parseInt(e.target.value); if (!isNaN(v)) setProfSel(Math.max(0, Math.min(metaLen - 1, v))) }} />
              <span className="node-result-val" style={{ whiteSpace: 'nowrap', minWidth: 84, textAlign: 'right' }}>
                {srcMeta?.[clampedIdx]?.label ?? ''} ({metaLen})
              </span>
            </div>
          </div>
        )}
        {profileData ? (
          <ProfileChart
            x={profileData.x} z={profileData.z}
            mode="line" height={200}
            features={caliperFeatures}
            lineFits={caliperLineFits}
            ranges={chartRanges}
            annotations={caliperAnnotations}
            editRangeIndex={activeElemIdx ?? undefined}
            onRoiDrag={activeElemIdx !== null ? handleRoiDrag : undefined}
          />
        ) : (
          <div className="param-empty">실행 후 프로파일이 표시됩니다</div>
        )}
      </div>

      {/* ② ELEMENTS (스크롤, 아코디언) ═════════════════════════════════ */}
      <div className="cal-sec-header">
        <span>② Elements</span>
        <div className="cal-addwrap" ref={addWrapRef}>
          <button className="param-btn cal-add-btn"
            onClick={() => setAddMenuOpen(v => !v)}>+ 추가 ▾</button>
          {addMenuOpen && (
            <div className="cal-addmenu">
              {ELEM_ADD_OPTIONS.map(opt => (
                <button key={opt.label} className="cal-addmenu-item"
                  onClick={() => addElement(opt.preset)}>{opt.label}</button>
              ))}
            </div>
          )}
        </div>
      </div>
      <div className="cal-sec-body cal-el-body">
        {elements.map((e, i) => {
          const active = activeElemIdx === i
          return (
            <div key={i} className={`cal-el-row${active ? ' active' : ''}`}
              style={active ? { borderColor: color(i) } : undefined}>
              <div className="cal-el-header" onClick={() => toggleElem(i)}>
                <span className="cal-chevron">▶</span>
                <span className="cal-badge" style={{ background: color(i), color: '#fff' }}>R{i}</span>
                <span className="cal-el-desc">{elemDesc(e)}</span>
                <span className={`cal-tag cal-tag-${e.type}`}>{e.type}</span>
                <button className="cal-del" onClick={ev => { ev.stopPropagation(); delElem(i) }}>✕</button>
              </div>
              {active && (
                <div className="cal-el-props">
                  <NumField label="s from(mm)" value={e.fromMm} step={0.1}
                    tooltip="s(폭) ROI 시작(mm). 차트 좌드래그로도 설정"
                    onChange={v => setElem(i, { ...e, fromMm: v })} />
                  <NumField label="s to(mm)" value={e.toMm} step={0.1}
                    tooltip="s(폭) ROI 끝(mm)"
                    onChange={v => setElem(i, { ...e, toMm: v })} />
                  <NumField label="z from(mm)" value={e.zFromMm ?? 0} step={0.1}
                    tooltip="z(높이) ROI 하한(mm). z to와 같거나 크면 높이 제한 없음. 차트 좌드래그로도 설정"
                    onChange={v => setElem(i, { ...e, zFromMm: v })} />
                  <NumField label="z to(mm)" value={e.zToMm ?? 0} step={0.1}
                    tooltip="z(높이) ROI 상한(mm). z from보다 커야 높이 제한 적용"
                    onChange={v => setElem(i, { ...e, zToMm: v })} />
                  <SelectRow label="type" value={e.type}
                    onChange={v => setElem(i, { ...e, type: v as ElementDef['type'] })}
                    tooltip="point=특징점 검출, line=구간 직선 피팅">
                    <option value="point">point</option>
                    <option value="line">line</option>
                  </SelectRow>

                  {e.type === 'point' ? (
                    <>
                      <SelectRow label="kind" value={e.kind}
                        onChange={v => setElem(i, { ...e, kind: v })}
                        tooltip="검출 유형. edge=엣지, ridge=능선, valley=골, corner=코너, maxZ/minZ=극값, mean=평균">
                        <option value="edge">edge</option>
                        <option value="ridge">ridge</option>
                        <option value="valley">valley</option>
                        <option value="corner">corner</option>
                        <option value="maxZ">maxZ</option>
                        <option value="minZ">minZ</option>
                        <option value="mean">mean</option>
                      </SelectRow>
                      {e.kind === 'edge' && (
                        <SelectRow label="direction" value={e.dir}
                          onChange={v => setElem(i, { ...e, dir: v })}
                          tooltip="엣지 방향 필터">
                          <option value="rising">rising ↑</option>
                          <option value="falling">falling ↓</option>
                          <option value="any">any</option>
                        </SelectRow>
                      )}
                      <NumField label="nth" value={e.nth ?? 0} step={1}
                        tooltip="N번째 검출 결과(0=첫 번째)"
                        onChange={v => setElem(i, { ...e, nth: v })} />
                      <NumField label="threshold(mm)" value={e.threshold ?? 0.05} step={0.005}
                        tooltip="검출 최소 Z 변화량(mm). 노이즈보다 크게"
                        onChange={v => setElem(i, { ...e, threshold: v })} />
                      <NumField label="smoothWindow" value={e.smoothWindow ?? 3} step={1}
                        tooltip="검출 전 이동평균 창 크기"
                        onChange={v => setElem(i, { ...e, smoothWindow: v })} />
                    </>
                  ) : (
                    <div className="cal-el-note">최소제곱 직선 피팅 — 파라미터 없음</div>
                  )}
                </div>
              )}
            </div>
          )
        })}
        {elements.length === 0 && <div className="param-empty">[+ 추가]로 element 추가</div>}
      </div>

      {/* ③ MEASUREMENTS (스크롤, 아코디언) ═══════════════════════════════ */}
      <div className="cal-sec-header">
        <span>③ Measurements</span>
        <button className="param-btn cal-add-btn"
          onClick={() => {
            const idx = measurements.length
            setMeas([...measurements, defaultMeasurement()])
            setActiveMeasIdx(idx)
          }}>+ 추가</button>
      </div>
      <div className="cal-sec-body cal-meas-body">
        {measurements.map((m, i) => {
          const active = activeMeasIdx === i
          const slots = COMBO_SLOTS[m.combo]
          const refsStr = slots.map(s => `R${s.ref === 'refA' ? m.refA : m.refB}`).join('→')
          return (
            <div key={i} className={`cal-meas-row${active ? ' active' : ''}`}>
              <div className="cal-meas-header" onClick={() => toggleMeas(i)}>
                <span className="cal-chevron">▶</span>
                <span className="cal-meas-badge">M{i}</span>
                <span className="cal-meas-summary">
                  {COMBO_LABELS[m.combo]}
                  <span className="cal-mm-metric">{m.metric}</span>
                  <span className="cal-mm-refs">{refsStr}</span>
                </span>
                <button className="cal-del" onClick={ev => { ev.stopPropagation(); delMeas(i) }}>✕</button>
              </div>
              {active && (
                <div className="cal-meas-props">
                  <div className="cal-meas-top">
                    <select className="param-select" style={{ flex: 1 }} value={m.combo}
                      onChange={ev => changeCombo(i, m, ev.target.value as MeasurementDef['combo'])}>
                      <option value="pp">point · point</option>
                      <option value="pl">point · line</option>
                      <option value="ll">line · line</option>
                      <option value="l">line</option>
                      <option value="p">point</option>
                    </select>
                    <select className="param-select cal-metric-select" value={m.metric}
                      onChange={ev => setMeasAt(i, { ...m, metric: ev.target.value })}>
                      {COMBO_METRICS[m.combo].map(mt => <option key={mt} value={mt}>{mt}</option>)}
                    </select>
                  </div>

                  <div className="cal-meas-targets">
                    {slots.map((s, si) => {
                      const val = s.ref === 'refA' ? m.refA : m.refB
                      const opts = elements
                        .map((el, ei) => ({ el, ei }))
                        .filter(({ el }) => el.type === s.type)
                      return (
                        <div key={si} style={{ display: 'contents' }}>
                          {si > 0 && <span className="cal-slot-sep">→</span>}
                          <div className="cal-slot">
                            <label>{s.label}</label>
                            <select className={`param-select cal-slot-${s.type}`} value={val}
                              onChange={ev => {
                                const idx = parseInt(ev.target.value)
                                setMeasAt(i, s.ref === 'refA' ? { ...m, refA: idx } : { ...m, refB: idx })
                              }}>
                              {opts.length === 0 && <option value={0}>({s.type} 없음)</option>}
                              {opts.map(({ ei }) => (
                                <option key={ei} value={ei}>R{ei} ({s.type})</option>
                              ))}
                            </select>
                          </div>
                        </div>
                      )
                    })}
                  </div>

                  <div className="cal-meas-tol">
                    <div style={{ flex: 1 }}>
                      <NumField label="nominal(mm)" value={m.nominalMm ?? 0} step={0.01}
                        tooltip="기준값. 0이면 판정 없음"
                        onChange={v => setMeasAt(i, { ...m, nominalMm: v })} />
                    </div>
                    <div style={{ flex: 1 }}>
                      <NumField label="+(mm)" value={m.plusMm ?? 0} step={0.01}
                        tooltip="상한 공차"
                        onChange={v => setMeasAt(i, { ...m, plusMm: v })} />
                    </div>
                    <div style={{ flex: 1 }}>
                      <NumField label="-(mm)" value={m.minusMm ?? 0} step={0.01}
                        tooltip="하한 공차(양수 입력)"
                        onChange={v => setMeasAt(i, { ...m, minusMm: v })} />
                    </div>
                  </div>
                </div>
              )}
            </div>
          )
        })}
        {measurements.length === 0 && <div className="param-empty">[+ 추가]로 측정 추가</div>}
      </div>

      {/* ④ RESULTS (스크롤, 숫자만) ══════════════════════════════════════ */}
      <div className="cal-sec-header">
        <span>④ Results</span>
        <span className="cal-hint">실측값</span>
      </div>
      <div className="cal-sec-body cal-res-body">
        {!hasResult ? (
          <div className="param-empty">실행 후 결과가 표시됩니다</div>
        ) : (
          <>
            {/* Measurements (주) */}
            {measurements.map((m, i) => {
              const val = measVal(i)
              const pass = measPass(i)
              const unit = m.metric === 'angle' || m.metric === 'tilt' ? 'deg'
                : m.metric === 'flatness' ? 'mm' : 'mm'
              return (
                <div key={i} className="cal-res-row" style={{ borderLeftColor: '#888' }}>
                  <span className="cal-res-label">M{i}</span>
                  <span className="cal-res-values">
                    {val !== undefined ? `${val.toFixed(4)} ${unit}` : '—'}
                  </span>
                  {pass !== undefined && (
                    <span className={`cal-pass-badge ${pass ? 'cal-pass' : 'cal-fail'}`}>
                      {pass ? 'PASS' : 'FAIL'}
                    </span>
                  )}
                </div>
              )
            })}

            {/* Elements (보조, 접힘) */}
            <div className="cal-res-el-header" onClick={() => setRawExpanded(v => !v)}>
              <span className="cal-chevron" style={{ transform: rawExpanded ? 'rotate(90deg)' : undefined }}>▶</span>
              <span>Element raw values ({elements.length})</span>
            </div>
            {rawExpanded && elements.map((e, i) => {
              const r = resolveElem(i)
              // fc가 있으면 rmse는 fc.elems에서 직접, 없으면 mmap 폴백
              if (e.type === 'point') {
                const s = r?.kind === 'point' ? r.sMm : undefined
                const z = r?.kind === 'point' ? r.zMm : undefined
                return (
                  <div key={i} className="cal-res-row" style={{ borderLeftColor: color(i) }}>
                    <span className="cal-res-label" style={{ color: color(i) }}>R{i}</span>
                    <span className="cal-res-values">
                      s={s !== undefined ? s.toFixed(3) : '—'} z={z !== undefined ? z.toFixed(3) : '—'}
                    </span>
                  </div>
                )
              }
              const slope = r?.kind === 'line' ? r.slope : undefined
              const intercept = r?.kind === 'line' ? r.intercept : undefined
              const rmse = fc ? (fc.elems[i]?.valid ? fc.elems[i].rmse : undefined) : mmap[`elem[${i}].rmse`]
              return (
                <div key={i} className="cal-res-row" style={{ borderLeftColor: color(i) }}>
                  <span className="cal-res-label" style={{ color: color(i) }}>R{i}</span>
                  <span className="cal-res-values">
                    slope={slope !== undefined ? slope.toFixed(4) : '—'} intercept={intercept !== undefined ? intercept.toFixed(3) : '—'} rmse={rmse !== undefined ? rmse.toFixed(3) : '—'}
                  </span>
                </div>
              )
            })}
          </>
        )}
      </div>
    </div>
  )
}
