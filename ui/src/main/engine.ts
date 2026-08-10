import { app, ipcMain } from 'electron'
import { spawn, ChildProcess } from 'child_process'
import { join } from 'path'
import WebSocket from 'ws'

const ENGINE_URL = 'ws://localhost:9000'
const ENGINE_RECONNECT_MS = 1000
const ENGINE_MAX_RETRIES = 10

let engineProcess: ChildProcess | null = null
let ws: WebSocket | null = null
let wsReady = false
let retryCount = 0

export function isEngineReady() { return wsReady }

// Callbacks set by index.ts to forward events to renderer
export const engineEvents = {
  onMessage: (_data: unknown) => {}
}

// ── WebSocket connection ──────────────────────────────────────────────────

function connectWs() {
  if (ws) { ws.terminate(); ws = null }
  wsReady = false

  const socket = new WebSocket(ENGINE_URL)
  ws = socket

  socket.on('open', () => {
    console.log('[engine] WebSocket connected')
    wsReady = true
    retryCount = 0
  })

  socket.on('message', (raw) => {
    try {
      const data = JSON.parse(raw.toString())
      engineEvents.onMessage(data)
    } catch { /* ignore malformed */ }
  })

  socket.on('close', () => {
    wsReady = false
    if (ws !== socket) return  // 명시적으로 교체된 소켓 — 재연결 루프 중복 방지
    ws = null
    if (retryCount < ENGINE_MAX_RETRIES) {
      retryCount++
      setTimeout(connectWs, ENGINE_RECONNECT_MS)
    }
  })

  socket.on('error', () => { /* handled by close */ })
}

// ── Engine process lifecycle ──────────────────────────────────────────────

export function startEngine() {
  const exePath = app.isPackaged
    ? join(process.resourcesPath, 'VisionEngine.exe')
    : join(__dirname, '../../../build/bin/Release/VisionEngine.exe')

  try {
    engineProcess = spawn(exePath, ['--parent-pid', String(process.pid)], {
      stdio: ['ignore', 'pipe', 'pipe'],
      detached: false,
    })
    engineProcess.stdout?.on('data', (d) => process.stdout.write('[C++] ' + d))
    engineProcess.stderr?.on('data', (d) => process.stderr.write('[C++] ' + d))
    engineProcess.on('error', (e) => console.error('[engine] spawn error:', e.message))
    engineProcess.on('exit', (code) => {
      console.log('[engine] exit code:', code)
      engineProcess = null
      if (code !== 0) {
        console.warn('[engine] crashed — restarting in 1s...')
        setTimeout(() => { retryCount = 0; startEngine() }, 1000)
      }
    })
  } catch (e) {
    console.warn('[engine] Could not spawn VisionEngine.exe — using standalone WebSocket mode')
  }

  // Give the engine a moment to start its server, then connect
  setTimeout(connectWs, 500)
}

export function stopEngine() {
  ws?.terminate()
  ws = null
  if (engineProcess) {
    engineProcess.kill()
    engineProcess = null
  }
}

// 강제 재시작: 내부 프로세스/소켓을 정리하고 재연결 카운터를 리셋한 뒤 다시 시작.
// 이미 외부에서 엔진이 떠 있으면 새 spawn은 포트 충돌로 종료되고 기존 엔진에 연결된다.
export function restartEngine() {
  if (ws) { ws.terminate(); ws = null }
  wsReady = false
  if (engineProcess) { engineProcess.kill(); engineProcess = null }
  retryCount = 0
  startEngine()
}

// ── IPC handlers ──────────────────────────────────────────────────────────

export function registerEngineIpc() {
  ipcMain.handle('engine:run', async (_event, recipe: unknown) => {
    if (!wsReady || !ws) {
      return { error: 'VisionEngine not connected' }
    }
    ws.send(JSON.stringify({ cmd: 'run', schemaVersion: 2, ...(recipe as object) }))
    return { ok: true }
  })

  ipcMain.handle('engine:ping', async () => {
    if (!wsReady || !ws) return { connected: false }
    ws.send(JSON.stringify({ cmd: 'ping' }))
    return { connected: true }
  })

  ipcMain.handle('engine:isReady', async () => {
    return { connected: wsReady }
  })

  ipcMain.handle('engine:restart', async () => {
    restartEngine()
    return { ok: true }
  })

  ipcMain.handle('engine:preload', async (_event, folder: string, xResMm: number, yResMm: number, zResMm: number) => {
    if (!wsReady || !ws) return { error: 'VisionEngine not connected' }
    ws.send(JSON.stringify({ cmd: 'preload', folder, xResMm, yResMm, zResMm }))
    return { ok: true }
  })
}
