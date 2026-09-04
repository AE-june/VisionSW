import { useState, useEffect } from 'react'
import { TOOL_DEF_MAP } from '../types/tools'

interface Props {
  nodeId: string
  toolType: string
  label: string
  params: Record<string, unknown>
  onParamChange: (nodeId: string, params: Record<string, unknown>) => void
  onClose: () => void
  embedded?: boolean   // true = header 숨김 (NodePanel 내부에서 사용)
}

function RoiField({ label, value, onChange, tooltip }: {
  label: string
  value: { x: number; y: number; w: number; h: number }
  onChange: (v: typeof value) => void
  tooltip?: string
}) {
  const f = (field: keyof typeof value) => (
    <label className="param-roi-field">
      {field.toUpperCase()}
      <input
        type="number"
        value={value[field]}
        onChange={e => onChange({ ...value, [field]: parseFloat(e.target.value) || 0 })}
      />
    </label>
  )
  return (
    <div className="param-row">
      <span className="param-label">{label}<Tip text={tooltip} /></span>
      <div className="param-roi">{f('x')}{f('y')}{f('w')}{f('h')}</div>
    </div>
  )
}

function Tip({ text }: { text?: string }) {
  if (!text) return null
  return <span className="param-tip" data-tip={text}>ⓘ</span>
}

export function NumField({ label, value, onChange, step = 1, tooltip }: {
  label: string; value: number; onChange: (v: number) => void; step?: number; tooltip?: string
}) {
  // 로컬 문자열 상태 — 비우거나 입력 중간('', '-', '1.')을 허용. 유효 숫자일 때만 커밋.
  const [raw, setRaw] = useState(String(value))
  useEffect(() => { setRaw(String(value)) }, [value])
  return (
    <div className="param-row">
      <span className="param-label">{label}<Tip text={tooltip} /></span>
      <input className="param-input" type="text" inputMode="decimal" value={raw}
        onChange={e => {
          const s = e.target.value
          setRaw(s)
          if (s === '' || s === '-' || s === '.' || s === '-.' || s.endsWith('.')) return
          const n = parseFloat(s)
          if (!Number.isNaN(n)) onChange(n)
        }}
        onBlur={() => {
          const n = parseFloat(raw)
          if (Number.isNaN(n)) setRaw(String(value))
        }} />
    </div>
  )
}

function SelectField({ label, value, options, onChange, tooltip }: {
  label: string; value: string; options: string[]; onChange: (v: string) => void; tooltip?: string
}) {
  return (
    <div className="param-row">
      <span className="param-label">{label}<Tip text={tooltip} /></span>
      <select className="param-select" value={value} onChange={e => onChange(e.target.value)}>
        {options.map(o => <option key={o}>{o}</option>)}
      </select>
    </div>
  )
}

function CheckField({ label, value, onChange, tooltip }: {
  label: string; value: boolean; onChange: (v: boolean) => void; tooltip?: string
}) {
  return (
    <div className="param-row">
      <span className="param-label">{label}<Tip text={tooltip} /></span>
      <input type="checkbox" checked={value} onChange={e => onChange(e.target.checked)} />
    </div>
  )
}

function PathField({ label, value, onChange, toolType }: {
  label: string; value: string; onChange: (v: string) => void; toolType?: string
}) {
  const handleBrowse = async () => {
    const api = (window as Window & { electronAPI?: { openFile: (f?: Electron.FileFilter[]) => Promise<string | null> } }).electronAPI
    if (!api) return
    const filters = (toolType === 'HeightMapLoader' || toolType === 'ExposureMerge')
      ? [{ name: 'HeightMap (PNG)', extensions: ['png'] }, { name: 'All Files', extensions: ['*'] }]
      : toolType === 'CloudLoader'
      ? [{ name: 'Point Cloud', extensions: ['ply', 'xyz', 'asc', 'pcd', 'bin'] }, { name: 'All Files', extensions: ['*'] }]
      : [{ name: 'Image', extensions: ['png', 'jpg', 'jpeg', 'bmp', 'tiff'] }, { name: 'All Files', extensions: ['*'] }]
    const path = await api.openFile(filters)
    if (path) onChange(path)
  }

  return (
    <div className="param-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 4 }}>
      <span className="param-label">{label}</span>
      <div style={{ display: 'flex', gap: 6 }}>
        <button className="btn-browse" onClick={handleBrowse}>📂 찾아보기</button>
      </div>
      {value && <div className="param-path-display" title={value}>{value}</div>}
    </div>
  )
}

function PlaneFitParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const roiRef = params.roiRef     as { x: number; y: number; w: number; h: number }
  const roiM   = params.roiMeasure as typeof roiRef

  return <>
    <div className="param-section">기준면 ROI</div>
    <RoiField label="Reference" value={roiRef} onChange={v => set('roiRef', v)}
      tooltip="기준 평면을 피팅할 영역. HeightMap 전체 크기 대비 비율(0~1). 기준 표면(유리 등)이 있는 영역을 설정" />
    <div className="param-section">측정 ROI</div>
    <RoiField label="Measure"   value={roiM}   onChange={v => set('roiMeasure', v)}
      tooltip="평면 기준으로 높이를 측정할 영역. HeightMap 비율(0~1). 기준면 ROI와 겹쳐도 됨" />
  </>
}

export function NoiseFilterParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const ft = (params.filterType as string) ?? 'median'
  return <>
    <div className="param-section">HeightMap 필터</div>
    <SelectField label="종류" value={ft}
      options={['mean', 'median', 'gaussian', 'sor', 'bilateral']} onChange={v => set('filterType', v)}
      tooltip="mean=박스 평균, median=중앙값(점 노이즈 제거), gaussian=거리 가중 평균, sor=이상치 제거, bilateral=엣지 보존 평활화" />
    <NumField label="Kernel X" value={(params.kernelSizeX as number) ?? 3} step={2} onChange={v => set('kernelSizeX', v)}
      tooltip="X방향 커널 크기(픽셀, 홀수). 클수록 X방향으로 더 넓게 평활화. median은 3 또는 5만 지원" />
    <NumField label="Kernel Y" value={(params.kernelSizeY as number) ?? 3} step={2} onChange={v => set('kernelSizeY', v)}
      tooltip="Y방향 커널 크기(픽셀, 홀수). 클수록 Y방향으로 더 넓게 평활화. median은 3 또는 5만 지원" />
    {ft === 'sor' && (
      <NumField label="Std Ratio" value={(params.stdRatio as number) ?? 1.0} step={0.1} onChange={v => set('stdRatio', v)}
        tooltip="이웃 평균에서 N×표준편차 이상 벗어난 픽셀을 이상치로 제거. 1.0=1σ 기준, 작을수록 더 공격적으로 제거" />
    )}
    {ft === 'bilateral' && (
      <NumField label="Sigma Range (mm)" value={(params.sigmaRangeMm as number) ?? 0.02} step={0.005} onChange={v => set('sigmaRangeMm', v)}
        tooltip="Z값 유사도 허용 범위(mm). 측정 노이즈보다 크고 엣지 단차보다 작게 설정. 작을수록 엣지 보존 강화" />
    )}
  </>
}

function ProfileToCloudParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-section">스캔축 좌표 스냅</div>
    <NumField label="Transport Res(mm)" step={0.001} value={(params.transportResMm as number) ?? 0}
      onChange={v => set('transportResMm', v)}
      tooltip="0=사용 안 함(profile에 저장된 값 그대로). >0이면 스캔축 좌표를 이 값의 배수로 스냅 — Cloud to Profiles의 스캔 step(mm)과 동일하게 맞추면 부동소수점 오차 없이 NotchMeasure의 transportResMm 재그룹핑과 정확히 맞물림" />
    <div className="param-empty" style={{ fontSize: 10 }}>입력: Profile[]. Cloud to Profiles의 역변환</div>
  </>
}

function CsvWriterParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const path = params.path as string ?? ''

  const handleSave = async () => {
    const api = (window as Window & { electronAPI?: { saveFile: (f?: Electron.FileFilter[]) => Promise<string | null> } }).electronAPI
    if (!api?.saveFile) return
    const p = await api.saveFile([
      { name: 'CSV', extensions: ['csv'] },
      { name: 'All Files', extensions: ['*'] },
    ])
    if (p) set('path', p)
  }

  return <>
    <div className="param-section">출력 파일 (CSV)</div>
    <div className="param-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 4 }}>
      <span className="param-label">저장 위치</span>
      <button className="btn-browse" onClick={handleSave}>📁 폴더/파일 선택</button>
      {path && <div className="param-path-display" title={path}>{path}</div>}
    </div>
    <CheckField label="파일명에 저장 시각 추가" value={(params.addTimestamp as boolean) ?? false}
      onChange={v => set('addTimestamp', v)}
      tooltip="켜면 실행 시각(yyMMdd-HHmmfff)을 파일명 앞에 붙여 매번 새 파일로 저장. Profile[] 저장(스냅샷)엔 적합하지만, 측정값 append(반복성 로그) 모드에서 켜면 실행마다 새 파일이 생겨 누적되지 않음" />
    <div className="param-empty" style={{ fontSize: 10 }}>실행할 때마다 한 행씩 추가됩니다 (저장 시각 추가 옵션이 꺼져 있을 때)</div>
  </>
}

// HeightMapLoader: 단일 파일 / 폴더(연속검사) 모드 — 이미지 소스를 노드가 단독 소유
function HeightMapLoaderParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const mode = (params.mode as string) ?? 'file'
  const folder = (params.folder as string) ?? ''
  const index = (params.index as number) ?? 0
  const [files, setFiles] = useState<{ name: string; path: string }[]>([])

  useEffect(() => {
    if (mode === 'folder' && folder && window.electronAPI?.listFolderImages) {
      window.electronAPI.listFolderImages(folder).then(fs => {
        setFiles(fs)
        // 엔진은 path만 읽으므로, 선택된 인덱스의 파일로 path를 동기화 (초기/폴더변경 시)
        const ci = Math.min(Math.max(0, index), Math.max(0, fs.length - 1))
        if (fs[ci] && params.path !== fs[ci].path) onChange({ ...params, index: ci, path: fs[ci].path })
      })
    } else {
      setFiles([])
    }
    // mode/folder 변경 시에만 재나열 — index 변경은 gotoIndex가 path까지 함께 갱신
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [mode, folder])

  const pickFolder = async () => {
    const dir = await window.electronAPI?.openFolder?.()
    if (dir) {
      // 폴더 전체를 미리 로드하지 않는다(메모리 폭증 방지). 엔진은 실행 시 현재 index의
      // 파일만 그때그때 로드하고, 인터랙티브 캐시는 상한(LRU)으로 최근 몇 장만 유지한다.
      onChange({ ...params, folder: dir, index: 0 })
    }
  }

  const clampIdx = Math.min(Math.max(0, index), Math.max(0, files.length - 1))
  const cur = files[clampIdx]
  // 인덱스 이동 시 index와 path를 함께 세팅 (path가 실제 로드 대상)
  const gotoIndex = (i: number) => {
    const f = files[i]
    onChange(f ? { ...params, index: i, path: f.path } : { ...params, index: i })
  }

  return <>
    <div className="param-section">이미지 소스</div>
    <div className="param-row">
      <span className="param-label">모드</span>
      <select className="param-select" value={mode} onChange={e => set('mode', e.target.value)}>
        <option value="file">단일 파일</option>
        <option value="folder">폴더 (연속검사)</option>
      </select>
    </div>
    {mode === 'file' ? (
      <PathField label="파일" value={params.path as string ?? ''} onChange={v => set('path', v)} toolType="HeightMapLoader" />
    ) : <>
      <div className="param-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 4 }}>
        <span className="param-label">폴더</span>
        <button className="btn-browse" onClick={pickFolder}>📁 폴더 선택</button>
        {folder && <div className="param-path-display" title={folder}>{folder} · 이미지 {files.length}개</div>}
      </div>
      {files.length > 0 && (
        <div className="param-row">
          <span className="param-label">이미지</span>
          <div className="zl-idx">
            <button className="btn-browse" onClick={() => gotoIndex((clampIdx - 1 + files.length) % files.length)}>◀</button>
            <span className="zl-idx-cur" title={cur?.name}>{clampIdx + 1}/{files.length}</span>
            <button className="btn-browse" onClick={() => gotoIndex((clampIdx + 1) % files.length)}>▶</button>
          </div>
        </div>
      )}
      {cur && <div className="param-path-display" title={cur.name}>{cur.name}</div>}
      <div className="param-empty" style={{ fontSize: 10 }}>연속검사(📁 폴더검사)가 이 폴더를 순서대로 검사합니다</div>
    </>}
    <div className="param-section">분해능 (mm/px)</div>
    <NumField label="X Res" value={params.xResMm as number ?? 1.0} step={0.001} onChange={v => set('xResMm', v)}
      tooltip="픽셀당 X방향 실제 크기(mm). 센서 캘리브레이션 값 입력" />
    <NumField label="Y Res" value={params.yResMm as number ?? 1.0} step={0.001} onChange={v => set('yResMm', v)}
      tooltip="픽셀당 Y방향 실제 크기(mm). X Res와 다를 수 있음 (스캔 피치)" />
    <NumField label="Z Res" value={params.zResMm as number ?? 0.001} step={0.0001} onChange={v => set('zResMm', v)}
      tooltip="Z count 1당 높이(mm). 픽셀값 × Z Res = 실제 높이(mm). 센서 스펙 참조" />
  </>
}

// ExposureMerge: 분리된 다중노출 HeightMap 머지 + 리플렉션 제거
function ExposureMergeParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const exposureCount = (params.exposureCount as number) ?? 2
  return <>
    <div className="param-section">노출 설정</div>
    <div className="param-row">
      <label className="param-label">노출 수</label>
      <select
        className="param-select"
        value={exposureCount}
        onChange={e => set('exposureCount', Number(e.target.value))}
      >
        <option value={2}>2노출 (노출1/노출2)</option>
        <option value={3}>3노출 (노출1/2/3)</option>
      </select>
    </div>
    <div className="param-empty" style={{ fontSize: 10 }}>
      포트 인덱스 = 행 순서 (0=노출1, 1=노출2, 2=노출3)
    </div>
    <div className="param-section">오프셋 / 씨앗</div>
    <NumField label="매칭 허용 (cnt)" value={(params.matchTol as number) ?? 20} step={5} onChange={v => set('matchTol', v)}
      tooltip="두 노출 값이 모두 유효하고 |차이| ≤ 이 값이면 겹침 일치 판정. 전역 오프셋(노출 간 Z 바이어스) 추정에 사용. 너무 작으면 오프셋 표본 부족, 너무 크면 리플렉션도 포함. 단위: raw count" />
    <NumField label="리플 씨앗 허용 (cnt)" value={(params.reflTol as number) ?? -1} step={5} onChange={v => set('reflTol', v)}
      tooltip="-1이면 매칭 허용(matchTol)과 동일값 사용. 따로 설정 시 이 값 이하 차이만 신뢰 씨앗으로 인정 → 클수록 씨앗↑·덜 제거, 작을수록 더 제거. 단위: raw count" />
    <div className="param-section">연속성 확장</div>
    <NumField label="X 허용치 (cnt/px)" value={(params.tolX as number) ?? 5} step={1} onChange={v => set('tolX', v)}
      tooltip="BFS 확장 시 스캔(이송)방향 이웃과의 Z 허용 차이. 스캔 피치가 측면보다 크면 작게 잡아야 리플렉션 streak를 막을 수 있음. 단위: count/px" />
    <NumField label="Y 허용치 (cnt/px)" value={(params.tolY as number) ?? 30} step={5} onChange={v => set('tolY', v)}
      tooltip="BFS 확장 시 측면(래터럴)방향 이웃과의 Z 허용 차이. 측면 피치(~6µm)가 작아 값이 더 작아도 됨. 단위: count/px" />
    <NumField label="갭 점프 (px)" value={(params.gapK as number) ?? 0} step={1} onChange={v => set('gapK', Math.round(v))}
      tooltip="BFS가 NaN 픽셀을 건너뛸 최대 거리. 0이면 건너뛰기 없음. 늘리면 구멍 너머까지 연속성 전파 — 리플렉션이 NaN 경계로 막혀있을 때 유용. 단위: px" />
    <div className="param-section">필터</div>
    <div className="param-row">
      <label className="param-label">리플렉션 제거</label>
      <input type="checkbox" checked={(params.removeReflection as boolean) ?? true}
        onChange={e => set('removeReflection', e.target.checked)}
        title="끄면 오프셋 보정 + 노출1 우선 머지만 수행(BFS 연속성 필터 생략). 리플렉션이 없는 데이터에서 빠르게 확인용." />
    </div>
    <NumField label="밴드 수 (0=자동)" value={(params.bands as number) ?? 0} step={1} onChange={v => set('bands', Math.round(v))}
      tooltip="병렬 처리 밴드 수. 0이면 CPU 스레드 수 자동. 높일수록 행 분할↑ — 단 밴드 경계 컨텍스트(OV=160행)가 겹쳐 재연산됨. 보통 기본값이 최적." />
  </>
}

// GapFill: 결측(NaN) 픽셀 보간. maxGap 이하만 채움. method별 파라미터 노출.
function GapFillParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const method = (params.method as string) ?? 'neighbor'
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>결측(NaN) 픽셀을 보간해 메웁니다. maxGap 이하 구멍만 채우고 큰 구멍은 남깁니다(가짜 표면 방지).</div>
    <div className="param-section">보간</div>
    <SelectField label="방법" value={method} options={['neighbor', 'median', 'laplace', 'nearest', 'idw', 'linear', 'anisotropic']} onChange={v => set('method', v)}
      tooltip="neighbor=반복 이웃평균(매끈) · median=이웃 중앙값(엣지 보존) · laplace=PDE(큰 구멍 매끈) · nearest=최근접(단차 유지) · idw=역거리 가중 · linear=행/열 선형 · anisotropic=엣지 인지 확산(엣지 보존+면 매끈)" />
    <NumField label="최대 구멍 (px)" value={params.maxGap as number ?? 5} step={1} onChange={v => set('maxGap', v)}
      tooltip="가장 가까운 유효 픽셀까지 거리가 이 값 이하인 결측만 채움. 큰 구멍 중앙은 NaN으로 남김" />
    {(method === 'neighbor' || method === 'median') && (
      <NumField label="최소 유효이웃" value={params.minValidNeighbors as number ?? 3} step={1} onChange={v => set('minValidNeighbors', v)}
        tooltip="8-이웃 중 유효 픽셀이 이 개수 이상일 때만 채움 (튄 값으로 채우는 것 방지)" />
    )}
    {method === 'anisotropic' && (
      <NumField label="엣지 민감도 σ (cnt)" value={params.edgeSigma as number ?? 30} step={5} onChange={v => set('edgeSigma', v)}
        tooltip="이웃과 Z 차이가 이 값(raw count)보다 크면 엣지로 보고 안 섞음. 작을수록 엣지 더 예민하게 보존. 실제 단차보다 작게 설정" />
    )}
    {method === 'idw' && <>
      <NumField label="IDW 반경 (px)" value={params.idwRadius as number ?? 8} step={1} onChange={v => set('idwRadius', v)}
        tooltip="가중평균에 참고할 반경(px). 클수록 멀리까지 참고" />
      <NumField label="IDW 거듭제곱" value={params.idwPower as number ?? 2} step={0.5} onChange={v => set('idwPower', v)}
        tooltip="거리 가중 지수. 클수록 가까운 값이 우세" />
    </>}
    <div className="param-section">출력 단계</div>
    <div className="param-row">
      <label className="param-label">저장 출력</label>
      <select className="param-select" value={params.outputStage as number ?? 0} onChange={e => set('outputStage', Number(e.target.value))}>
        <option value={0}>1. 메운 결과</option>
        <option value={1}>2. 원본</option>
        <option value={2}>3. 메운 영역</option>
      </select>
    </div>
    <div className="param-empty" style={{ fontSize: 10 }}>결과창 드롭다운에서 "메운 영역"으로 어디를 채웠는지 확인하세요.</div>
  </>
}

// ExposureSplit: 인터리브 다중노출 HeightMap → 노출별 분리
function ExposureSplitParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const splitCount = (params.splitCount as number) ?? 2
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>인터리브 Z PNG를 연결하세요. 출력 포트 인덱스 = 행 순서 (0부터).</div>
    <div className="param-section">분리</div>
    <div className="param-row">
      <label className="param-label">노출 수</label>
      <select className="param-select" value={splitCount} onChange={e => set('splitCount', Number(e.target.value))}>
        <option value={2}>2노출 (노출1/노출2)</option>
        <option value={3}>3노출 (노출1/2/3)</option>
      </select>
    </div>
  </>
}

// ExposureMerge3 (3노출 머지): 저/중/장 인터리브 → 캐스케이드로 2번 머지(저>중>장 우선). ExposureMerge2와 동일 규칙, 청크 모드 없음.
function ExposureMerge3Params({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const removeRefl = params.removeReflection as boolean ?? true
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>HeightMapLoader에서 저/중/장 3노출 인터리브 Z PNG(행 순서 저·중·장 반복)를 연결하세요. 우선순위 저&gt;중&gt;장.</div>
    <div className="param-section">머지</div>
    <NumField label="일치 허용 (cnt)" value={params.matchTol as number ?? 20} step={5} onChange={v => set('matchTol', v)}
      tooltip="두 노출이 모두 유효하고 |차이| ≤ 이 값이면 겹침 일치로 보고 오프셋(offset) 추정에 사용. 단위: raw count" />
    <CheckField label="반해상도 출력 (Y×3)" value={params.halfRes as boolean ?? true} onChange={v => set('halfRes', v)}
      tooltip="머지는 3중 프로파일당 1행이라 켜면 n행·Y피치×3로 출력. 끄면 각 행을 3배 복제해 원본 높이 유지" />
    <div className="param-section">리플렉션 제거 (연속성 필터)</div>
    <CheckField label="리플렉션 제거" value={removeRefl} onChange={v => set('removeReflection', v)}
      tooltip="끄면 연속성 검증 없이 유효한 노출을 그대로 채택(저>중>장). 글라스처럼 저노출이 빠지고 중·장만 남는 전환부가 밴드로 지워지는 걸 방지. 켜면 fill 리플렉션을 연속성으로 제거(기본)." />
    {removeRefl && <>
      <NumField label="리플 허용 (cnt)" value={params.reflTol as number ?? 30} step={10} onChange={v => set('reflTol', v)}
        tooltip="리플렉션 씨앗 허용치. 두 노출 |차이| ≤ 이 값이면 신뢰 씨앗. 클수록 씨앗↑ → 고노출 fill 더 유지 → 덜 제거. z분해능이 작아 count가 커졌으면 이 값도 키우세요(밴드 방지). 단위: raw count" />
      <NumField label="X 허용치 (cnt/px)" value={params.tolX as number ?? 10} step={1} onChange={v => set('tolX', v)}
        tooltip="연속성 확장 시 X방향 이웃과의 Z 허용 차이. 단위: count/px" />
      <NumField label="Y 허용치 (cnt/px)" value={params.tolY as number ?? 100} step={10} onChange={v => set('tolY', v)}
        tooltip="연속성 확장 시 Y방향 이웃과의 Z 허용 차이. 단위: count/px" />
      <NumField label="갭 점프 (px)" value={params.gapK as number ?? 2} step={1} onChange={v => set('gapK', v)}
        tooltip="연속성이 NaN(구멍) 픽셀을 건너뛸 최대 거리. 허용치는 건너뛴 거리에 비례해 증가" />
    </>}
    <div className="param-section">청크 연산 (메모리 바운드/스트리밍)</div>
    <CheckField label="청크 단위 연산" value={params.chunkMode as boolean ?? false} onChange={v => set('chunkMode', v)}
      tooltip="끄면 전체 이미지를 한 번에 연산(기본). 켜면 입력을 겹침 포함 청크로 나눠 캐스케이드 — 작업 메모리를 청크 크기로 바운드. 청크 모드는 최종 출력만(중간 단계 생략)" />
    {(params.chunkMode as boolean) && <>
      <NumField label="청크 행 수 (입력)" value={params.chunkRows as number ?? 1000} step={100} onChange={v => set('chunkRows', v)}
        tooltip="청크 하나에 담을 입력 프로파일(행) 수. 출력행 = /3. 클수록 겹침 재연산 비율↓" />
      <NumField label="겹침 행 수 (입력)" value={params.overlapRows as number ?? 180} step={30} onChange={v => set('overlapRows', v)}
        tooltip="청크 위·아래로 확장해 함께 연산하는 입력행 수(출력=/3). 캐스케이드 2단계 연속성이 청크 경계를 넘어 이어지도록 하는 마진. SDC 실측: 40출력행(120입력)이면 전체모드와 0px 동일. 기본 60출력행(180입력, 마진). 클수록 이음매 안전·연산량↑" />
    </>}
    <div className="param-section">Intensity 머지 (선택)</div>
    <div className="param-empty" style={{ fontSize: 10 }}>포트2에 intensity(저/중/장 인터리브) 연결 시 머지 intensity를 포트2로 출력. 포트3에 thickness 연결 시 목표 두께에 가장 근접한 노출의 intensity를 선택(없으면 최대 밝기).</div>
    <NumField label="목표 두께 (cnt)" value={params.targetThickness as number ?? 30} step={1} onChange={v => set('targetThickness', v)}
      tooltip="레이저선 두께가 이 값에 가장 가까운 노출의 intensity를 픽셀별로 선택. 언더/포화 노출은 두께가 목표에서 멀어 자동 배제. thickness 입력(포트3) 있을 때만 사용." />
    <div className="param-empty" style={{ fontSize: 10 }}>중간 단계(저·중 머지 / 저·중·장 원본)는 결과창 드롭다운(디스플레이 전용, 비청크)에서만 확인됩니다.</div>
  </>
}

// ExposureFilter (split-free 3노출 필터): 분리하지 않고 datum 정규화 → 대칭 일관성 제거 → gap fill.
//   EM3와 달리 저노출 리플렉션도 대칭으로 걸러진다. 기본 출력은 전해상도(h행).
function ExposureFilterParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>HeightMapLoader에서 저/중/장 3노출 인터리브 Z PNG(행 순서 저·중·장 반복)를 연결하세요. split 없이 원본 격자에서 필터링합니다.</div>
    <div className="param-section">Stage 0 — 노출 datum 정규화</div>
    <NumField label="datum 윈도우 (행)" value={params.datumWindow as number ?? 9} step={3} onChange={v => set('datumWindow', v)}
      tooltip="클래스별 offset 산출용 Y방향 로버스트 선피팅 윈도우. 3의 배수여야 주기3 리플이 윈도우 내에서 상쇄됨(아니면 자동 올림). 단위: 원본 행" />
    <NumField label="datum 반복" value={params.datumIters as number ?? 3} step={1} onChange={v => set('datumIters', v)}
      tooltip="교대 추정(선피팅↔offset median) 반복 횟수. 3이면 충분." />
    <div className="param-section">Stage 1 — 대칭 일관성 리플렉션 제거</div>
    <NumField label="τ 기본 (cnt)" value={params.tauBase as number ?? 30} step={5} onChange={v => set('tauBase', v)}
      tooltip="자기 제외 Y선피팅 예측과의 허용 차이 기본값. |z' − pred| > τ 면 제거. 평면부 반사가 남으면 줄이고, 정상 표면이 지워지면 키우세요. 단위: raw count" />
    <NumField label="τ 기울기항" value={params.tauSlope as number ?? 0.5} step={0.1} onChange={v => set('tauSlope', v)}
      tooltip="τ = tauBase + tauSlope·|기울기|. 급경사 벽이 잘못 제거되면 키우세요. 단위: 무차원(count/row에 곱)" />
    <NumField label="일관성 윈도우 (행)" value={params.consistWindow as number ?? 9} step={3} onChange={v => set('consistWindow', v)}
      tooltip="이상치 판정용 Y선피팅 윈도우(자기 제외). 단위: 원본 행" />
    <NumField label="최소 클래스 이웃" value={params.minClassNeighbors as number ?? 2} step={1} onChange={v => set('minClassNeighbors', v)}
      tooltip="근거 가드: 서로 다른 2개 이상 클래스가 각각 이만큼 유효해야 판정. 못 미치면 보존(장노출만 유효한 어두운 영역 자동 살림)." />
    <div className="param-section">Stage 2 — Gap Fill</div>
    <NumField label="최대 갭 (행)" value={params.maxGapRows as number ?? 6} step={1} onChange={v => set('maxGapRows', v)}
      tooltip="제거로 생긴 세로 구멍을 위·아래 유효값으로 선형보간. streak가 이 값을 넘으면 NaN 유지. 단위: 원본 행" />
    <div className="param-section">출력</div>
    <CheckField label="반해상도 출력 (Y×3)" value={params.halfRes as boolean ?? false} onChange={v => set('halfRes', v)}
      tooltip="기본 끔: 전해상도 h행(진짜 Y해상도, 3피치 다른 실측) 그대로 출력. 켜면 각 3행 묶음을 median으로 n행·Y피치×3 축약(EM3 출력 기하 호환용)." />
    <div className="param-section">Intensity (선택)</div>
    <div className="param-empty" style={{ fontSize: 10 }}>포트2에 intensity 연결 시 Z 유효 위치의 intensity를 포트2로 출력. 반해상도일 때만 포트3 thickness로 노출 선택.</div>
    <NumField label="목표 두께 (cnt)" value={params.targetThickness as number ?? 30} step={1} onChange={v => set('targetThickness', v)}
      tooltip="반해상도 축약 시 레이저선 두께가 이 값에 가장 가까운 노출의 intensity 선택(없으면 최대 밝기). thickness 입력(포트3) 있을 때만." />
  </>
}

// ImageSaver: 입력(HeightMap/이미지)을 저장. 폴더(필수)+파일명(선택,비우면 소스명)+포맷.
function ImageSaverParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const folder = params.folder as string ?? ''
  const filename = params.filename as string ?? ''
  const format = params.format as string ?? 'png'
  const pickFolder = async () => {
    const dir = await window.electronAPI?.openFolder?.()
    if (dir) set('folder', dir)
  }
  return <>
    <div className="param-section">저장 위치</div>
    <div className="param-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 4 }}>
      <span className="param-label">폴더 (필수)</span>
      <button className="btn-browse" onClick={pickFolder}>📁 폴더 선택</button>
      {folder && <div className="param-path-display" title={folder}>{folder}</div>}
    </div>
    <div className="param-row">
      <span className="param-label">파일명 (선택)</span>
      <input className="param-input" type="text" value={filename} placeholder="비우면 소스 파일명"
        onChange={e => set('filename', e.target.value)} />
    </div>
    <SelectField label="포맷" value={format} options={['png', 'tif', 'jpg', 'bmp']} onChange={v => set('format', v)}
      tooltip="png/tif=16bit(원본 raw 값 보존), jpg/bmp=8bit(min-max 정규화)" />
    <div className="param-empty" style={{ fontSize: 10 }}>
      저장 형식: 폴더 / HHMMSSmmm_(파일명 또는 소스명).포맷 — 밀리초 접두사로 폴더검사 병렬 저장에도 안 겹칩니다.
    </div>
  </>
}

// HeightMapToCloud: HeightMap → PointCloud3D 변환 (서브샘플 step)
function HeightMapToCloudParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>HeightMap의 유효 픽셀을 (x,y,z)mm 3D 점으로 변환합니다. 무효(NaN) 픽셀은 제외됩니다.</div>
    <div className="param-section">서브샘플</div>
    <NumField label="Step (px)" value={params.step as number ?? 1} step={1} onChange={v => set('step', v)}
      tooltip="N픽셀마다 1점 추출. 1=모든 픽셀(대용량). 대형 HeightMap은 2~4로 줄여 파일 크기·속도 개선" />
  </>
}

// ExposureMergeCloud: 인터리브 HeightMap → 이중노출 머지 → PointCloud3D (X는 균일, col×xRes)
function ExposureMergeCloudParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>인터리브 HeightMap(짝=저/홀=고)을 이중노출 머지 후 3D 점으로 출력. VisionSW는 균일 X(col×xRes) — per-point 보정 X는 SDK 경로 전용.</div>
    <div className="param-section">머지 / 연속성 필터</div>
    <NumField label="일치 허용 (cnt)" value={params.matchTol as number ?? 20} step={5} onChange={v => set('matchTol', v)}
      tooltip="저·장노출 |차이| ≤ 이 값이면 겹침 일치. 오프셋·씨앗에 사용" />
    <NumField label="X 허용치 (cnt/px)" value={params.tolX as number ?? 5} step={1} onChange={v => set('tolX', v)}
      tooltip="연속성 확장 X방향 Z 허용 차이" />
    <NumField label="Y 허용치 (cnt/px)" value={params.tolY as number ?? 30} step={5} onChange={v => set('tolY', v)}
      tooltip="연속성 확장 Y방향 Z 허용 차이" />
    <NumField label="갭 점프 (px)" value={params.gapK as number ?? 0} step={1} onChange={v => set('gapK', v)}
      tooltip="연속성이 NaN 픽셀을 건너뛸 최대 거리" />
  </>
}

// CloudSelect: PointCloud 배열에서 특정 인덱스 선택
function CloudSelectParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const cloudIdx = (params.cloudIdx as number) ?? 0
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>PointCloudSplit 등 배열 출력 툴 이후에 연결. 원하는 인덱스를 선택해 단일 PointCloud로 출력합니다.</div>
    <div className="param-section">선택</div>
    <div className="param-row">
      <label className="param-label">Cloud 인덱스</label>
      <input className="param-input" type="number" min={0} value={cloudIdx}
        onChange={e => set('cloudIdx', Number(e.target.value))} />
    </div>
  </>
}

// PointCloudSOR: Statistical Outlier Removal
function PointCloudSORParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const roiEnabled = (params.roiEnabled as boolean) ?? false
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>
      k-NN 평균거리 기반 이상점 제거. 전역 평균 + stdDevMult × 표준편차 초과 점 제거.
    </div>
    <div className="param-section">파라미터</div>
    <NumField label="k 이웃 수" value={(params.kNeighbors as number) ?? 20}
      onChange={v => set('kNeighbors', Math.round(v))}
      tooltip="각 점에서 가장 가까운 이웃 k개를 찾아 평균 거리를 계산. 클수록 통계가 안정되지만 느려짐. 보통 10~50." />
    <NumField label="표준편차 배수" value={(params.stdDevMult as number) ?? 1.0}
      onChange={v => set('stdDevMult', v)}
      tooltip="제거 임계값 = 전역 평균 + 배수 × 표준편차. 낮을수록(0.5) 공격적으로 제거, 높을수록(2.0) 보수적. 보통 0.5~2.0." />
    <NumField label="Grid cell (mm)" value={(params.cellSizeMm as number) ?? 0.1}
      onChange={v => set('cellSizeMm', v)}
      tooltip="이웃 탐색용 격자 셀 크기. 클수록 탐색 범위 넓어지나 느려짐. 스캐너 점 간격(예: 0.006mm)의 10~20배 정도가 적당." />
    <div className="param-section">ROI (선택 영역)</div>
    <div className="param-row">
      <label className="param-label">ROI 활성화</label>
      <input type="checkbox" checked={roiEnabled}
        onChange={e => set('roiEnabled', e.target.checked)} />
    </div>
    {roiEnabled && <>
      <NumField label="X min (mm)" value={(params.roiXMin as number) ?? -1000} onChange={v => set('roiXMin', v)}
        tooltip="SOR을 적용할 X 범위 최솟값. 이 범위 밖 점은 필터 없이 그대로 출력." />
      <NumField label="X max (mm)" value={(params.roiXMax as number) ?? 1000}  onChange={v => set('roiXMax', v)}
        tooltip="SOR을 적용할 X 범위 최댓값." />
      <NumField label="Y min (mm)" value={(params.roiYMin as number) ?? -1000} onChange={v => set('roiYMin', v)}
        tooltip="SOR을 적용할 Y 범위 최솟값." />
      <NumField label="Y max (mm)" value={(params.roiYMax as number) ?? 1000}  onChange={v => set('roiYMax', v)}
        tooltip="SOR을 적용할 Y 범위 최댓값." />
      <NumField label="Z min (mm)" value={(params.roiZMin as number) ?? -1000} onChange={v => set('roiZMin', v)}
        tooltip="SOR을 적용할 Z 범위 최솟값. 리플렉션이 특정 Z 높이에만 있으면 Z 범위만 지정해도 충분." />
      <NumField label="Z max (mm)" value={(params.roiZMax as number) ?? 1000}  onChange={v => set('roiZMax', v)}
        tooltip="SOR을 적용할 Z 범위 최댓값." />
    </>}
  </>
}

// CloudToHeightMap: PointCloud3D → HeightMap 변환
function CloudToHeightMapParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const autoRange = (params.autoRange as boolean) ?? true
  return <>
    <div className="param-section">변환 모드</div>
    <SelectField label="모드" value={(params.mode as string) ?? 'top'}
      options={['top', 'bottom', 'mean']}
      onChange={v => set('mode', v)}
      tooltip="같은 XY 위치에 여러 Z가 있을 때 선택 방법. top=표면 최상단, bottom=최하단, mean=평균." />
    <div className="param-section">해상도</div>
    <NumField label="X 해상도 (mm/px)" value={(params.xResMm as number) ?? 0.1}
      onChange={v => set('xResMm', v)}
      tooltip="HeightMap X 방향 픽셀 크기(mm). 스캐너 래터럴 해상도와 맞추거나 더 크게 설정." />
    <NumField label="Y 해상도 (mm/px)" value={(params.yResMm as number) ?? 0.1}
      onChange={v => set('yResMm', v)}
      tooltip="HeightMap Y 방향 픽셀 크기(mm). 스캐너 이송 간격과 맞추거나 더 크게 설정." />
    <NumField label="Z 해상도 (mm/count)" value={(params.zResMm as number) ?? 0.001}
      onChange={v => set('zResMm', v)}
      tooltip="Z 인코딩 단위. 0.001=1µm 분해능, 유효범위 ±32.767mm. 큰 공작물은 0.01 이상으로 설정." />
    <div className="param-section">XY 범위</div>
    <div className="param-row">
      <label className="param-label">자동 범위</label>
      <input type="checkbox" checked={autoRange}
        onChange={e => set('autoRange', e.target.checked)} />
    </div>
    {!autoRange && <>
      <NumField label="X min (mm)" value={(params.xMin as number) ?? -100} onChange={v => set('xMin', v)}
        tooltip="HeightMap XY 범위 수동 지정 시 X 최솟값." />
      <NumField label="X max (mm)" value={(params.xMax as number) ?? 100}  onChange={v => set('xMax', v)}
        tooltip="HeightMap XY 범위 수동 지정 시 X 최댓값." />
      <NumField label="Y min (mm)" value={(params.yMin as number) ?? -100} onChange={v => set('yMin', v)}
        tooltip="HeightMap XY 범위 수동 지정 시 Y 최솟값." />
      <NumField label="Y max (mm)" value={(params.yMax as number) ?? 100}  onChange={v => set('yMax', v)}
        tooltip="HeightMap XY 범위 수동 지정 시 Y 최댓값." />
    </>}
  </>
}

// CloudSaver: PointCloud3D → PLY/XYZ 파일 저장
function CloudSaverParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const folder = params.folder as string ?? ''
  const filename = params.filename as string ?? ''
  const format = params.format as string ?? 'ply'
  const cloudIdx = (params.cloudIdx as number) ?? 0
  const pickFolder = async () => {
    const dir = await window.electronAPI?.openFolder?.()
    if (dir) set('folder', dir)
  }
  return <>
    <div className="param-section">저장 위치 (PointCloud)</div>
    <div className="param-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 4 }}>
      <span className="param-label">폴더 (필수)</span>
      <button className="btn-browse" onClick={pickFolder}>📁 폴더 선택</button>
      {folder && <div className="param-path-display" title={folder}>{folder}</div>}
    </div>
    <div className="param-row">
      <span className="param-label">파일명 (선택)</span>
      <input className="param-input" type="text" value={filename} placeholder="비우면 소스 파일명"
        onChange={e => set('filename', e.target.value)} />
    </div>
    <SelectField label="포맷" value={format} options={['ply', 'xyz', 'bin']} onChange={v => set('format', v)}
      tooltip="ply=ascii(CloudCompare/MeshLab 호환), xyz=텍스트(x y z 한 줄씩), bin=생 바이너리(float32 x,y,z 연속·헤더없음·최고속, 점수=파일크기/12)" />
    <div className="param-section">PointCloudSplit 연결 시</div>
    <div className="param-row">
      <label className="param-label">Cloud 인덱스</label>
      <select className="param-select" value={cloudIdx} onChange={e => set('cloudIdx', Number(e.target.value))}>
        <option value={0}>0 — 노출1</option>
        <option value={1}>1 — 노출2</option>
        <option value={2}>2 — 노출3</option>
      </select>
    </div>
    <div className="param-empty" style={{ fontSize: 10 }}>
      저장 형식: 폴더 / HHMMSSmmm_(파일명 또는 소스명).포맷
    </div>
  </>
}

// CloudZReduce: 같은 (x,y) bin 내 여러 Z → 1개 선택
function CloudZReduceParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const roiEnabled = (params.roiEnabled as boolean) ?? false
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>같은 (x,y) 위치에 Z가 여러 개인 포인트클라우드에서 reduce로 Z 1개를 선택합니다.</div>
    <div className="param-section">Z 선택</div>
    <SelectField label="Reduce" value={(params.reduce as string) ?? 'max'} options={['max', 'min', 'mean', 'median', 'continuity']}
      onChange={v => set('reduce', v)}
      tooltip="max=가장 높은 Z · min=가장 낮은 Z · mean=평균 · median=중앙값 · continuity=앞뒤 스캔과 연속되는 Z 선택" />
    {(params.reduce as string) === 'continuity' && (
      <NumField label="이웃 범위" value={(params.neighborRange as number) ?? 2}
        onChange={v => set('neighborRange', Math.round(v))}
        tooltip="앞뒤 몇 개 스캔(X bin)을 참조할지. 클수록 전역 연속성 강조, 작을수록 국소. 보통 2~5." />
    )}
    <div className="param-section">Binning</div>
    <NumField label="X 스텝 (mm)" value={(params.xStepMm as number) ?? 0} step={0.001}
      onChange={v => set('xStepMm', v)}
      tooltip="0 = exact 좌표 기준 그룹핑(권장). 0보다 크면 해당 거리 내 점을 같은 bin으로 묶음." />
    <NumField label="Y 스텝 (mm)" value={(params.yStepMm as number) ?? 0} step={0.001}
      onChange={v => set('yStepMm', v)}
      tooltip="0 = exact 좌표 기준 그룹핑(권장)." />
    <div className="param-section">ROI (선택 영역)</div>
    <div className="param-row">
      <label className="param-label">ROI 활성화</label>
      <input type="checkbox" checked={roiEnabled}
        onChange={e => set('roiEnabled', e.target.checked)} />
    </div>
    {roiEnabled && <>
      <NumField label="X min (mm)" value={(params.roiXMin as number) ?? -1000} onChange={v => set('roiXMin', v)}
        tooltip="Z reduce를 적용할 X 범위 최솟값. 이 범위 밖 점은 그대로 출력." />
      <NumField label="X max (mm)" value={(params.roiXMax as number) ?? 1000}  onChange={v => set('roiXMax', v)} />
      <NumField label="Y min (mm)" value={(params.roiYMin as number) ?? -1000} onChange={v => set('roiYMin', v)} />
      <NumField label="Y max (mm)" value={(params.roiYMax as number) ?? 1000}  onChange={v => set('roiYMax', v)} />
      <NumField label="Z min (mm)" value={(params.roiZMin as number) ?? -1000} onChange={v => set('roiZMin', v)} />
      <NumField label="Z max (mm)" value={(params.roiZMax as number) ?? 1000}  onChange={v => set('roiZMax', v)} />
    </>}
  </>
}

// PointCloudSplit: 인터리브 다중노출 PointCloud를 노출별로 분리
function PointCloudSplitParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const splitCount = (params.splitCount as number) ?? 2
  const scanAxis = (params.scanAxis as string) ?? 'x'
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>인터리브된 다중노출 PointCloud를 스캔 행 순서로 분리합니다. CloudSaver의 Cloud 인덱스로 저장할 노출을 선택하세요.</div>
    <div className="param-section">분리</div>
    <div className="param-row">
      <label className="param-label">분할 수</label>
      <select className="param-select" value={splitCount} onChange={e => set('splitCount', Number(e.target.value))}>
        <option value={2}>2 (저/장)</option>
        <option value={3}>3 (저/중/장)</option>
      </select>
    </div>
    <div className="param-row">
      <label className="param-label">스캔축</label>
      <select className="param-select" value={scanAxis} onChange={e => set('scanAxis', e.target.value)}>
        <option value="x">X (기본)</option>
        <option value="y">Y</option>
      </select>
    </div>
    <NumField label="스캔 스텝 (mm)" value={(params.scanStepMm as number) ?? 0.004} step={0.001}
      onChange={v => set('scanStepMm', v)}
      tooltip="스캔축 binning 단위(mm). 인터리브 이중노출 간격. 단일노출 영역은 2× 간격이어도 자동 처리." />
  </>
}


function LineFitParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const feature = (params.feature as string) ?? 'ridge'
  return <>
    <div className="param-section">라인 검출</div>
    <SelectField label="특징" value={feature} options={['ridge', 'valley', 'edge']}
      onChange={v => set('feature', v)}
      tooltip="ridge=높이 최대(능선) · valley=높이 최소(골) · edge=임계 교차(계단 엣지)" />
    <SelectField label="스캔방향" value={(params.scanDir as string) ?? 'lr'} options={['lr', 'rl', 'tb', 'bt']}
      onChange={v => set('scanDir', v)}
      tooltip="캘리퍼 스캔축. lr/rl=행마다 좌→우/우→좌 (세로 라인 검출) · tb/bt=열마다 위→아래/아래→위 (가로 라인 검출)" />
    {feature === 'edge' && <>
      <NumField label="임계값(raw)" value={(params.threshold as number) ?? 0} step={1}
        onChange={v => set('threshold', v)}
        tooltip="raw 높이 임계. 이 값 교차 지점을 엣지로 검출" />
      <CheckField label="상승엣지" value={(params.risingEdge as boolean) ?? true}
        onChange={v => set('risingEdge', v)}
        tooltip="on=낮→높 교차 · off=높→낮 교차" />
    </>}
    <SelectField label="피팅 방식" value={(params.fitMethod as string) ?? 'leastSquares'}
      options={['leastSquares', 'ransac']} onChange={v => set('fitMethod', v)}
      tooltip="leastSquares=PCA 총최소제곱(빠름, 이상점 약함) · ransac=인라이어 기반(이상점 강함)" />
    {(params.fitMethod as string) === 'ransac' && <>
      <NumField label="RANSAC 허용(mm)" value={(params.ransacTolMm as number) ?? 0.5} step={0.1}
        onChange={v => set('ransacTolMm', v)}
        tooltip="인라이어 판정 수직거리 허용치(mm). 이 이내 점만 피팅에 사용" />
      <NumField label="RANSAC 반복" value={(params.ransacIters as number) ?? 100} step={10}
        onChange={v => set('ransacIters', v)}
        tooltip="무작위 표본 반복수. 클수록 안정, 느림. 시드 고정(반복성 보장)" />
    </>}
    <div className="param-empty" style={{ fontSize: 10 }}>출력: Line(각도·중심) · 측정값 angleDeg/cxMm/cyMm/straightness/pointCount</div>
  </>
}

function CloudLoaderParams({ params, onChange, toolType }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void; toolType?: string }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-section">포인트클라우드 로드</div>
    <PathField label="파일 (ply/xyz/asc/pcd/bin)" value={(params.path as string) ?? ''}
      onChange={v => set('path', v)} toolType={toolType} />
    <CheckField label="X/Y 반전 로드 (Keyence)" value={(params.swapXY as boolean) ?? false}
      onChange={v => set('swapXY', v)}
      tooltip="켜면 로드한 점의 X/Y를 맞바꿈. SmartRay는 x=스캔(transport)/y=레이저라인(lateral)인데 Keyence는 반대(스캔=Y, 레이저라인=X)이므로 켜서 파이프라인 축 관례에 맞춤" />
    <div className="param-empty" style={{ fontSize: 10 }}>출력: PointCloud3D. CloudSaver 포맷과 대칭.</div>
  </>
}

function CloudToProfilesParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const reduce = (params.reduce as string) ?? 'none'
  return <>
    <div className="param-section">행별 Profile 추출</div>
    <SelectField label="스캔축" value={(params.scanAxis as string) ?? 'x'} options={['x', 'y']}
      onChange={v => set('scanAxis', v)}
      tooltip="프로파일이 늘어선 방향(스캔). 이 축으로 행을 나눔. 프로파일 내부=나머지(횡)축. 예: 스캔=x → x고정 y단면" />
    <NumField label="스캔 step(mm)" step={0.05} value={(params.scanStepMm as number) ?? 0.1}
      onChange={v => set('scanStepMm', v)} tooltip="스캔축 bin(행 간격)" />
    <SelectField label="횡 축약" value={reduce} options={['none', 'max', 'min', 'mean']}
      onChange={v => set('reduce', v)}
      tooltip="none: 횡 위치당 다중 Z 전부 보존 · max/min/mean: 횡축 bin으로 대표값 1개(정규 1D)" />
    {reduce !== 'none' && (
      <NumField label="횡 step(mm)" step={0.05} value={(params.latStepMm as number) ?? 0.1}
        onChange={v => set('latStepMm', v)} tooltip="횡(lateral)축 bin 크기 (축약 모드에서만)" />
    )}
    <NumField label="최소 점수" step={1} value={(params.minPoints as number) ?? 1}
      onChange={v => set('minPoints', v)} tooltip="이보다 점 적은 행은 스킵" />
    <div className="param-empty" style={{ fontSize: 10 }}>출력: Profile[] (label row:N). 차트=횡축 vs Z. ProfileFeature로 행별 분석.</div>
  </>
}

function NotchMeasureParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const method = (params.method as string) ?? 'flat'
  return <>
    <div className="param-section">프로파일 머지</div>
    <NumField label="avgProfiles" step={1} value={(params.avgProfiles as number) ?? 1}
      onChange={v => set('avgProfiles', v)} tooltip="측정 전 합칠 연속 스캔 프로파일 수 (1=개별 측정, N=N개 머지 후 1회 측정)" />
    <SelectField label="머지 방식" value={(params.avgMethod as string) ?? 'mean'} options={['mean', 'median']}
      onChange={v => set('avgMethod', v)}
      tooltip="N개 프로파일을 열(column)별로 합치는 방식" />
    <div className="param-section">바닥 검출</div>
    <SelectField label="바닥 방식" value={method} options={['flat', 'corner']}
      onChange={v => set('method', v)}
      tooltip="flat: 평탄도 최소 창 탐색 · corner: 기울기 변화점(실패 시 flat 폴백)" />
    <SelectField label="바닥 집계" value={(params.floorAgg as string) ?? 'median'} options={['median', 'mean']}
      onChange={v => set('floorAgg', v)}
      tooltip="바닥 창 안 포인트 집계 방식 (median: 이상값 강건, mean: 전체 평균)" />
    <NumField label="notchTrigUm(µm)" step={10} value={(params.notchTrigUm as number) ?? -150}
      onChange={v => set('notchTrigUm', v)} tooltip="노치 개구 트리거 임계값(음수=아래, 기본 -150µm)" />
    <NumField label="notchMinCols" step={1} value={(params.notchMinCols as number) ?? 20}
      onChange={v => set('notchMinCols', v)} tooltip="노치로 인정할 최소 연속 컬럼 수(횡 피치 단위). 너무 크면 실제 노치가 폭 부족으로 통째로 무효 처리되어 profile이 비어보임 — 그럴 땐 이 값을 낮춰보세요" />
    <NumField label="notchMaxGapUm(µm)" step={5} value={(params.notchMaxGapUm as number) ?? 50}
      onChange={v => set('notchMaxGapUm', v)} tooltip="노치 연속구간 판정 시 허용 gap(횡 방향, µm)" />
    <NumField label="landTolUm(µm)" step={5} value={(params.landTolUm as number) ?? 30}
      onChange={v => set('landTolUm', v)} tooltip="랜드 분류 허용 편차 ±µm" />
    {method === 'flat' && (
      <>
        <NumField label="floorWinUm(µm)" step={10} value={(params.floorWinUm as number) ?? 150}
          onChange={v => set('floorWinUm', v)} tooltip="바닥 탐색 창 폭 µm (flat 방식)" />
        <NumField label="floorMinPts" step={1} value={(params.floorMinPts as number) ?? 12}
          onChange={v => set('floorMinPts', v)} tooltip="바닥 탐색 창 안 최소 점 개수" />
      </>
    )}
    {method === 'corner' && (
      <NumField label="smoothCols" step={1} value={(params.smoothCols as number) ?? 3}
        onChange={v => set('smoothCols', v)} tooltip="기울기 계산 이동평균 폭(3~5, corner 방식)" />
    )}
    <div className="param-section" style={{ fontSize: 10, opacity: 0.7 }}>센서 기하</div>
    <NumField label="transportRes(mm)" step={0.0001} value={(params.transportResMm as number) ?? 0.003998}
      onChange={v => set('transportResMm', v)} tooltip="스캔방향 피치 mm" />
    <NumField label="lateralPitch(mm)" step={0.0001} value={(params.lateralPitchMm as number) ?? 0.0063}
      onChange={v => set('lateralPitchMm', v)} tooltip="횡방향 피치 mm" />
  </>
}

function NotchMeasureV2Params({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const method = (params.method as string) ?? 'flat'
  return <>
    <div className="param-section">프로파일 머지 (V1과 동일)</div>
    <NumField label="avgProfiles" step={1} value={(params.avgProfiles as number) ?? 1}
      onChange={v => set('avgProfiles', v)} tooltip="측정 전 합칠 연속 스캔 프로파일 수 (1=개별 측정, N=N개 머지 후 1회 측정)" />
    <SelectField label="머지 방식" value={(params.avgMethod as string) ?? 'mean'} options={['mean', 'median']}
      onChange={v => set('avgMethod', v)}
      tooltip="N개 프로파일을 컬럼별로 합치는 방식" />
    <div className="param-section">Land 기준선 피팅</div>
    <NumField label="landFitIters" step={1} value={(params.landFitIters as number) ?? 4}
      onChange={v => set('landFitIters', v)} tooltip="3차 다항식 강건 피팅 반복 횟수(이상치 ±3σ 제거)" />
    <div className="param-section">Notch 개구 검출</div>
    <NumField label="notchTrigUm(µm)" step={10} value={(params.notchTrigUm as number) ?? -150}
      onChange={v => set('notchTrigUm', v)} tooltip="land 기준선 대비 이 값(부호 있음)보다 낮아지면 notch 후보. 보통 음수" />
    <NumField label="notchMaxGapUm(µm)" step={5} value={(params.notchMaxGapUm as number) ?? 50}
      onChange={v => set('notchMaxGapUm', v)} tooltip="notch 연속구간 판정 시 허용 gap(y방향)" />
    <NumField label="notchMinCols" step={1} value={(params.notchMinCols as number) ?? 20}
      onChange={v => set('notchMinCols', v)} tooltip="노치로 인정할 최소 연속 컬럼 수. 너무 크면 실제 노치가 폭 부족으로 통째로 무효 처리되어 profile이 비어보임 — 그럴 땐 이 값을 낮춰보세요" />
    <div className="param-section">바닥 검출</div>
    <SelectField label="바닥 방식" value={method} options={['flat', 'corner']}
      onChange={v => set('method', v)}
      tooltip="flat: 평탄도 최소 창 탐색(전체 notch 범위) · corner: 기울기 변화점(실패 시 flat 폴백)" />
    <SelectField label="바닥 집계" value={(params.floorAgg as string) ?? 'median'} options={['median', 'mean']}
      onChange={v => set('floorAgg', v)}
      tooltip="바닥 창 안 포인트 집계 방식 (median: 이상값 강건, mean: 전체 평균)" />
    {method === 'flat' && (
      <>
        <NumField label="floorWinUm(µm)" step={10} value={(params.floorWinUm as number) ?? 150}
          onChange={v => set('floorWinUm', v)} tooltip="바닥 탐색 창 폭 µm (flat 방식)" />
        <NumField label="floorMinPts" step={1} value={(params.floorMinPts as number) ?? 12}
          onChange={v => set('floorMinPts', v)} tooltip="탐색 창 안 최소 점 개수" />
        <NumField label="floorSearchFrac" step={0.05} value={(params.floorSearchFrac as number) ?? 1.0}
          onChange={v => set('floorSearchFrac', v)} tooltip="창 중심 후보를 notch 폭의 가운데 이 비율(0~1)로 제한. 1.0=V1과 동일(전체 범위 탐색). 낮출수록 중심부만 탐색해 경계 shelf 오탐 방지" />
      </>
    )}
    {method === 'corner' && (
      <>
        <NumField label="smoothCols" step={1} value={(params.smoothCols as number) ?? 3}
          onChange={v => set('smoothCols', v)} tooltip="기울기 계산 이동평균 폭(3~5)" />
        <NumField label="slopeDrop" step={0.05} value={(params.slopeDrop as number) ?? 0.35}
          onChange={v => set('slopeDrop', v)} tooltip="기울기가 이 비율 이하로 급감하면 코너로 판정" />
        <NumField label="cornerSearchUm(µm)" step={50} value={(params.cornerSearchUm as number) ?? 500}
          onChange={v => set('cornerSearchUm', v)} tooltip="코너 탐색 범위" />
      </>
    )}
    <div className="param-section">Land 대표값</div>
    <CheckField label="평탄도 필터 사용(V1과 동일)" value={(params.landFlatFilter as boolean) ?? false}
      onChange={v => set('landFlatFilter', v)}
      tooltip="켜면 |상대높이| < landTolUm인 평탄한 점만 사용(V1과 동일, landMaxDistMm과 좁게 겹치면 점이 하나도 없어 NaN 가능). 끄면(기본) 평탄도 필터 없이 구간 내 전체 점을 Land 집계 방식으로 집계 — median과 함께 쓰면 이상치에 강건하면서도 NaN이 생기지 않음" />
    {(params.landFlatFilter as boolean) === true && (
      <NumField label="landTolUm(µm)" step={5} value={(params.landTolUm as number) ?? 30}
        onChange={v => set('landTolUm', v)} tooltip="notch 밖 영역에서 |상대높이| < 이 값(µm)인 점만 land로 사용" />
    )}
    <NumField label="landMaxDistMm" step={0.5} value={(params.landMaxDistMm as number) ?? 0}
      onChange={v => set('landMaxDistMm', v)}
      tooltip="notch 경계에서부터 이 거리(mm) 이내 점만 land로 참조. 0=제한 없음(V1과 동일, notch 밖 전체 영역 사용) — 기존 V2의 landSampleMm과 같은 취지, 노치와 먼 land 변동에 덜 흔들리게 하고 싶을 때 사용" />
    <SelectField label="Land 집계" value={(params.landAgg as string) ?? 'median'} options={['mean', 'median']}
      onChange={v => set('landAgg', v)}
      tooltip="median(기본)=이상치에 강건, 평탄도 필터 꺼도 안정적 · mean=V1과 동일(평탄도 필터 켬과 조합 권장)" />
    <NumField label="edge 탐색 기울기(µm/mm)" step={5} value={(params.edgeSlopeTolUmPerMm as number) ?? 30}
      onChange={v => set('edgeSlopeTolUmPerMm', v)}
      tooltip="노치 경계 바깥으로 나가며 기울기가 이 값(µm/mm) 미만이 되는 첫 지점을 edge land로 판정. 작을수록 엄격(완전 평탄 구간만), 클수록 느슨(경사 시작점)" />
    <NumField label="edge 윈도우(pts)" step={1} value={(params.edgeSlopeWindowPts as number) ?? 5}
      onChange={v => set('edgeSlopeWindowPts', Math.max(1, Math.round(v)))}
      tooltip="기울기 산출 시 몇 점 간격으로 볼지. 1=인접 2점(노이즈 민감), 클수록 더 넓은 구간 평균 기울기(노이즈 억제). 권장 5~20" />
    <div className="param-section">안정화 (이웃 chunk 이동중앙값)</div>
    <NumField label="안정화 반경(chunk)" step={5} value={(params.floorStabilizeHalf as number) ?? 25}
      onChange={v => set('floorStabilizeHalf', v)} tooltip="바닥값 이동중앙값 반경(chunk 개수). 0이면 안정화 비활성. 무효 chunk도 이 반경 안 이웃으로 채움" />
    <NumField label="안정화 허용치-위치(µm)" step={5} value={(params.floorStabilizeCenterTolUm as number) ?? 50}
      onChange={v => set('floorStabilizeCenterTolUm', v)} tooltip="바닥 중심위치가 이웃 이동중앙값과 이 값(µm) 넘게 차이나면 스냅" />
    <NumField label="안정화 허용치-높이(µm)" step={5} value={(params.floorStabilizeZTolUm as number) ?? 60}
      onChange={v => set('floorStabilizeZTolUm', v)} tooltip="바닥 상대높이가 이웃 이동중앙값과 이 값(µm) 넘게 차이나면 스냅" />
    <div className="param-section">출력 PointCloud3D (land/floor 필터링)</div>
    <NumField label="floorTolUm(µm)" step={5} value={(params.floorTolUm as number) ?? 40}
      onChange={v => set('floorTolUm', v)} tooltip="바닥 점 판정 허용치: |상대높이 - floorZRel| < 이 값(µm)인 점만 출력 클라우드에 포함" />
    <NumField label="landMarginMm" step={0.005} value={(params.landMarginMm as number) ?? 0.020}
      onChange={v => set('landMarginMm', v)} tooltip="notch 안팎 판정 마진(mm) — notch 경계 바로 바깥까지는 land 판정에서 제외" />
    <div className="param-section" style={{ fontSize: 10, opacity: 0.7 }}>센서 기하</div>
    <NumField label="Lateral Res(mm)" step={0.0001} value={(params.lateralResMm as number) ?? 0.0063}
      onChange={v => set('lateralResMm', v)} tooltip="y(lateral) 컬럼 binning 간격" />
    <NumField label="Transport Res(mm)" step={0.0001} value={(params.transportResMm as number) ?? 0.008}
      onChange={v => set('transportResMm', v)} tooltip="x(transport) profile 그룹핑 간격" />
    <div className="param-empty" style={{ fontSize: 10 }}>출력: Profile[] (depth_left/right/combined_um, depth_left/right_edge_um, notch_floor_z/land_left_z/land_right_z_mm) + valid_count + land/floor로 분류된 PointCloud3D</div>
  </>
}

function ReduceDomainParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-section">처리 범위 제한</div>
    <CheckField label="제외 모드(invert)" value={(params.invert as boolean) ?? false}
      onChange={v => set('invert', v)}
      tooltip="off=Region 안쪽만 유지(포함) · on=Region 안쪽을 NaN 처리(검사 제외 마스크)" />
    <div className="param-empty" style={{ fontSize: 10 }}>
      포트0=HeightMap · 포트1=Region. NaN은 하류 측정툴이 자동 제외.
      제외영역 여러 개면 ReduceDomain(invert) 여러 번 체인.
    </div>
  </>
}

function RegionMeasureParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const ag = (params.aggregation as string) ?? 'Mean'
  return <>
    <div className="param-section">Z 집계 방식</div>
    <SelectField label="aggregation" value={ag}
      options={['Mean', 'Median', 'Max', 'Min', 'StdDev', 'HighTail', 'Percentile']}
      onChange={v => set('aggregation', v)}
      tooltip="Region 내 유효 Z값 집계. Mean/Median/Max/Min/StdDev · HighTail=상위 %평균 · Percentile=백분위" />
    {ag === 'HighTail' && (
      <NumField label="highTail %" step={1} value={(params.highTailPct as number) ?? 20}
        onChange={v => set('highTailPct', v)} tooltip="상위 몇 % 평균을 zMm으로" />
    )}
    {ag === 'Percentile' && (
      <NumField label="percentile" step={1} value={(params.percentile as number) ?? 50}
        onChange={v => set('percentile', v)} tooltip="백분위(0~100). 50=중앙값" />
    )}
    <div className="param-empty" style={{ fontSize: 10 }}>
      포트0=Region[](필수) · 포트1=HeightMap(선택, 없으면 px 측정만).
      출력: area·cxMm·cyMm·bbox·orient·zMm·volume·flatness
    </div>
  </>
}

function ValidRegionParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-section">유효 픽셀 필터</div>
    <NumField label="Channel" value={(params.channel as number) ?? 0} step={1} onChange={v => set('channel', v)}
      tooltip="검사할 채널 인덱스. 0=기본 채널" />
    <CheckField label="Invert" value={(params.invert as boolean) ?? false} onChange={v => set('invert', v)}
      tooltip="true=NaN 픽셀을 유효로 반전 (유효 픽셀을 마스크 아웃)" />
  </>
}

function LevelNodeParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const mode = (params.mode as string) ?? 'distance'
  return <>
    <div className="param-section">평탄화</div>
    <SelectField label="Mode" value={mode} options={['distance', 'flatten']} onChange={v => set('mode', v)}
      tooltip="distance=Plane까지 부호 있는 수직 거리(mm), flatten=수직 투영(z 성분만 제거)" />
    <CheckField label="Keep Invalid" value={(params.keepInvalid as boolean) ?? true} onChange={v => set('keepInvalid', v)}
      tooltip="true=입력 NaN 픽셀을 NaN으로 유지. false=0으로 채움" />
    <NumField label="Offset (mm)" value={(params.offsetMm as number) ?? 0} step={0.01} onChange={v => set('offsetMm', v)}
      tooltip="출력 전체에 더할 오프셋(mm). 기준면 위로 올리거나 내릴 때 사용" />
  </>
}

function SurfaceCropNodeParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const mode = (params.mode as string) ?? 'rect'
  const rect = (params.rect as { x: number; y: number; w: number; h: number }) ?? { x: 0, y: 0, w: 0, h: 0 }
  return <>
    <div className="param-section">잘라내기</div>
    <SelectField label="Mode" value={mode} options={['rect', 'region']} onChange={v => set('mode', v)}
      tooltip="rect=사각형 픽셀 좌표 크롭, region=입력 Region 경계 박스 크롭" />
    {mode === 'rect' && (
      <RoiField label="Rect (px)" value={rect} onChange={v => set('rect', v)}
        tooltip="잘라낼 픽셀 좌표 (x=좌, y=상, w=폭, h=높이). w=0이면 끝까지" />
    )}
    <CheckField label="Outside NaN" value={(params.outsideNaN as boolean) ?? true} onChange={v => set('outsideNaN', v)}
      tooltip="region 모드: 영역 밖 픽셀을 NaN으로 채움. false=원본값 유지" />
  </>
}

function SurfaceResampleNodeParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const mode = (params.mode as string) ?? 'factor'
  return <>
    <div className="param-section">다운샘플</div>
    <SelectField label="Mode" value={mode} options={['factor', 'resolution']} onChange={v => set('mode', v)}
      tooltip="factor=배율로 축소, resolution=목표 분해능(mm)으로 배율 자동 계산" />
    {mode === 'factor' ? (
      <NumField label="Factor" value={(params.factor as number) ?? 2} step={1} onChange={v => set('factor', v)}
        tooltip="축소 배율. 2=가로세로 각 2배 축소(1/4 픽셀)" />
    ) : <>
      <NumField label="Target X Res (mm)" value={(params.targetXResMm as number) ?? 0} step={0.001} onChange={v => set('targetXResMm', v)}
        tooltip="목표 X 분해능(mm). 현재 xResMm보다 커야 함" />
      <NumField label="Target Y Res (mm)" value={(params.targetYResMm as number) ?? 0} step={0.001} onChange={v => set('targetYResMm', v)}
        tooltip="목표 Y 분해능(mm). 현재 yResMm보다 커야 함" />
    </>}
    <SelectField label="Method" value={(params.method as string) ?? 'decimate'} options={['decimate', 'meanValid']} onChange={v => set('method', v)}
      tooltip="decimate=stride 샘플(빠름·NaN 포함), meanValid=블록 유효값 평균(NaN 제외)" />
    <div className="param-empty" style={{ fontSize: 10 }}>측정 경로 비권장 — 프리뷰·정렬 전용</div>
  </>
}

export default function ParamPanel({ nodeId, toolType, label, params, onParamChange, onClose, embedded }: Props) {
  const def = TOOL_DEF_MAP[toolType]
  if (!def) return null

  const handleChange = (p: Record<string, unknown>) => onParamChange(nodeId, p)

  if (embedded) {
    return (
      <div className="param-panel-body">
        {toolType === 'PlaneFit'         && <PlaneFitParams   params={params} onChange={handleChange} />}
        {toolType === 'NoiseFilter'      && <NoiseFilterParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapLoader' && <HeightMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge' && <ExposureMergeParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureSplit' && <ExposureSplitParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge3' && <ExposureMerge3Params params={params} onChange={handleChange} />}
        {toolType === 'ExposureFilter' && <ExposureFilterParams params={params} onChange={handleChange} />}
        {toolType === 'GapFill' && <GapFillParams params={params} onChange={handleChange} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
        {toolType === 'ImageSaver'       && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapSaver'   && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapToCloud'      && <HeightMapToCloudParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMergeCloud' && <ExposureMergeCloudParams params={params} onChange={handleChange} />}
        {toolType === 'CloudSelect'      && <CloudSelectParams params={params} onChange={handleChange} />}
        {toolType === 'PointCloudSOR'   && <PointCloudSORParams params={params} onChange={handleChange} />}
        {toolType === 'CloudToHeightMap' && <CloudToHeightMapParams params={params} onChange={handleChange} />}
        {toolType === 'CloudSaver'       && <CloudSaverParams params={params} onChange={handleChange} />}
        {toolType === 'PointCloudSplit'  && <PointCloudSplitParams params={params} onChange={handleChange} />}
        {toolType === 'CloudZReduce'     && <CloudZReduceParams params={params} onChange={handleChange} />}
        {toolType === 'Align'            && <div className="param-empty">입력: HeightMap + Point (기준점). 파라미터 없음</div>}
        {toolType === 'ValidRegion'      && <ValidRegionParams params={params} onChange={handleChange} />}
        {toolType === 'Level'            && <LevelNodeParams params={params} onChange={handleChange} />}
        {toolType === 'SurfaceCrop'      && <SurfaceCropNodeParams params={params} onChange={handleChange} />}
        {toolType === 'SurfaceResample'  && <SurfaceResampleNodeParams params={params} onChange={handleChange} />}
        {toolType === 'LineFit'          && <LineFitParams params={params} onChange={handleChange} />}
        {toolType === 'RegionMeasure'    && <RegionMeasureParams params={params} onChange={handleChange} />}
        {toolType === 'ReduceDomain'     && <ReduceDomainParams params={params} onChange={handleChange} />}
        {toolType === 'CloudLoader'      && <CloudLoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CloudToProfiles'  && <CloudToProfilesParams params={params} onChange={handleChange} />}
        {toolType === 'ProfileToCloud'   && <ProfileToCloudParams params={params} onChange={handleChange} />}
        {toolType === 'NotchMeasure'     && <NotchMeasureParams params={params} onChange={handleChange} />}
        {toolType === 'NotchMeasureV2'   && <NotchMeasureV2Params params={params} onChange={handleChange} />}
      </div>
    )
  }

  return (
    <div className="param-panel">
      <div className="param-panel-header">
        <span>{label}</span>
        <button className="param-close" onClick={onClose}>✕</button>
      </div>
      <div className="param-panel-body">
        {toolType === 'PlaneFit'         && <PlaneFitParams   params={params} onChange={handleChange} />}
        {toolType === 'NoiseFilter'      && <NoiseFilterParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapLoader' && <HeightMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge' && <ExposureMergeParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureSplit' && <ExposureSplitParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge3' && <ExposureMerge3Params params={params} onChange={handleChange} />}
        {toolType === 'ExposureFilter' && <ExposureFilterParams params={params} onChange={handleChange} />}
        {toolType === 'GapFill' && <GapFillParams params={params} onChange={handleChange} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
        {toolType === 'ImageSaver'       && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapSaver'   && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapToCloud'      && <HeightMapToCloudParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMergeCloud' && <ExposureMergeCloudParams params={params} onChange={handleChange} />}
        {toolType === 'CloudSelect'      && <CloudSelectParams params={params} onChange={handleChange} />}
        {toolType === 'PointCloudSOR'   && <PointCloudSORParams params={params} onChange={handleChange} />}
        {toolType === 'CloudToHeightMap' && <CloudToHeightMapParams params={params} onChange={handleChange} />}
        {toolType === 'CloudSaver'       && <CloudSaverParams params={params} onChange={handleChange} />}
        {toolType === 'PointCloudSplit'  && <PointCloudSplitParams params={params} onChange={handleChange} />}
        {toolType === 'CloudZReduce'     && <CloudZReduceParams params={params} onChange={handleChange} />}
        {toolType === 'Align'            && <div className="param-empty">입력: HeightMap + Point (기준점). 파라미터 없음</div>}
        {toolType === 'ValidRegion'      && <ValidRegionParams params={params} onChange={handleChange} />}
        {toolType === 'Level'            && <LevelNodeParams params={params} onChange={handleChange} />}
        {toolType === 'SurfaceCrop'      && <SurfaceCropNodeParams params={params} onChange={handleChange} />}
        {toolType === 'SurfaceResample'  && <SurfaceResampleNodeParams params={params} onChange={handleChange} />}
        {toolType === 'LineFit'          && <LineFitParams params={params} onChange={handleChange} />}
        {toolType === 'RegionMeasure'    && <RegionMeasureParams params={params} onChange={handleChange} />}
        {toolType === 'ReduceDomain'     && <ReduceDomainParams params={params} onChange={handleChange} />}
        {toolType === 'CloudLoader'      && <CloudLoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CloudToProfiles'  && <CloudToProfilesParams params={params} onChange={handleChange} />}
        {toolType === 'ProfileToCloud'   && <ProfileToCloudParams params={params} onChange={handleChange} />}
        {toolType === 'NotchMeasure'     && <NotchMeasureParams params={params} onChange={handleChange} />}
        {toolType === 'NotchMeasureV2'   && <NotchMeasureV2Params params={params} onChange={handleChange} />}
      </div>
    </div>
  )
}
