import { useState, useRef, useEffect } from 'react'

interface Props {
  running: boolean
  engineConnected: boolean
  onRun: () => void
  onStop: () => void
  onToggleResult: () => void
  onSaveRecipe: () => void
  onSaveRecipeAs: () => void
  recipeName: string | null
  onLoadRecipe: () => void
  onRestartEngine: () => void
  onOpenFolderInspect: () => void
}

export default function Toolbar({ running, engineConnected, onRun, onStop, onToggleResult, onSaveRecipe, onSaveRecipeAs, recipeName, onLoadRecipe, onRestartEngine, onOpenFolderInspect }: Props) {
  const [menuOpen, setMenuOpen] = useState(false)
  const menuRef = useRef<HTMLDivElement>(null)

  // 바깥 클릭 시 메뉴 닫기
  useEffect(() => {
    if (!menuOpen) return
    const onDown = (e: MouseEvent) => {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) setMenuOpen(false)
    }
    window.addEventListener('mousedown', onDown)
    return () => window.removeEventListener('mousedown', onDown)
  }, [menuOpen])

  const item = (label: string, shortcut: string, fn: () => void) => (
    <button className="tb-menu-item" onClick={() => { setMenuOpen(false); fn() }}>
      <span>{label}</span>{shortcut && <span className="tb-menu-key">{shortcut}</span>}
    </button>
  )

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

      {/* 파일 메뉴 (열기/저장/다른 이름으로 저장) */}
      <div className="tb-menu" ref={menuRef}>
        <button className={`btn-icon${menuOpen ? ' active' : ''}`} onClick={() => setMenuOpen(o => !o)}>파일 ▾</button>
        {menuOpen && (
          <div className="tb-menu-list">
            {item('레시피 열기', '', onLoadRecipe)}
            <div className="tb-menu-sep" />
            {item('저장', 'Ctrl+S', onSaveRecipe)}
            {item('다른 이름으로 저장', 'Ctrl+Shift+S', onSaveRecipeAs)}
          </div>
        )}
      </div>
      <span className="recipe-name" title={recipeName ?? '저장 안 됨'}>{recipeName ?? '무제'}</span>

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
