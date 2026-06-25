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

function RoiField({ label, value, onChange }: {
  label: string
  value: { x: number; y: number; w: number; h: number }
  onChange: (v: typeof value) => void
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
      <span className="param-label">{label}</span>
      <div className="param-roi">{f('x')}{f('y')}{f('w')}{f('h')}</div>
    </div>
  )
}

function NumField({ label, value, onChange, step = 1 }: {
  label: string; value: number; onChange: (v: number) => void; step?: number
}) {
  return (
    <div className="param-row">
      <span className="param-label">{label}</span>
      <input className="param-input" type="number" value={value} step={step}
        onChange={e => onChange(parseFloat(e.target.value) || 0)} />
    </div>
  )
}

function SelectField({ label, value, options, onChange }: {
  label: string; value: string; options: string[]; onChange: (v: string) => void
}) {
  return (
    <div className="param-row">
      <span className="param-label">{label}</span>
      <select className="param-select" value={value} onChange={e => onChange(e.target.value)}>
        {options.map(o => <option key={o}>{o}</option>)}
      </select>
    </div>
  )
}

function CheckField({ label, value, onChange }: {
  label: string; value: boolean; onChange: (v: boolean) => void
}) {
  return (
    <div className="param-row">
      <span className="param-label">{label}</span>
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
    const filters = toolType === 'ZMapLoader'
      ? [{ name: 'ZMap (PNG)', extensions: ['png'] }, { name: 'All Files', extensions: ['*'] }]
      : [{ name: 'Image', extensions: ['png', 'jpg', 'jpeg', 'bmp', 'tiff'] }, { name: 'All Files', extensions: ['*'] }]
    const path = await api.openFile(filters)
    if (path) onChange(path)
  }

  const filename = value ? value.split(/[\\/]/).pop() : ''

  return (
    <div className="param-row" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 4 }}>
      <span className="param-label">{label}</span>
      <div style={{ display: 'flex', gap: 6 }}>
        <button className="btn-browse" onClick={handleBrowse}>📂 찾아보기</button>
      </div>
      {filename && <div className="param-path-display" title={value}>{filename}</div>}
    </div>
  )
}

function LineFitParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const roi1 = params.roiFit1 as { x: number; y: number; w: number; h: number }
  const roi2 = params.roiFit2 as typeof roi1
  const roiM = params.roiMeasure as typeof roi1

  return <>
    <div className="param-section">ROI</div>
    <RoiField label="Fit 1"    value={roi1} onChange={v => set('roiFit1', v)} />
    <RoiField label="Fit 2"    value={roi2} onChange={v => set('roiFit2', v)} />
    <RoiField label="Measure"  value={roiM} onChange={v => set('roiMeasure', v)} />
    <div className="param-section">집계</div>
    <SelectField label="Mode" value={params.aggregation as string}
      options={['Max', 'Mean', 'HighTail']} onChange={v => set('aggregation', v)} />
    <div className="param-section">RANSAC</div>
    <CheckField label="사용" value={params.useRansac as boolean} onChange={v => set('useRansac', v)} />
    {params.useRansac && <>
      <NumField label="반복 횟수" value={params.ransacIterations as number} onChange={v => set('ransacIterations', v)} />
      <NumField label="Threshold (mm)" value={params.ransacThreshold as number} step={0.01} onChange={v => set('ransacThreshold', v)} />
    </>}
  </>
}

function PlaneFitParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const roiRef = params.roiRef     as { x: number; y: number; w: number; h: number }
  const roiM   = params.roiMeasure as typeof roiRef

  return <>
    <div className="param-section">기준면 ROI</div>
    <RoiField label="Reference" value={roiRef} onChange={v => set('roiRef', v)} />
    <div className="param-section">측정 ROI</div>
    <RoiField label="Measure"   value={roiM}   onChange={v => set('roiMeasure', v)} />
  </>
}

function ThicknessParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const roi = params.roi as { xMin: number; xMax: number; yMin: number; yMax: number }

  return <>
    <div className="param-section">ROI (mm)</div>
    {(['xMin','xMax','yMin','yMax'] as const).map(f => (
      <NumField key={f} label={f} value={roi[f]} step={0.1} onChange={v => set('roi', { ...roi, [f]: v })} />
    ))}
    <div className="param-section">측정</div>
    <NumField label="Nominal (mm)"   value={params.nominalMm as number}   step={0.01} onChange={v => set('nominalMm', v)} />
    <NumField label="Tolerance (mm)" value={params.toleranceMm as number} step={0.01} onChange={v => set('toleranceMm', v)} />
  </>
}

function NoiseFilterParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  return <>
    <div className="param-section">2D</div>
    <NumField label="Kernel Size" value={params.kernelSize as number} onChange={v => set('kernelSize', v)} />
    <div className="param-section">3D</div>
    <NumField label="Radius"        value={params.radius as number}        step={0.1} onChange={v => set('radius', v)} />
    <NumField label="Min Neighbors" value={params.minNeighbors as number}             onChange={v => set('minNeighbors', v)} />
  </>
}

function CsvWriterParams({ params, onChange }: { params: Record<string, unknown>; onChange: (p: Record<string, unknown>) => void }) {
  const set = (key: string, val: unknown) => onChange({ ...params, [key]: val })
  const path = params.path as string ?? ''
  const filename = path ? path.split(/[\\/]/).pop() : ''

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
      {filename && <div className="param-path-display" title={path}>{filename}</div>}
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
      window.electronAPI.listFolderImages(folder).then(setFiles)
    } else {
      setFiles([])
    }
  }, [mode, folder])

  const pickFolder = async () => {
    const dir = await window.electronAPI?.openFolder?.()
    if (dir) onChange({ ...params, folder: dir, index: 0 })
  }

  const clampIdx = Math.min(Math.max(0, index), Math.max(0, files.length - 1))
  const cur = files[clampIdx]

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
        {folder && <div className="param-path-display" title={folder}>{folder.split(/[\\/]/).pop()} · 이미지 {files.length}개</div>}
      </div>
      {files.length > 0 && (
        <div className="param-row">
          <span className="param-label">이미지</span>
          <div className="zl-idx">
            <button className="btn-browse" disabled={clampIdx <= 0} onClick={() => set('index', clampIdx - 1)}>◀</button>
            <span className="zl-idx-cur" title={cur?.name}>{clampIdx + 1}/{files.length}</span>
            <button className="btn-browse" disabled={clampIdx >= files.length - 1} onClick={() => set('index', clampIdx + 1)}>▶</button>
          </div>
        </div>
      )}
      {cur && <div className="param-path-display" title={cur.name}>{cur.name}</div>}
      <div className="param-empty" style={{ fontSize: 10 }}>연속검사(📁 폴더검사)가 이 폴더를 순서대로 검사합니다</div>
    </>}
    <div className="param-section">분해능 (mm/px)</div>
    <NumField label="X Res" value={params.xResMm as number ?? 1.0} step={0.001} onChange={v => set('xResMm', v)} />
    <NumField label="Y Res" value={params.yResMm as number ?? 1.0} step={0.001} onChange={v => set('yResMm', v)} />
    <NumField label="Z Res" value={params.zResMm as number ?? 0.001} step={0.0001} onChange={v => set('zResMm', v)} />
  </>
}

export default function ParamPanel({ nodeId, toolType, label, params, onParamChange, onClose, embedded }: Props) {
  const def = TOOL_DEF_MAP[toolType]
  if (!def) return null

  const handleChange = (p: Record<string, unknown>) => onParamChange(nodeId, p)

  if (embedded) {
    return (
      <div className="param-panel-body">
        {toolType === 'LineFitHeight'    && <LineFitParams    params={params} onChange={handleChange} />}
        {toolType === 'PlaneFit'         && <PlaneFitParams   params={params} onChange={handleChange} />}
        {toolType === 'ThicknessMeasure' && <ThicknessParams  params={params} onChange={handleChange} />}
        {toolType === 'NoiseFilter'      && <NoiseFilterParams params={params} onChange={handleChange} />}
        {toolType === 'ZMapLoader' && <ZMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ImageLoader' && <LoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
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
        {toolType === 'LineFitHeight'    && <LineFitParams    params={params} onChange={handleChange} />}
        {toolType === 'PlaneFit'         && <PlaneFitParams   params={params} onChange={handleChange} />}
        {toolType === 'ThicknessMeasure' && <ThicknessParams  params={params} onChange={handleChange} />}
        {toolType === 'NoiseFilter'      && <NoiseFilterParams params={params} onChange={handleChange} />}
        {toolType === 'ZMapLoader' && <ZMapLoaderParams params={params} onChange={handleChange} />}
        {toolType === 'ImageLoader' && <LoaderParams params={params} onChange={handleChange} toolType={toolType} />}
        {toolType === 'CsvWriter'        && <CsvWriterParams params={params} onChange={handleChange} />}
        {toolType === 'EdgeDetector'     && <div className="param-empty">파라미터 없음</div>}
      </div>
    </div>
  )
}
