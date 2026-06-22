interface Props {
  running: boolean
  engineConnected: boolean
  onRun: () => void
  onStop: () => void
  onToggleResult: () => void
  onOpenFile: () => void
}

export default function Toolbar({ running, engineConnected, onRun, onStop, onToggleResult, onOpenFile }: Props) {
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
      <div className="toolbar-divider" />
      <button className="btn-icon" title="레시피 열기" onClick={onOpenFile}>📂</button>
      <button className="btn-icon" title="레시피 저장">💾</button>
      <div className="toolbar-spacer" />
      <span className="status-item" style={{ fontSize: 11, marginRight: 8 }}>
        {engineConnected
          ? <span style={{ color: '#4caf50' }}>● Engine</span>
          : <span style={{ color: '#888' }}>○ Engine</span>
        }
      </span>
      <button className="btn-panel" onClick={onToggleResult}>결과 패널</button>
    </div>
  )
}
