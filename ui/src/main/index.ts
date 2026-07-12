import { app, shell, BrowserWindow, ipcMain, dialog, Menu } from 'electron'
import { join, basename } from 'path'
import { writeFile, readFile, readdir, stat } from 'fs/promises'
import { electronApp, optimizer, is } from '@electron-toolkit/utils'
import { startEngine, stopEngine, registerEngineIpc, engineEvents, isEngineReady } from './engine'
import { registerBatchIpc, setBatchWindow, stopBatchPool } from './batchPool'

let mainWindow: BrowserWindow | null = null

// 네이티브 메뉴바 — File에 레시피 조작(열기/저장/다른 이름으로 저장)을 넣고,
// 클릭 시 렌더러로 IPC를 보내 실제 동작(다이얼로그+상태)은 렌더러가 처리한다.
function buildApplicationMenu(win: BrowserWindow): void {
  const send = (action: string) => win.webContents.send('menu:action', action)
  const isMac = process.platform === 'darwin'
  const template: Electron.MenuItemConstructorOptions[] = [
    {
      label: 'File',
      submenu: [
        { label: '레시피 열기', accelerator: 'CmdOrCtrl+O', click: () => send('openRecipe') },
        { type: 'separator' },
        { label: '레시피 저장', accelerator: 'CmdOrCtrl+S', click: () => send('saveRecipe') },
        { label: '레시피 다른 이름으로 저장', accelerator: 'CmdOrCtrl+Shift+S', click: () => send('saveRecipeAs') },
        { type: 'separator' },
        isMac ? { role: 'close' } : { role: 'quit', label: '종료' },
      ],
    },
    { label: 'Edit', submenu: [{ role: 'undo' }, { role: 'redo' }, { type: 'separator' }, { role: 'cut' }, { role: 'copy' }, { role: 'paste' }, { role: 'selectAll' }] },
    { label: 'View', submenu: [{ role: 'reload' }, { role: 'forceReload' }, { role: 'toggleDevTools' }, { type: 'separator' }, { role: 'resetZoom' }, { role: 'zoomIn' }, { role: 'zoomOut' }, { type: 'separator' }, { role: 'togglefullscreen' }] },
    { role: 'windowMenu' },
  ]
  Menu.setApplicationMenu(Menu.buildFromTemplate(template))
}

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

  buildApplicationMenu(mainWindow)

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

// 저장 다이얼로그 (폴더 선택 + 파일명 입력)
ipcMain.handle('dialog:saveFile', async (_event, filters: Electron.FileFilter[]) => {
  const result = await dialog.showSaveDialog({
    filters: filters ?? [{ name: 'All Files', extensions: ['*'] }]
  })
  return result.canceled ? null : result.filePath
})

// 폴더검사: 폴더 선택
ipcMain.handle('dialog:openFolder', async () => {
  const result = await dialog.showOpenDialog({ properties: ['openDirectory'] })
  return result.canceled ? null : result.filePaths[0]
})

// 폴더검사: 폴더 내 이미지 파일 목록 (이름 자연 정렬 + 수정시각)
ipcMain.handle('folder:listImages', async (_event, dir: string) => {
  const exts = new Set(['.png', '.jpg', '.jpeg', '.bmp', '.tif', '.tiff'])
  const entries = await readdir(dir, { withFileTypes: true })
  const names = entries
    .filter(e => e.isFile() && exts.has(e.name.slice(e.name.lastIndexOf('.')).toLowerCase()))
    .map(e => e.name)
  // 자연 정렬 (img2 < img10) — 기본 반환 순서. 시간순은 렌더러에서 mtimeMs로 재정렬.
  names.sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' }))
  return Promise.all(names.map(async name => {
    const path = join(dir, name)
    let mtimeMs = 0
    try { mtimeMs = (await stat(path)).mtimeMs } catch { /* 접근 불가 파일은 0 */ }
    return { name: basename(name), path, mtimeMs }
  }))
})

// 레시피 저장/불러오기 (텍스트 파일 읽기/쓰기)
ipcMain.handle('recipe:save', async (_event, filePath: string, content: string) => {
  await writeFile(filePath, content, 'utf-8')
  return true
})
ipcMain.handle('recipe:load', async (_event, filePath: string) => {
  return await readFile(filePath, 'utf-8')
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
  if (mainWindow) setBatchWindow(mainWindow)
  registerBatchIpc()

  app.on('activate', function () {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  stopEngine()
  stopBatchPool()
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
