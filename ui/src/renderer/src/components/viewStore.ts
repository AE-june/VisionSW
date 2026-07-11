// 노드별 이미지 뷰어 표시 상태를 세션 동안 유지한다.
// 패널 토글(언마운트)·노드 전환·탭 변경에도 살아있어야 하므로 컴포넌트 로컬 state가 아닌
// 모듈 레벨 맵에 보관한다. (앱 재시작 시엔 초기화 — 디스크 저장 아님)
export interface ViewState {
  zoom?: number
  colormap?: boolean
  autoRange?: boolean
  rangeLo?: number
  rangeHi?: number
  canvasH?: number
  unit?: 'px' | 'mm'
  drawShape?: 'rect' | 'circle'
  tab?: 'params' | 'result'
}

const store = new Map<string, ViewState>()

export function getViewState(key: string): ViewState {
  return store.get(key) ?? {}
}

export function patchViewState(key: string, patch: ViewState): void {
  store.set(key, { ...store.get(key), ...patch })
}
