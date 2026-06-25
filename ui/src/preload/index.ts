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

  // Subscribe to streamed events from VisionEngine
  onEngineEvent: (cb: (data: unknown) => void) => {
    const handler = (_: Electron.IpcRendererEvent, data: unknown) => cb(data)
    ipcRenderer.on('engine:event', handler)
    return () => ipcRenderer.removeListener('engine:event', handler)
  },
})
