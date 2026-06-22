/**
 * Mock VisionEngine — C++ 빌드 없이 UI 테스트용
 * 실행: node mock-engine.mjs
 */

import { WebSocketServer } from 'ws'

const PORT = 9000
const wss = new WebSocketServer({ port: PORT })
console.log(`[MockEngine] listening on ws://localhost:${PORT}`)

// 1x1 gray pixel PNG (base64) — 노드 결과 미리보기 테스트용
const TINY_PNG = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAACklEQVQI12NgAAAAAgAB4iG8MwAAAABJRU5ErkJggg=='

function sleep(ms) { return new Promise(r => setTimeout(r, ms)) }

function send(ws, obj) {
  ws.send(JSON.stringify(obj))
}

async function runPipeline(ws, msg) {
  const nodes = msg.nodes ?? []
  const edges = msg.edges ?? []

  send(ws, { event: 'start' })

  // 위상 정렬 (간단히 edges 기반)
  const inDeg = {}
  const adj = {}
  for (const n of nodes) { inDeg[n.id] = 0; adj[n.id] = [] }
  for (const e of edges) { adj[e.source].push(e.target); inDeg[e.target]++ }
  const queue = nodes.filter(n => inDeg[n.id] === 0).map(n => n.id)
  const order = []
  while (queue.length) {
    const cur = queue.shift()
    order.push(cur)
    for (const nb of (adj[cur] ?? [])) {
      if (--inDeg[nb] === 0) queue.push(nb)
    }
  }

  let pipelinePass = true

  for (const nodeId of order) {
    const node = nodes.find(n => n.id === nodeId)
    if (!node) continue

    send(ws, { event: 'log', level: 'info', msg: `Running ${node.type} [${node.id}]` })
    await sleep(300)  // 처리 시뮬레이션

    const result = makeMockResult(node)
    if (!result.ok) pipelinePass = false
    send(ws, result)
  }

  send(ws, { event: 'done', pass: pipelinePass })
}

function makeMockResult(node) {
  const base = { event: 'result', id: node.id, tool: node.type, ok: true, msg: '' }

  switch (node.type) {
    case 'ZMapLoader':
    case 'ImageLoader':
      return { ...base, preview: TINY_PNG }

    case 'NoiseFilter':
      return { ...base, preview: TINY_PNG }

    case 'EdgeDetector':
      return { ...base, preview: TINY_PNG }

    case 'LineFitHeight': {
      const hd = (Math.random() * 0.5).toFixed(4)
      const pass = parseFloat(hd) < 0.3
      return { ...base, heightDiff: parseFloat(hd), Qz: parseFloat(hd) + 1.2, refZatQ: 1.2, pass }
    }

    case 'ThicknessMeasure': {
      const t = 1.8 + (Math.random() - 0.5) * 0.2
      const nom = node.params?.nominalMm ?? 1.8
      const tol = node.params?.toleranceMm ?? 0.05
      const pass = Math.abs(t - nom) <= tol
      return { ...base, thicknessMm: +t.toFixed(4), minMm: +(t - 0.01).toFixed(4), maxMm: +(t + 0.01).toFixed(4), pass }
    }

    default:
      return base
  }
}

wss.on('connection', ws => {
  console.log('[MockEngine] client connected')
  send(ws, { event: 'ready' })

  ws.on('message', async raw => {
    try {
      const msg = JSON.parse(raw.toString())
      console.log('[MockEngine] cmd:', msg.cmd)

      if (msg.cmd === 'ping') { send(ws, { event: 'pong' }); return }
      if (msg.cmd === 'run')  { await runPipeline(ws, msg); return }

      send(ws, { event: 'error', msg: `unknown cmd: ${msg.cmd}` })
    } catch(e) {
      send(ws, { event: 'error', msg: e.message })
    }
  })

  ws.on('close', () => console.log('[MockEngine] client disconnected'))
})
