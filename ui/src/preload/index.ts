import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('electronAPI', {
  // File system
  openFile: (filters?: Electron.FileFilter[]) =>
    ipcRenderer.invoke('dialog:openFile', filters),

  // 저장 다이얼로그 (폴더 + 파일명)
  saveFile: (filters?: Electron.FileFilter[]) =>
    ipcRenderer.invoke('dialog:saveFile', filters),

  // 레시피 저장/불러오기
  saveRecipe: (path: string, content: string) =>
    ipcRenderer.invoke('recipe:save', path, content),
  loadRecipe: (path: string) =>
    ipcRenderer.invoke('recipe:load', path),

  // 결과 이미지(오버레이 합성 PNG) 저장 — 저장 다이얼로그 후 파일 쓰기. 저장 경로 반환(취소 시 null)
  saveImage: (defaultName: string, dataURL: string) =>
    ipcRenderer.invoke('image:save', defaultName, dataURL),

  // 네이티브 메뉴(File) 클릭 액션 구독 — 'openRecipe' | 'saveRecipe' | 'saveRecipeAs'
  onMenuAction: (cb: (action: string) => void) => {
    const handler = (_: Electron.IpcRendererEvent, action: string) => cb(action)
    ipcRenderer.on('menu:action', handler)
    return () => ipcRenderer.removeListener('menu:action', handler)
  },

  // 폴더검사: 폴더 선택 + 이미지 목록
  openFolder: () => ipcRenderer.invoke('dialog:openFolder'),
  listFolderImages: (dir: string) =>
    ipcRenderer.invoke('folder:listImages', dir),

  // VisionEngine: send run command, returns { ok } or { error }
  engineRun: (recipe: unknown) =>
    ipcRenderer.invoke('engine:run', recipe),

  // VisionEngine: ping to check connection
  enginePing: () =>
    ipcRenderer.invoke('engine:ping'),

  // Check current engine connection status (call on mount)
  engineIsReady: () => ipcRenderer.invoke('engine:isReady'),

  // Force restart / reconnect the VisionEngine
  engineRestart: () => ipcRenderer.invoke('engine:restart'),

  // HeightMapLoader 폴더 선택 시 미리 캐시
  enginePreload: (folder: string, xResMm: number, yResMm: number, zResMm: number) =>
    ipcRenderer.invoke('engine:preload', folder, xResMm, yResMm, zResMm),

  // 특정 노드의 특정 프로파일 행 x/z 데이터 요청 (비동기 — 응답은 onEngineEvent profileData 이벤트)
  engineFetchProfile: (nodeId: string, profileIdx: number) =>
    ipcRenderer.invoke('engine:fetchProfile', nodeId, profileIdx),

  // NotchMeasureV2 chunk envelope 온디맨드 요청 (응답은 onEngineEvent notchEnvData)
  engineFetchNotchEnv: (nodeId: string, chunkIdx: number) =>
    ipcRenderer.invoke('engine:fetchNotchEnv', nodeId, chunkIdx),

  // Subscribe to streamed events from VisionEngine
  onEngineEvent: (cb: (data: unknown) => void) => {
    const handler = (_: Electron.IpcRendererEvent, data: unknown) => cb(data)
    ipcRenderer.on('engine:event', handler)
    return () => ipcRenderer.removeListener('engine:event', handler)
  },

  // 폴더검사(배치) 전용 워커풀 — 인터랙티브 엔진(engine:*)과 별개 프로세스들
  batchStart: (count?: number) => ipcRenderer.invoke('batch:start', count),
  batchStop: () => ipcRenderer.invoke('batch:stop'),
  batchRun: (workerId: number, recipe: unknown) => ipcRenderer.invoke('batch:run', workerId, recipe),
  batchPrefetch: (workerId: number, path: string, xResMm: number, yResMm: number, zResMm: number) =>
    ipcRenderer.invoke('batch:prefetch', workerId, path, xResMm, yResMm, zResMm),
  // 워커가 죽었을 때 같은 슬롯을 재기동 (전체 중단 대신 배치 지속)
  batchRespawn: (workerId: number) => ipcRenderer.invoke('batch:respawn', workerId),
  onBatchEvent: (cb: (data: unknown) => void) => {
    const handler = (_: Electron.IpcRendererEvent, data: unknown) => cb(data)
    ipcRenderer.on('batch:event', handler)
    return () => ipcRenderer.removeListener('batch:event', handler)
  },
})
