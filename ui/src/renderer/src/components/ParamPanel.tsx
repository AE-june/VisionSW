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

function NumField({ label, value, onChange, step = 1, tooltip }: {
  label: string; value: number; onChange: (v: number) => void; step?: number; tooltip?: string
}) {
  return (
    <div className="param-row">
      <span className="param-label">{label}<Tip text={tooltip} /></span>
      <input className="param-input" type="number" value={value} step={step}
        onChange={e => onChange(parseFloat(e.target.value) || 0)} />
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

const ROI_TIPS: Record<string, string> = {
  xMin: '측정 ROI의 X 시작 경계 (mm, 물리 좌표)',
  xMax: '측정 ROI의 X 끝 경계 (mm, 물리 좌표)',
  yMin: '측정 ROI의 Y 시작 경계 (mm, 물리 좌표)',
  yMax: '측정 ROI의 Y 끝 경계 (mm, 물리 좌표)',
}

function ThicknessParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const roi = params.roi as { xMin: number; xMax: number; yMin: number; yMax: number }

  return <>
    <div className="param-section">ROI (mm)</div>
    {(['xMin','xMax','yMin','yMax'] as const).map(f => (
      <NumField key={f} label={f} value={roi[f]} step={0.1} tooltip={ROI_TIPS[f]} onChange={v => set('roi', { ...roi, [f]: v })} />
    ))}
    <div className="param-section">측정</div>
    <NumField label="Nominal (mm)"   value={params.nominalMm as number}   step={0.01} tooltip="목표 두께(mm). 측정값과의 차이가 Tolerance를 초과하면 NG" onChange={v => set('nominalMm', v)} />
    <NumField label="Tolerance (mm)" value={params.toleranceMm as number} step={0.01} tooltip="허용 오차(mm). |측정값 - Nominal| > 이 값이면 NG 판정" onChange={v => set('toleranceMm', v)} />
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
    <div className="param-empty" style={{ fontSize: 10 }}>실행할 때마다 한 행씩 추가됩니다</div>
  </>
}

function LoaderParams({ params, onChange, toolType }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void; toolType: string }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <PathField label="파일" value={params.path as string ?? ''} onChange={v => set('path', v)} toolType={toolType} />
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

// ExposureSplit (다중노출 분리): 인터리브 → 노출별 행 분리 (행확장 없음)
function ExposureMergeParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const splitCount = (params.splitCount as number) ?? 2
  const labels = splitCount === 3 ? ['1. 저노출', '2. 중노출', '3. 장노출'] : ['1. 저노출', '2. 장노출']
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>HeightMapLoader에서 인터리브 Z PNG를 연결하세요. 인터리브된 노출을 행별로 분리합니다(행확장 없음). 머지는 Exposure Merge 노드가 담당합니다.</div>
    <div className="param-section">분리</div>
    <div className="param-row">
      <label className="param-label">분할 수</label>
      <select
        className="param-select"
        value={splitCount}
        onChange={e => {
          const sc = Number(e.target.value)
          const os = Math.min((params.outputStage as number ?? 0), sc - 1)
          onChange({ ...params, splitCount: sc, outputStage: os })
        }}
      >
        <option value={2}>2행 (저/장)</option>
        <option value={3}>3행 (저/중/장)</option>
      </select>
    </div>
    <div className="param-section">출력</div>
    <div className="param-row">
      <label className="param-label">저장 출력</label>
      <select
        className="param-select"
        value={Math.min((params.outputStage as number ?? 0), splitCount - 1)}
        onChange={e => set('outputStage', Number(e.target.value))}
      >
        {labels.map((l, i) => <option key={i} value={i}>{l}</option>)}
      </select>
    </div>
    <div className="param-empty" style={{ fontSize: 10 }}>
      결과창 드롭다운에서 각 노출을 미리볼 수 있습니다.
    </div>
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

// ExposureMerge2 (이중노출 머지 재구현): 오프셋 보정 + 연속성 필터로 fill 리플렉션 제거
function ExposureMerge2Params({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>HeightMapLoader에서 인터리브 Z PNG를 연결하세요. 겹침은 저노출 우선, fill 리플렉션은 연속성으로 제거합니다.</div>
    <div className="param-section">머지 / 연속성 필터</div>
    <NumField label="일치 허용 (cnt)" value={params.matchTol as number ?? 20} step={5} onChange={v => set('matchTol', v)}
      tooltip="저·장노출 두 값이 모두 유효하고 |차이| ≤ 이 값이면 '겹침 일치'로 판정. 오프셋 추정에 사용(리플 씨앗 허용 미설정 시 씨앗에도 사용). 단위: raw count" />
    <NumField label="리플 씨앗 허용 (cnt)"
      value={(params.reflTol as number ?? -1) >= 0 ? (params.reflTol as number) : (params.matchTol as number ?? 20)}
      step={5} onChange={v => set('reflTol', v)}
      tooltip="연속성 씨앗 판정 허용치. 기본은 '일치 허용'과 동일값을 사용(-1). 따로 키우면 씨앗↑→고노출 fill 더 유지(덜 제거), 줄이면 더 제거. 오프셋(일치 허용)과 분리 조절용. 단위: raw count" />
    <NumField label="X 허용치 (cnt/px)" value={params.tolX as number ?? 10} step={1} onChange={v => set('tolX', v)}
      tooltip="연속성 확장 시 X방향 이웃과의 Z 허용 차이. X 피치(~6µm)가 작으므로 작게. 단위: count/px" />
    <NumField label="Y 허용치 (cnt/px)" value={params.tolY as number ?? 100} step={10} onChange={v => set('tolY', v)}
      tooltip="연속성 확장 시 Y방향 이웃과의 Z 허용 차이. Y 피치가 X의 ~15배라 크게. 단위: count/px" />
    <NumField label="갭 점프 (px)" value={params.gapK as number ?? 2} step={1} onChange={v => set('gapK', v)}
      tooltip="연속성이 NaN(구멍) 픽셀을 건너뛸 최대 거리. 허용치는 건너뛴 거리에 비례해 증가" />
    <CheckField label="반해상도 출력 (Y×2)" value={params.halfRes as boolean ?? true} onChange={v => set('halfRes', v)}
      tooltip="머지는 홀짝 절반 그리드라 켜면 n행·Y피치×2로 출력. 끄면 각 행을 2배 복제해 원본 높이 유지" />
    <div className="param-section">청크 연산 (실시간 스트리밍)</div>
    <CheckField label="청크 단위 연산" value={params.chunkMode as boolean ?? false} onChange={v => set('chunkMode', v)}
      tooltip="끄면 전체 이미지를 한 번에 연산(기본). 켜면 입력을 청크로 나눠 겹침 포함 처리 — 프로파일이 스트리밍으로 들어오는 실시간 검사용" />
    {(params.chunkMode as boolean) && <>
      <NumField label="청크 행 수" value={params.chunkRows as number ?? 1000} step={100} onChange={v => set('chunkRows', v)}
        tooltip="청크 하나에 담을 입력 프로파일(행) 수. 클수록 겹침 재연산 비율↓(예 1000+겹침320이면 낭비 1.64배). 실시간이면 여러 콜백을 모아 이 크기로 처리" />
      <NumField label="겹침 행 수" value={params.overlapRows as number ?? 320} step={20} onChange={v => set('overlapRows', v)}
        tooltip="청크 위·아래로 확장해 함께 연산하는 행 수. 연속성(BFS)이 청크 경계를 넘어 이어지도록 하는 마진 — 코어 출력만 남기고 겹침은 버림. 리플렉션 최장 streak를 덮어야 전체모드와 동일. SDC 100장 검증상 ≥320이면 완전 일치. 클수록 이음매 결함↓·연산량↑ (청크행 대비 겹침이 크면 재연산 비율↑ → 청크행을 키워 상쇄)" />
    </>}
    <div className="param-empty" style={{ fontSize: 10 }}>출력은 항상 최종 머지(리플렉션 제거) 이미지. 중간 단계는 결과창 드롭다운(디스플레이 전용)에서만 확인되며 검사 시간엔 반영되지 않습니다. (② I/LLT 게이팅은 추후)</div>
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
    <div className="param-empty" style={{ fontSize: 10 }}>중간 단계(저·중 머지 / 저·중·장 원본)는 결과창 드롭다운(디스플레이 전용, 비청크)에서만 확인됩니다.</div>
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

// CloudSaver: PointCloud3D → PLY/XYZ 파일 저장
function CloudSaverParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const folder = params.folder as string ?? ''
  const filename = params.filename as string ?? ''
  const format = params.format as string ?? 'ply'
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
    <div className="param-empty" style={{ fontSize: 10 }}>
      저장 형식: 폴더 / HHMMSSmmm_(파일명 또는 소스명).포맷
    </div>
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
        {toolType === 'ThicknessMeasure' && <ThicknessParams  params={params} onChange={handleChange} />}
        {toolType === 'NoiseFilter'      && <NoiseFilterParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapLoader' && <HeightMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge' && <ExposureMergeParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge2' && <ExposureMerge2Params params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge3' && <ExposureMerge3Params params={params} onChange={handleChange} />}
        {toolType === 'GapFill' && <GapFillParams params={params} onChange={handleChange} />}
        {toolType === 'ImageLoader' && <LoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
        {toolType === 'ImageSaver'       && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapToCloud'      && <HeightMapToCloudParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMergeCloud' && <ExposureMergeCloudParams params={params} onChange={handleChange} />}
        {toolType === 'CloudSaver'       && <CloudSaverParams params={params} onChange={handleChange} />}
        {toolType === 'Align'            && <div className="param-empty">입력: HeightMap + Point (기준점). 파라미터 없음</div>}
        {toolType === 'EdgeDetector'     && <div className="param-empty">파라미터 없음</div>}
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
        {toolType === 'ThicknessMeasure' && <ThicknessParams  params={params} onChange={handleChange} />}
        {toolType === 'NoiseFilter'      && <NoiseFilterParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapLoader' && <HeightMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge' && <ExposureMergeParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge2' && <ExposureMerge2Params params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge3' && <ExposureMerge3Params params={params} onChange={handleChange} />}
        {toolType === 'GapFill' && <GapFillParams params={params} onChange={handleChange} />}
        {toolType === 'ImageLoader' && <LoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
        {toolType === 'ImageSaver'       && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'HeightMapToCloud'      && <HeightMapToCloudParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMergeCloud' && <ExposureMergeCloudParams params={params} onChange={handleChange} />}
        {toolType === 'CloudSaver'       && <CloudSaverParams params={params} onChange={handleChange} />}
        {toolType === 'Align'            && <div className="param-empty">입력: HeightMap + Point (기준점). 파라미터 없음</div>}
        {toolType === 'EdgeDetector'     && <div className="param-empty">파라미터 없음</div>}
      </div>
    </div>
  )
}
