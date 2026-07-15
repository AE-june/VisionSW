import { app, ipcMain, BrowserWindow } from 'electron'
import { spawn, ChildProcess } from 'child_process'
import { join } from 'path'
import os from 'os'
import WebSocket from 'ws'

// ── 폴더검사(배치)용 엔진 워커풀 ────────────────────────────────────────────
// 인터랙티브 편집용 단일 엔진(engine.ts, 포트 9000)과는 완전히 분리된
// N개의 VisionEngine.exe 프로세스를 별도 포트(9001~)에 띄워 진짜 병렬로 돌린다.
// (엔진 내부 g_cacheMtx가 프로세스당 전역이라 커넥션/스레드만 늘려서는 병렬화되지 않음)

interface Worker {
  id: number
  port: number
  proc: ChildProcess
  ws: WebSocket | null
  ready: boolean
  retiring?: boolean   // 재기동 과정에서 의도적으로 종료 중 — exit 핸들러가 workerExit을 쏘지 않게 함
}

let workers: Worker[] = []
let mainWindowRef: BrowserWindow | null = null
let intentionalStop = false
// 워커별 연속 재기동 실패 횟수 — 계속 죽는 워커에 무한 재시도하지 않기 위함
const crashCounts = new Map<number, number>()
const MAX_RESPAWN_RETRIES = 2

export function setBatchWindow(win: BrowserWindow) {
  mainWindowRef = win
}

function poolLog(level: 'info' | 'warn' | 'error', msg: string) {
  mainWindowRef?.webContents.send('batch:event', { workerId: -1, event: 'log', level, msg })
}

// 워커 수 = CPU 코어 기준과 가용 메모리 기준 중 더 보수적인 쪽.
// 대용량(라인스캔) ZMap을 여러 VisionEngine 프로세스가 동시에 처리하면 프로세스당
// 순간 피크가 수 GB까지 올라갈 수 있어(실측), CPU 코어 수만으로 워커 수를 정하면
// 메모리가 부족한 PC/큰 이미지 조합에서 워커가 그대로 죽어버린다(bad_alloc/응답불능).
function computeSafeWorkerCount(): { n: number; cpuBased: number; memBased: number; freeGB: number } {
  const cpuBased = Math.max(1, Math.floor(os.cpus().length / 2) - 1)
  const freeGB = os.freemem() / 1024 ** 3
  const MEM_BUDGET_GB_PER_WORKER = 3.5   // 실측된 워커당 피크(동시 실행 시 ~3~4GB)에 맞춘 예산
  const RESERVE_GB = 4                   // OS/다른 앱 몫으로 남겨둘 최소 여유
  const memBased = Math.max(1, Math.floor((freeGB - RESERVE_GB) / MEM_BUDGET_GB_PER_WORKER))
  return { n: Math.max(1, Math.min(cpuBased, memBased)), cpuBased, memBased, freeGB }
}

function exePath(): string {
  return app.isPackaged
    ? join(process.resourcesPath, 'VisionEngine.exe')
    : join(__dirname, '../../../build/bin/Release/VisionEngine.exe')
}

function connectWithRetry(port: number, retries = 15): Promise<WebSocket> {
  return new Promise((resolve, reject) => {
    const attempt = (n: number) => {
      const socket = new WebSocket(`ws://localhost:${port}`)
      const onOpen = () => { socket.off('error', onError); resolve(socket) }
      const onError = () => {
        socket.off('open', onOpen)
        if (n <= 0) { reject(new Error(`워커 포트 ${port} 연결 실패`)); return }
        setTimeout(() => attempt(n - 1), 300)
      }
      socket.once('open', onOpen)
      socket.once('error', onError)
    }
    attempt(retries)
  })
}

async function spawnWorker(id: number, port: number): Promise<Worker> {
  const proc = spawn(exePath(), ['--port', String(port), '--parent-pid', String(process.pid)], {
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  proc.stdout?.on('data', (d) => process.stdout.write(`[worker${id}] ${d}`))
  proc.stderr?.on('data', (d) => process.stderr.write(`[worker${id}] ${d}`))

  const worker: Worker = { id, port, proc, ws: null, ready: false }

  proc.on('exit', (code) => {
    worker.ready = false
    if (!intentionalStop && !worker.retiring) {
      mainWindowRef?.webContents.send('batch:event', { workerId: id, event: 'workerExit', code })
    }
  })

  const socket = await connectWithRetry(port)
  worker.ws = socket
  worker.ready = true
  socket.on('message', (raw) => {
    try {
      const data = JSON.parse(raw.toString())
      mainWindowRef?.webContents.send('batch:event', { workerId: id, ...data })
    } catch { /* ignore malformed */ }
  })
  socket.on('close', () => { worker.ready = false })
  return worker
}

export async function startBatchPool(count?: number): Promise<number[]> {
  await stopBatchPool()
  intentionalStop = false
  crashCounts.clear()
  let n: number
  if (count && count > 0) {
    n = count
  } else {
    const safe = computeSafeWorkerCount()
    n = safe.n
    poolLog('info',
      `워커 ${n}개로 시작 (CPU 기준 최대 ${safe.cpuBased}개, 여유 메모리 ${safe.freeGB.toFixed(1)}GB 기준 최대 ${safe.memBased}개)`)
  }
  const basePort = 9001
  workers = await Promise.all(
    Array.from({ length: n }, (_, i) => spawnWorker(i, basePort + i))
  )
  return workers.map((w) => w.id)
}

// 워커가 죽었을 때 같은 포트에 새 프로세스를 다시 띄워 배치를 이어간다.
// (이미지 하나가 유난히 크거나 순간 메모리 피크에 걸려 워커 하나가 죽었다고
//  전체 폴더검사를 중단할 필요는 없다 — 같은 슬롯을 재기동해서 계속 돈다)
export async function respawnWorker(workerId: number): Promise<{ ok?: boolean; error?: string }> {
  const old = workers.find((w) => w.id === workerId)
  if (!old) return { error: `워커 ${workerId}를 찾을 수 없음` }

  const retries = crashCounts.get(workerId) ?? 0
  if (retries >= MAX_RESPAWN_RETRIES) {
    poolLog('error', `워커 ${workerId} 재기동 ${retries}회 실패 — 이 슬롯은 포기하고 나머지로 계속 진행`)
    return { error: '재기동 한도 초과' }
  }
  crashCounts.set(workerId, retries + 1)

  old.retiring = true
  try { old.ws?.terminate() } catch { /* ignore */ }
  try { old.proc.kill() } catch { /* ignore */ }

  try {
    const fresh = await spawnWorker(workerId, old.port)
    const idx = workers.findIndex((w) => w.id === workerId)
    if (idx >= 0) workers[idx] = fresh; else workers.push(fresh)
    poolLog('warn', `워커 ${workerId} 재기동 완료 (${retries + 1}/${MAX_RESPAWN_RETRIES})`)
    return { ok: true }
  } catch (e) {
    poolLog('error', `워커 ${workerId} 재기동 실패: ${(e as Error).message}`)
    return { error: (e as Error).message }
  }
}

export async function stopBatchPool(): Promise<void> {
  intentionalStop = true
  const dying = workers
  workers = []
  // 실제 프로세스 종료(=포트 해제)를 기다린 뒤 리턴해야, 다음 startBatchPool이 같은 포트(9001~)에
  // 새 워커를 띄울 때 이전 워커가 물고 있는 포트 때문에 바인딩 실패 → 스폰 실패하는 문제를 막는다.
  await Promise.all(dying.map((w) => new Promise<void>((resolve) => {
    let settled = false
    const finish = () => { if (!settled) { settled = true; resolve() } }
    // 이미 종료됨
    if (w.proc.exitCode !== null || w.proc.pid == null) { finish(); return }
    w.proc.once('exit', finish)
    try { w.ws?.terminate() } catch { /* ignore */ }
    try { w.proc.kill() } catch { /* ignore */ }
    // kill이 2초 내 안 먹으면 강제 종료(트리 포함) — 그래도 exit 이벤트가 안 오면 그냥 진행.
    setTimeout(() => {
      if (settled) return
      if (w.proc.pid) {
        try {
          if (process.platform === 'win32') spawn('taskkill', ['/PID', String(w.proc.pid), '/F', '/T'], { stdio: 'ignore' })
          else w.proc.kill('SIGKILL')
        } catch { /* ignore */ }
      }
      setTimeout(finish, 800)
    }, 2000)
  })))
}

export function registerBatchIpc(): void {
  ipcMain.handle('batch:start', async (_event, count?: number) => {
    try {
      const workerIds = await startBatchPool(count)
      return { ok: true, workerIds }
    } catch (e) {
      return { error: (e as Error).message }
    }
  })

  ipcMain.handle('batch:stop', async () => {
    await stopBatchPool()
    return { ok: true }
  })

  ipcMain.handle('batch:respawn', async (_event, workerId: number) => {
    return respawnWorker(workerId)
  })

  ipcMain.handle('batch:run', async (_event, workerId: number, recipe: unknown) => {
    const w = workers.find((w) => w.id === workerId)
    if (!w?.ready || !w.ws) return { error: `워커 ${workerId} 연결 안 됨` }
    w.ws.send(JSON.stringify({ cmd: 'run', batch: true, ...(recipe as object) }))
    return { ok: true }
  })

  ipcMain.handle(
    'batch:prefetch',
    async (_event, workerId: number, path: string, xResMm: number, yResMm: number, zResMm: number) => {
      const w = workers.find((w) => w.id === workerId)
      if (!w?.ready || !w.ws) return { error: `워커 ${workerId} 연결 안 됨` }
      w.ws.send(JSON.stringify({ cmd: 'prefetch', path, xResMm, yResMm, zResMm }))
      return { ok: true }
    }
  )
}
