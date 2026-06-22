import { app, shell, BrowserWindow, ipcMain, dialog } from 'electron'
import { join } from 'path'
import { electronApp, optimizer, is } from '@electron-toolkit/utils'
import { startEngine, stopEngine, registerEngineIpc, engineEvents, isEngineReady } from './engine'

let mainWindow: BrowserWindow | null = null

function createWindow(): void {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1024,
    minHeight: 600,
    show: false,
    title: 'VisionSW',
    backgroundColor: '#181818',
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      sandbox: false
    }
  })

  mainWindow.on('ready-to-show', () => {
    mainWindow!.show()
  })

  // renderer가 로드된 후 현재 엔진 연결 상태를 한 번 더 전송
  mainWindow.webContents.on('did-finish-load', () => {
    if (isEngineReady()) {
      mainWindow?.webContents.send('engine:event', { event: 'ready' })
    }
  })

  mainWindow.webContents.setWindowOpenHandler((details) => {
    shell.openExternal(details.url)
    return { action: 'deny' }
  })

  if (is.dev && process.env['ELECTRON_RENDERER_URL']) {
    mainWindow.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    mainWindow.loadFile(join(__dirname, '../renderer/index.html'))
  }
}

// Forward engine WebSocket events to renderer via IPC
function setupEngineForwarding() {
  engineEvents.onMessage = (data) => {
    mainWindow?.webContents.send('engine:event', data)
  }
}

// File dialog IPC
ipcMain.handle('dialog:openFile', async (_event, filters: Electron.FileFilter[]) => {
  const result = await dialog.showOpenDialog({
    properties: ['openFile'],
    filters: filters ?? [{ name: 'All Files', extensions: ['*'] }]
  })
  return result.canceled ? null : result.filePaths[0]
})

app.whenReady().then(() => {
  electronApp.setAppUserModelId('com.smartray.visionsw')

  app.on('browser-window-created', (_, window) => {
    optimizer.watchWindowShortcuts(window)
  })

  createWindow()
  setupEngineForwarding()
  registerEngineIpc()
  startEngine()

  app.on('activate', function () {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  stopEngine()
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
