import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('electronAPI', {
  // File system
  openFile: (filters?: Electron.FileFilter[]) =>
    ipcRenderer.invoke('dialog:openFile', filters),

  // VisionEngine: send run command, returns { ok } or { error }
  engineRun: (recipe: unknown) =>
    ipcRenderer.invoke('engine:run', recipe),

  // VisionEngine: ping to check connection
  enginePing: () =>
    ipcRenderer.invoke('engine:ping'),

  // Check current engine connection status (call on mount)
  engineIsReady: () => ipcRenderer.invoke('engine:isReady'),

  // Subscribe to streamed events from VisionEngine
  onEngineEvent: (cb: (data: unknown) => void) => {
    const handler = (_: Electron.IpcRendererEvent, data: unknown) => cb(data)
    ipcRenderer.on('engine:event', handler)
    return () => ipcRenderer.removeListener('engine:event', handler)
  },
})
