export interface BatchFile { name: string; path: string; mtimeMs: number }
export interface BatchResult { file: string; pass: boolean | null; totalMs: number; error?: boolean }
export interface BatchLoader { id: string; label: string; folder: string; count: number }
export type BatchSortKey = 'name' | 'time'

interface Props {
  running: boolean
  loaders: BatchLoader[]  // folder 모드 ZMapLoader 목록 (노드 folder 설정에서 수집)
  sortKey: BatchSortKey
  index: number           // 현재 세트 인덱스 (0-based)
  cycle: number           // 현재 회차 (1-based)
  repeat: boolean
  showPreview: boolean    // 배치 중 디스플레이(미리보기) 갱신 여부
  results: BatchResult[]
  onSortKey: (k: BatchSortKey) => void
  onToggleRepeat: (v: boolean) => void
  onTogglePreview: (v: boolean) => void
  onStart: () => void
  onStop: () => void
  onClose: () => void
}

const baseName = (p: string) => p.split(/[\\/]/).pop() ?? p

export default function FolderInspectPanel({
  running, loaders, sortKey, index, cycle, repeat, showPreview, results,
  onSortKey, onToggleRepeat, onTogglePreview, onStart, onStop, onClose,
}: Props) {
  const counts = loaders.map(l => l.count)
  const total = counts.length ? Math.min(...counts) : 0
  const mismatch = counts.length > 1 && new Set(counts).size > 1
  const passCnt = results.filter(r => r.pass === true).length
  const failCnt = results.filter(r => r.pass === false).length
  const canStart = loaders.length > 0 && total > 0

  return (
    <div className="fi-overlay" onMouseDown={e => { if (e.target === e.currentTarget) onClose() }}>
      <div className="fi-panel">
        <div className="fi-header">
          <span>📁 폴더검사 / 연속검사</span>
          <button className="param-close" onClick={onClose}>✕</button>
        </div>

        <div className="fi-body">
          {loaders.length === 0 ? (
            <div className="fi-warn">
              ⚠ 폴더 모드로 설정된 ZMapLoader가 없습니다. ZMapLoader 노드를 열어 <b>모드 → 폴더(연속검사)</b>로 설정하고 폴더를 지정하세요.
            </div>
          ) : (
            <>
              {/* 폴더 모드 로더 목록 (노드 folder 설정 기준) */}
              <div className="fi-row fi-sub">폴더 로더 {loaders.length}개 · 세트 {total}개씩 순서대로 검사</div>
              <div className="fi-loaders">
                {loaders.map(l => (
                  <div className="fi-loader-row" key={l.id}>
                    <span className="fi-loader-label" title={l.folder}>{l.label}</span>
                    <span className="fi-loader-folder" title={l.folder}>{baseName(l.folder)}</span>
                    <span className={`fi-loader-count${mismatch ? ' warn' : ''}`}>{l.count}개</span>
                  </div>
                ))}
              </div>
              {mismatch && (
                <div className="fi-row fi-sub fi-warn-text">⚠ 폴더별 이미지 개수가 다릅니다 — 최소 {total}개까지만 검사합니다.</div>
              )}

              {/* 매칭 정렬 기준 */}
              <div className="fi-row">
                <span className="fi-opt-label">매칭 정렬</span>
                <label className="fi-check">
                  <input type="radio" name="fi-sort" checked={sortKey === 'name'} disabled={running}
                    onChange={() => onSortKey('name')} /> 이름순
                </label>
                <label className="fi-check">
                  <input type="radio" name="fi-sort" checked={sortKey === 'time'} disabled={running}
                    onChange={() => onSortKey('time')} /> 시간순(수정시각)
                </label>
              </div>
            </>
          )}

          {/* 옵션 */}
          <div className="fi-row">
            <label className="fi-check">
              <input type="checkbox" checked={repeat} disabled={running}
                onChange={e => onToggleRepeat(e.target.checked)} /> 무한반복 (중지까지 폴더 반복)
            </label>
          </div>
          <div className="fi-row">
            <label className="fi-check">
              <input type="checkbox" checked={showPreview} disabled={running}
                onChange={e => onTogglePreview(e.target.checked)} /> 디스플레이 업데이트 (매 세트 미리보기 갱신 · 느려짐)
            </label>
          </div>

          {/* 실행 */}
          <div className="fi-row">
            {running ? (
              <button className="btn-stop" onClick={onStop}>■ 중지</button>
            ) : (
              <button className="btn-run" onClick={onStart} disabled={!canStart}>▶ 시작</button>
            )}
            {(running || results.length > 0) && (
              <span className="fi-progress">
                {repeat && `${cycle}회차 · `}
                {Math.min(index + (running ? 1 : 0), total)}/{total}
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
              <div className="fi-res-head"><span>세트</span><span>결과</span><span>시간</span></div>
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
