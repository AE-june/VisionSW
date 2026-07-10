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
    const filters = (toolType === 'ZMapLoader' || toolType === 'ExposureMerge')
      ? [{ name: 'ZMap (PNG)', extensions: ['png'] }, { name: 'All Files', extensions: ['*'] }]
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
      tooltip="기준 평면을 피팅할 영역. ZMap 전체 크기 대비 비율(0~1). 기준 표면(유리 등)이 있는 영역을 설정" />
    <div className="param-section">측정 ROI</div>
    <RoiField label="Measure"   value={roiM}   onChange={v => set('roiMeasure', v)}
      tooltip="평면 기준으로 높이를 측정할 영역. ZMap 비율(0~1). 기준면 ROI와 겹쳐도 됨" />
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
    <div className="param-section">ZMap 필터</div>
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

// ZMapLoader: 단일 파일 / 폴더(연속검사) 모드 — 이미지 소스를 노드가 단독 소유
function ZMapLoaderParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
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
      onChange({ ...params, folder: dir, index: 0 })
      window.electronAPI?.enginePreload?.(
        dir,
        params.xResMm as number ?? 1.0,
        params.yResMm as number ?? 1.0,
        params.zResMm as number ?? 0.001,
      )
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
      <PathField label="파일" value={params.path as string ?? ''} onChange={v => set('path', v)} toolType="ZMapLoader" />
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
            <button className="btn-browse" disabled={clampIdx <= 0} onClick={() => gotoIndex(clampIdx - 1)}>◀</button>
            <span className="zl-idx-cur" title={cur?.name}>{clampIdx + 1}/{files.length}</span>
            <button className="btn-browse" disabled={clampIdx >= files.length - 1} onClick={() => gotoIndex(clampIdx + 1)}>▶</button>
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

// ExposureMerge: 인터리브 ZMap 입력(ZMapLoader 연결) → BFS 리플렉션 제거 + 머지
function ExposureMergeParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-empty" style={{ fontSize: 10 }}>ZMapLoader에서 인터리브 Z PNG를 연결하세요. I/L 파일은 같은 폴더에서 자동으로 읽습니다.</div>
    <div className="param-section">BFS 리플렉션 필터 (장노출)</div>
    <CheckField label="BFS 필터 사용" value={params.enableBfs as boolean ?? true} onChange={v => set('enableBfs', v)}
      tooltip="장노출 이미지에서 BFS 플러드필로 리플렉션 영역을 제거. ROI 내에서 씨앗(저/장 일치 픽셀)에서 출발해 연결된 표면만 유지" />
    {(params.enableBfs as boolean ?? true) && <>
      <NumField label="씨앗 허용치" value={params.seedTol as number ?? 100} step={10} onChange={v => set('seedTol', v)}
        tooltip="저노출·장노출 두 값이 모두 유효하고 |차이| ≤ 이 값인 픽셀을 BFS 시작점(씨앗)으로 사용. 단위: raw count (0~65535)" />
      <NumField label="X 허용치 (cnt/px)" value={params.tolX as number ?? 10} step={1} onChange={v => set('tolX', v)}
        tooltip="BFS 확장 시 X방향 이웃과의 Z 허용 차이. X 피치(~6µm)가 작으므로 작게 설정. 단위: count/px" />
      <NumField label="Y 허용치 (cnt/px)" value={params.tolY as number ?? 100} step={10} onChange={v => set('tolY', v)}
        tooltip="BFS 확장 시 Y방향 이웃과의 Z 허용 차이. Y 피치가 X의 ~15배이므로 크게 설정. 단위: count/px" />
      <NumField label="갭 점프 (px)" value={params.gapK as number ?? 2} step={1} onChange={v => set('gapK', v)}
        tooltip="BFS가 NaN 픽셀을 건너뛸 최대 거리. 허용치는 건너뛴 거리에 비례해 증가" />
      <div className="param-empty" style={{ fontSize: 10 }}>ROI는 결과창에서 드래그로 설정합니다.</div>
    </>}
    <div className="param-section">SOR 필터 (저노출 대입 후)</div>
    <CheckField label="SOR 필터 사용" value={params.enableSor as boolean ?? true} onChange={v => set('enableSor', v)}
      tooltip="머지 결과에서 주변 이웃 통계를 벗어난 고립된 이상값 픽셀을 제거" />
    {(params.enableSor as boolean ?? true) && <>
      <NumField label="SOR 커널" value={params.sorKernel as number ?? 5} step={2} onChange={v => set('sorKernel', v)}
        tooltip="통계를 계산할 창 크기(홀수). 클수록 더 넓은 영역 참조. 0이면 비활성" />
      <NumField label="SOR Std 비율" value={params.sorRatio as number ?? 2.0} step={0.1} onChange={v => set('sorRatio', v)}
        tooltip="평균 ± (비율 × 표준편차) 범위 밖이면 이상값으로 제거. 작을수록 더 공격적" />
    </>}
    <div className="param-section">출력 단계</div>
    <div className="param-row">
      <label className="param-label">저장 출력</label>
      <select
        className="param-select"
        value={params.outputStage as number ?? 4}
        onChange={e => set('outputStage', Number(e.target.value))}
      >
        <option value={0}>1. 저노출</option>
        <option value={1}>2. 장노출</option>
        <option value={2}>3. 장노출 리플렉션 제거</option>
        <option value={3}>4. 저노출 대입 장노출</option>
        <option value={4}>5. SOR 적용</option>
      </select>
    </div>
    <div className="param-empty" style={{ fontSize: 10 }}>
      결과창 드롭다운에서 모든 단계를 미리볼 수 있습니다.
    </div>
  </>
}

// ImageSaver: 입력(ZMap/이미지)을 파일로 저장
function ImageSaverParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const path = params.path as string ?? ''
  const handleSave = async () => {
    const api = (window as Window & { electronAPI?: { saveFile: (f?: Electron.FileFilter[]) => Promise<string | null> } }).electronAPI
    if (!api?.saveFile) return
    const p = await api.saveFile([
      { name: 'PNG (16bit)', extensions: ['png'] },
      { name: 'TIFF (16bit)', extensions: ['tif', 'tiff'] },
      { name: 'Image (8bit)', extensions: ['jpg', 'jpeg', 'bmp'] },
      { name: 'All Files', extensions: ['*'] },
    ])
    if (p) onChange({ ...params, path: p })
  }
  return <>
    <div className="param-section">저장 파일</div>
    <div className="param-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 4 }}>
      <span className="param-label">저장 위치</span>
      <button className="btn-browse" onClick={handleSave}>📁 파일 선택</button>
      {path && <div className="param-path-display" title={path}>{path}</div>}
    </div>
    <div className="param-empty" style={{ fontSize: 10 }}>
      ZMap→ PNG/TIFF는 16비트, 그 외 포맷은 8비트(정규화)로 저장. 실행할 때마다 저장됩니다.
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
        {toolType === 'ZMapLoader' && <ZMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge' && <ExposureMergeParams params={params} onChange={handleChange} />}
        {toolType === 'ImageLoader' && <LoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
        {toolType === 'ImageSaver'       && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'Align'            && <div className="param-empty">입력: ZMap + Point (기준점). 파라미터 없음</div>}
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
        {toolType === 'ZMapLoader' && <ZMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ExposureMerge' && <ExposureMergeParams params={params} onChange={handleChange} />}
        {toolType === 'ImageLoader' && <LoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
        {toolType === 'ImageSaver'       && <ImageSaverParams params={params} onChange={handleChange} />}
        {toolType === 'Align'            && <div className="param-empty">입력: ZMap + Point (기준점). 파라미터 없음</div>}
        {toolType === 'EdgeDetector'     && <div className="param-empty">파라미터 없음</div>}
      </div>
    </div>
  )
}
