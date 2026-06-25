interface Props {
  running: boolean
  engineConnected: boolean
  onRun: () => void
  onStop: () => void
  onToggleResult: () => void
  onSaveRecipe: () => void
  onLoadRecipe: () => void
  onRestartEngine: () => void
  onOpenFolderInspect: () => void
}

export default function Toolbar({ running, engineConnected, onRun, onStop, onToggleResult, onSaveRecipe, onLoadRecipe, onRestartEngine, onOpenFolderInspect }: Props) {
  return (
    <div className="toolbar">
      <span className="app-title">VisionSW</span>
      <div className="toolbar-divider" />
      <button
        className={running ? 'btn-stop' : 'btn-run'}
        onClick={running ? onStop : onRun}
      >
        {running ? '■ Stop' : '▶ Run'}
      </button>
      <button className="btn-folder" title="폴더검사 / 연속검사" onClick={onOpenFolderInspect}>📁 폴더검사</button>
      <div className="toolbar-divider" />
      <button className="btn-icon" title="레시피 열기" onClick={onLoadRecipe}>📂</button>
      <button className="btn-icon" title="레시피 저장" onClick={onSaveRecipe}>💾</button>
      <div className="toolbar-spacer" />
      <span className="status-item" style={{ fontSize: 11, marginRight: 6 }}>
        {engineConnected
          ? <span style={{ color: '#4caf50' }}>● Engine</span>
          : <span style={{ color: '#ff5252' }}>○ Engine</span>
        }
      </span>
      <button
        className="btn-icon"
        title={engineConnected ? '엔진 재시작' : '엔진 시작 / 재연결'}
        onClick={onRestartEngine}
        style={!engineConnected ? { color: '#ff5252', borderColor: '#ff5252' } : undefined}
      >↻</button>
      <button className="btn-panel" onClick={onToggleResult}>결과 패널</button>
    </div>
  )
}
