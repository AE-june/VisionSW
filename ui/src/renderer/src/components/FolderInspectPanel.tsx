export interface BatchFile { name: string; path: string }
export interface BatchResult { file: string; pass: boolean | null; totalMs: number; error?: boolean }

interface Props {
  running: boolean
  folder: string | null
  files: BatchFile[]
  index: number          // 현재 파일 인덱스 (0-based)
  cycle: number          // 현재 회차 (1-based)
  repeat: boolean
  results: BatchResult[]
  hasZMap: boolean
  onPickFolder: () => void
  onToggleRepeat: (v: boolean) => void
  onStart: () => void
  onStop: () => void
  onClose: () => void
}

export default function FolderInspectPanel({
  running, folder, files, index, cycle, repeat, results, hasZMap,
  onPickFolder, onToggleRepeat, onStart, onStop, onClose,
}: Props) {
  const total = files.length
  const passCnt = results.filter(r => r.pass === true).length
  const failCnt = results.filter(r => r.pass === false).length

  return (
    <div className="fi-overlay" onMouseDown={e => { if (e.target === e.currentTarget && !running) onClose() }}>
      <div className="fi-panel">
        <div className="fi-header">
          <span>📁 폴더검사 / 연속검사</span>
          <button className="param-close" onClick={onClose} disabled={running}>✕</button>
        </div>

        <div className="fi-body">
          {/* 폴더 선택 */}
          <div className="fi-row">
            <button className="pfe-btn" onClick={onPickFolder} disabled={running}>폴더 선택</button>
            <span className="fi-folder" title={folder ?? ''}>{folder ?? '폴더를 선택하세요'}</span>
          </div>
          <div className="fi-row fi-sub">
            이미지 {total}개 {total > 0 && `· 순서대로 전체 1회 검사`}
          </div>

          {!hasZMap && (
            <div className="fi-warn">⚠ 그래프에 ZMapLoader 노드가 없어 폴더검사를 시작할 수 없습니다.</div>
          )}

          {/* 옵션 + 실행 */}
          <div className="fi-row">
            <label className="fi-check">
              <input type="checkbox" checked={repeat} disabled={running}
                onChange={e => onToggleRepeat(e.target.checked)} /> 무한반복 (중지까지 폴더 반복)
            </label>
          </div>
          <div className="fi-row">
            {running ? (
              <button className="btn-stop" onClick={onStop}>■ 중지</button>
            ) : (
              <button className="btn-run" onClick={onStart} disabled={total === 0 || !hasZMap}>▶ 시작</button>
            )}
            {(running || results.length > 0) && (
              <span className="fi-progress">
                {repeat && `${cycle}회차 · `}
                {Math.min(index + (running ? 1 : 0), total)}/{total}
                {running && files[index] ? ` · ${files[index].name}` : ''}
                {` · ✓${passCnt} ✗${failCnt}`}
              </span>
            )}
          </div>

          {/* 진행바 */}
          {(running || results.length > 0) && total > 0 && (
            <div className="fi-bar"><div className="fi-bar-fill"
              style={{ width: `${Math.min(100, (index + (running ? 1 : 0)) / total * 100)}%` }} /></div>
          )}

          {/* 결과 요약표 */}
          {results.length > 0 && (
            <div className="fi-results">
              <div className="fi-res-head"><span>파일</span><span>결과</span><span>시간</span></div>
              <div className="fi-res-scroll">
                {results.map((r, i) => (
                  <div className="fi-res-row" key={i}>
                    <span className="fi-res-file" title={r.file}>{r.file}</span>
                    <span className={`fi-res-pass ${r.error ? 'err' : r.pass ? 'pass' : 'fail'}`}>
                      {r.error ? 'ERROR' : r.pass ? 'PASS' : 'FAIL'}
                    </span>
                    <span className="fi-res-ms">{Math.round(r.totalMs)} ms</span>
                  </div>
                ))}
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
