import { useState, useCallback } from 'react'
import { NumField, Tip } from './ParamPanel'
import ProfileChart, { type CaliperFeature, type CaliperLineFit, type ProfileRange } from './ProfileChart'

interface FeatureDef {
  kind: string; dir: string; threshold: number; smoothWindow: number
  searchFromMm: number; searchToMm: number; nth: number
}
interface LineFitDef { fromMm: number; toMm: number }
interface DistanceDef {
  from: number; to: number; mode: string
  nominalMm: number; plusMm: number; minusMm: number
}
interface NodeMeasurement { name: string; value: number; unit: string; valid: boolean }
interface ProfileData { label: string; n: number; x: number[]; z: (number | null)[] }

interface Props {
  params: Record<string, unknown>
  onChange: (next: Record<string, unknown>) => void
  resultMeasurements?: NodeMeasurement[]
  resultProfiles?: ProfileData[]
}

// 범위 편집 대상
type RangeTarget =
  | { kind: 'none' }
  | { kind: 'feature'; idx: number }
  | { kind: 'lineFit'; idx: number }

function defaultFeature(): FeatureDef {
  return { kind: 'edge', dir: 'any', threshold: 0.05, smoothWindow: 3, searchFromMm: 0, searchToMm: 0, nth: 0 }
}
function defaultLineFit(): LineFitDef { return { fromMm: 0, toMm: 10 } }
function defaultDistance(): DistanceDef {
  return { from: 0, to: 1, mode: 'deltaS', nominalMm: 0, plusMm: 0, minusMm: 0 }
}

const FEAT_COLORS = ['#f44336', '#4caf50', '#2196f3', '#ff9800', '#9c27b0']
const LF_COLORS   = ['#ff8f00', '#f06292', '#80cbc4', '#aed581']

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
  resultMeasurements, resultProfiles,
}: Props) {
  const features  = (params.features  as FeatureDef[])  ?? []
  const lineFits  = (params.lineFits  as LineFitDef[])  ?? []
  const distances = (params.distances as DistanceDef[]) ?? []

  const [rangeTarget, setRangeTarget] = useState<RangeTarget>({ kind: 'none' })

  const setFeats = (f: FeatureDef[])   => onChange({ ...params, features: f })
  const setFits  = (f: LineFitDef[])   => onChange({ ...params, lineFits: f })
  const setDists = (d: DistanceDef[])  => onChange({ ...params, distances: d })

  const setFeat  = (i: number, f: FeatureDef)  => { const a = [...features]; a[i] = f; setFeats(a) }
  const delFeat  = (i: number) => setFeats(features.filter((_, j) => j !== i))
  const setFit   = (i: number, f: LineFitDef)  => { const a = [...lineFits]; a[i] = f; setFits(a) }
  const delFit   = (i: number) => setFits(lineFits.filter((_, j) => j !== i))
  const setDist  = (i: number, d: DistanceDef) => { const a = [...distances]; a[i] = d; setDists(a) }
  const delDist  = (i: number) => setDists(distances.filter((_, j) => j !== i))

  // ── 프로파일 차트 — 결과 파싱 ────────────────────────────────────────────
  const caliperFeatures: CaliperFeature[] | undefined = (() => {
    if (!resultMeasurements) return undefined
    const mmap: Record<string, number> = {}
    for (const m of resultMeasurements) if (m.valid) mmap[m.name] = m.value
    const feats: CaliperFeature[] = []
    for (let fi = 0; ; fi++) {
      const sMm = mmap[`feat[${fi}].sMm`]
      if (sMm === undefined) break
      feats.push({ sMm, zMm: mmap[`feat[${fi}].zMm`] ?? 0, kind: 'feat', label: `F${fi}` })
    }
    return feats.length > 0 ? feats : undefined
  })()

  const caliperLineFits: CaliperLineFit[] | undefined = (() => {
    if (!resultMeasurements) return undefined
    const mmap: Record<string, number> = {}
    for (const m of resultMeasurements) if (m.valid) mmap[m.name] = m.value
    const fits: CaliperLineFit[] = []
    for (let li = 0; ; li++) {
      const fromMm = mmap[`lineFit[${li}].fromMm`]
      if (fromMm === undefined) break
      const slope = mmap[`lineFit[${li}].slope`]
      const intercept = mmap[`lineFit[${li}].intercept`]
      if (slope !== undefined && intercept !== undefined)
        fits.push({ fromMm, toMm: mmap[`lineFit[${li}].toMm`] ?? fromMm, slope, intercept })
    }
    return fits.length > 0 ? fits : undefined
  })()

  // 차트에 표시할 컬러 밴드 (피처 searchFrom/To, 라인피팅 from/to)
  const chartRanges: ProfileRange[] = [
    ...features.map((f, i) => ({
      fromMm: f.searchFromMm, toMm: f.searchToMm,
      color: FEAT_COLORS[i % FEAT_COLORS.length],
      label: `F${i}`,
    })).filter(r => r.toMm > r.fromMm),
    ...lineFits.map((lf, i) => ({
      fromMm: lf.fromMm, toMm: lf.toMm,
      color: LF_COLORS[i % LF_COLORS.length],
      label: `LF${i}`,
    })).filter(r => r.toMm > r.fromMm),
  ]

  // 드래그로 범위 설정
  const handleRangeDrag = useCallback((fromMm: number, toMm: number) => {
    if (rangeTarget.kind === 'feature') {
      setFeat(rangeTarget.idx, { ...features[rangeTarget.idx], searchFromMm: fromMm, searchToMm: toMm })
    } else if (rangeTarget.kind === 'lineFit') {
      setFit(rangeTarget.idx, { fromMm, toMm })
    }
  }, [rangeTarget, features, lineFits])

  const rangeTargetStr = (() => {
    if (rangeTarget.kind === 'feature') return `feat-${rangeTarget.idx}`
    if (rangeTarget.kind === 'lineFit') return `lf-${rangeTarget.idx}`
    return 'none'
  })()

  const profileData = resultProfiles?.[0]

  return (
    <div>
      {/* ══ 프로파일 차트 + 범위 편집 ══════════════════════════════════ */}
      <div className="param-section">
        프로파일 편집
        <Tip text="실행 후 단면 프로파일이 표시됩니다. 차트를 드래그해 피처 검색범위 또는 라인피팅 구간을 지정하세요." />
      </div>

      {/* 편집 대상 선택 */}
      <div className="param-row">
        <span className="param-label">편집 대상<Tip text="차트에서 좌클릭 드래그로 선택할 범위의 종류. 피처 검색범위 또는 라인피팅 구간" /></span>
        <select className="param-select" value={rangeTargetStr}
          onChange={e => {
            const v = e.target.value
            if (v === 'none') setRangeTarget({ kind: 'none' })
            else if (v.startsWith('feat-')) setRangeTarget({ kind: 'feature', idx: parseInt(v.slice(5)) })
            else if (v.startsWith('lf-'))   setRangeTarget({ kind: 'lineFit', idx: parseInt(v.slice(3)) })
          }}>
          <option value="none">선택 안함 (툴팁만)</option>
          {features.map((_, i) => (
            <option key={`feat-${i}`} value={`feat-${i}`}>F{i} 검색범위 설정</option>
          ))}
          {lineFits.map((_, i) => (
            <option key={`lf-${i}`} value={`lf-${i}`}>LF{i} 구간 설정</option>
          ))}
        </select>
      </div>

      {rangeTarget.kind !== 'none' && (
        <div className="param-empty" style={{ fontSize: 10, color: '#00e5ff' }}>
          차트에서 좌클릭 드래그 → {rangeTarget.kind === 'feature' ? `F${rangeTarget.idx} 검색범위` : `LF${rangeTarget.idx} 구간`} 설정
        </div>
      )}

      {/* 프로파일 차트 */}
      {profileData ? (
        <div style={{ marginBottom: 8 }}>
          <ProfileChart
            x={profileData.x} z={profileData.z}
            mode="line" height={200}
            features={caliperFeatures}
            lineFits={caliperLineFits}
            ranges={chartRanges}
            onRangeDrag={rangeTarget.kind !== 'none' ? handleRangeDrag : undefined}
          />
        </div>
      ) : (
        <div className="param-empty" style={{ marginBottom: 8 }}>실행 후 프로파일이 표시됩니다</div>
      )}

      {/* ══ 피처 목록 ══════════════════════════════════════════════════════ */}
      <div className="param-section" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <span>피처 ({features.length})<Tip text="프로파일에서 검출할 특징점. 드래그로 검색범위 지정 후 종류/방향 설정" /></span>
        <button className="param-btn" style={{ fontSize: 10, padding: '1px 6px' }}
          onClick={() => { setFeats([...features, defaultFeature()]); setRangeTarget({ kind: 'feature', idx: features.length }) }}>
          + 추가
        </button>
      </div>

      {features.map((f, i) => {
        const color = FEAT_COLORS[i % FEAT_COLORS.length]
        const hasRange = f.searchToMm > f.searchFromMm
        return (
          <div key={i} style={{ border: `1px solid ${color}55`, borderRadius: 4, marginBottom: 4, padding: '4px 6px' }}>
            <div className="param-row" style={{ marginBottom: 2 }}>
              <span className="param-label" style={{ fontWeight: 600, color }}>F{i}</span>
              <span style={{ fontSize: 10, color: '#888', flex: 1, marginLeft: 6 }}>
                {hasRange ? `${f.searchFromMm.toFixed(2)}~${f.searchToMm.toFixed(2)}mm` : '범위 없음'}
              </span>
              <button className="param-btn" style={{ fontSize: 10, padding: '1px 4px' }}
                title="이 피처 범위 편집"
                onClick={() => setRangeTarget({ kind: 'feature', idx: i })}>
                {rangeTarget.kind === 'feature' && rangeTarget.idx === i ? '편집중' : '편집'}
              </button>
              <button className="param-btn" style={{ fontSize: 10, padding: '1px 4px', marginLeft: 2 }}
                onClick={() => delFeat(i)}>삭제</button>
            </div>

            <div style={{ display: 'flex', gap: 6 }}>
              <div style={{ flex: 1 }}>
                <SelectRow label="kind" value={f.kind} onChange={v => setFeat(i, { ...f, kind: v })}
                  tooltip="검출 유형. edge=엣지, ridge=능선, valley=골, corner=코너, maxZ/minZ=극값">
                  <option value="edge"   title="경사 엣지. dir로 방향 선택">edge</option>
                  <option value="ridge"  title="볼록 피크">ridge</option>
                  <option value="valley" title="오목 피크">valley</option>
                  <option value="corner" title="곡률 변화 코너">corner</option>
                  <option value="maxZ"   title="Z 최대값 위치">maxZ</option>
                  <option value="minZ"   title="Z 최소값 위치">minZ</option>
                  <option value="mean"   title="구간 평균 Z">mean</option>
                </SelectRow>
              </div>
              {f.kind === 'edge' && (
                <div style={{ flex: 1 }}>
                  <SelectRow label="dir" value={f.dir} onChange={v => setFeat(i, { ...f, dir: v })}
                    tooltip="엣지 방향 필터">
                    <option value="any"     title="방향 무관">any</option>
                    <option value="rising"  title="Z 증가 방향">rising</option>
                    <option value="falling" title="Z 감소 방향">falling</option>
                  </SelectRow>
                </div>
              )}
            </div>

            {(f.kind === 'edge' || f.kind === 'ridge' || f.kind === 'valley') && (
              <NumField label="threshold(mm)" value={f.threshold ?? 0.05} step={0.005}
                tooltip="검출 최소 Z 변화량(mm). 노이즈보다 크게 설정"
                onChange={v => setFeat(i, { ...f, threshold: v })} />
            )}
            <NumField label="smoothWindow" value={f.smoothWindow ?? 3} step={1}
              tooltip="검출 전 이동평균 창 크기. 클수록 노이즈 억제, 작을수록 날카로운 검출"
              onChange={v => setFeat(i, { ...f, smoothWindow: v })} />
            <NumField label="nth" value={f.nth ?? 0} step={1}
              tooltip="N번째 검출 결과(0=첫 번째). 복수 후보 중 순서 선택"
              onChange={v => setFeat(i, { ...f, nth: v })} />
          </div>
        )
      })}
      {features.length === 0 && <div className="param-empty">[+ 추가]로 피처 추가. 추가 후 차트에서 드래그해 검색범위 설정</div>}

      {/* ══ 라인피팅 목록 ══════════════════════════════════════════════════ */}
      <div className="param-section" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <span>라인피팅 ({lineFits.length})<Tip text="구간 직선 피팅(z=a·s+b). 차트에서 드래그해 구간 지정" /></span>
        <button className="param-btn" style={{ fontSize: 10, padding: '1px 6px' }}
          onClick={() => { setFits([...lineFits, defaultLineFit()]); setRangeTarget({ kind: 'lineFit', idx: lineFits.length }) }}>
          + 추가
        </button>
      </div>

      {lineFits.map((lf, i) => {
        const color = LF_COLORS[i % LF_COLORS.length]
        return (
          <div key={i} style={{ border: `1px solid ${color}55`, borderRadius: 4, marginBottom: 4, padding: '4px 6px' }}>
            <div className="param-row" style={{ marginBottom: 2 }}>
              <span className="param-label" style={{ fontWeight: 600, color }}>LF{i}</span>
              <span style={{ fontSize: 10, color: '#888', flex: 1, marginLeft: 6 }}>
                {lf.fromMm.toFixed(2)}~{lf.toMm.toFixed(2)} mm
              </span>
              <button className="param-btn" style={{ fontSize: 10, padding: '1px 4px' }}
                onClick={() => setRangeTarget({ kind: 'lineFit', idx: i })}>
                {rangeTarget.kind === 'lineFit' && rangeTarget.idx === i ? '편집중' : '편집'}
              </button>
              <button className="param-btn" style={{ fontSize: 10, padding: '1px 4px', marginLeft: 2 }}
                onClick={() => delFit(i)}>삭제</button>
            </div>
            <div style={{ display: 'flex', gap: 6 }}>
              <div style={{ flex: 1 }}>
                <NumField label="from(mm)" value={lf.fromMm} step={0.1}
                  tooltip="피팅 구간 시작(mm). 차트 드래그로도 설정 가능"
                  onChange={v => setFit(i, { ...lf, fromMm: v })} />
              </div>
              <div style={{ flex: 1 }}>
                <NumField label="to(mm)" value={lf.toMm} step={0.1}
                  tooltip="피팅 구간 끝(mm)"
                  onChange={v => setFit(i, { ...lf, toMm: v })} />
              </div>
            </div>
          </div>
        )
      })}
      {lineFits.length === 0 && <div className="param-empty">[+ 추가]로 라인피팅 추가. 추가 후 차트에서 드래그해 구간 지정</div>}

      {/* ══ 거리 목록 ══════════════════════════════════════════════════════ */}
      <div className="param-section" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <span>거리 ({distances.length})<Tip text="두 피처 사이 거리 + 공차 판정" /></span>
        <button className="param-btn" style={{ fontSize: 10, padding: '1px 6px' }}
          onClick={() => setDists([...distances, defaultDistance()])}>+ 추가</button>
      </div>

      {distances.map((d, i) => (
        <div key={i} style={{ border: '1px solid #2a2d38', borderRadius: 4, marginBottom: 4, padding: '4px 6px' }}>
          <div className="param-row" style={{ marginBottom: 2 }}>
            <span className="param-label" style={{ fontWeight: 600 }}>D{i}</span>
            <button className="param-btn" style={{ fontSize: 10, padding: '1px 4px', marginLeft: 'auto' }}
              onClick={() => delDist(i)}>삭제</button>
          </div>
          <div style={{ display: 'flex', gap: 4, alignItems: 'center', marginBottom: 4 }}>
            <span style={{ fontSize: 11, color: FEAT_COLORS[d.from % FEAT_COLORS.length] }}>F{d.from}</span>
            <span style={{ fontSize: 11, color: '#888' }}>→</span>
            <span style={{ fontSize: 11, color: FEAT_COLORS[d.to % FEAT_COLORS.length] }}>F{d.to}</span>
            <select className="param-select" style={{ flex: 1, fontSize: 10 }}
              value={d.from} onChange={e => setDist(i, { ...d, from: parseInt(e.target.value) })}>
              {features.map((_, fi) => <option key={fi} value={fi}>F{fi}</option>)}
            </select>
            <span style={{ fontSize: 10, color: '#666' }}>→</span>
            <select className="param-select" style={{ flex: 1, fontSize: 10 }}
              value={d.to} onChange={e => setDist(i, { ...d, to: parseInt(e.target.value) })}>
              {features.map((_, fi) => <option key={fi} value={fi}>F{fi}</option>)}
            </select>
          </div>
          <SelectRow label="mode" value={d.mode ?? 'deltaS'} onChange={v => setDist(i, { ...d, mode: v })}
            tooltip="deltaS=횡축 거리, deltaZ=Z 높이 차">
            <option value="deltaS" title="s축 방향 거리">deltaS</option>
            <option value="deltaZ" title="Z 높이 차">deltaZ</option>
          </SelectRow>
          <div style={{ display: 'flex', gap: 4 }}>
            <div style={{ flex: 1 }}>
              <NumField label="nominal(mm)" value={d.nominalMm ?? 0} step={0.01}
                tooltip="기준 거리. 0이면 판정 없음"
                onChange={v => setDist(i, { ...d, nominalMm: v })} />
            </div>
            <div style={{ flex: 1 }}>
              <NumField label="+(mm)" value={d.plusMm ?? 0} step={0.01}
                tooltip="상한 공차"
                onChange={v => setDist(i, { ...d, plusMm: v })} />
            </div>
            <div style={{ flex: 1 }}>
              <NumField label="-(mm)" value={d.minusMm ?? 0} step={0.01}
                tooltip="하한 공차(양수 입력)"
                onChange={v => setDist(i, { ...d, minusMm: v })} />
            </div>
          </div>
        </div>
      ))}
      {distances.length === 0 && <div className="param-empty">거리측정 없음</div>}
    </div>
  )
}
