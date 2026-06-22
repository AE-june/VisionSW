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
    engineProcess = spawn(exePath, [], {
      stdio: ['ignore', 'pipe', 'pipe'],
      detached: false,
    })
    engineProcess.stdout?.on('data', (d) => process.stdout.write('[C++] ' + d))
    engineProcess.stderr?.on('data', (d) => process.stderr.write('[C++] ' + d))
    engineProcess.on('error', (e) => console.error('[engine] spawn error:', e.message))
    engineProcess.on('exit', (code) => console.log('[engine] exit code:', code))
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

// ── IPC handlers ──────────────────────────────────────────────────────────

export function registerEngineIpc() {
  ipcMain.handle('engine:run', async (_event, recipe: unknown) => {
    if (!wsReady || !ws) {
      return { error: 'VisionEngine not connected' }
    }
    ws.send(JSON.stringify({ cmd: 'run', ...recipe as object }))
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
}
