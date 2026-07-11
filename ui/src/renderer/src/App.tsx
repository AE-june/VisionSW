import { useState, useRef, useCallback, useEffect } from 'react'
import { addEdge, useNodesState, useEdgesState } from '@xyflow/react'
import type { Connection, Node, Edge, NodeMouseHandler } from '@xyflow/react'
import Toolbar from './components/Toolbar'
import ToolboxPanel from './components/ToolboxPanel'
import NodeCanvas from './components/NodeCanvas'
import ResultPanel from './components/ResultPanel'
import type { MeasureResult, LogEntry } from './components/ResultPanel'
import NodePanel from './components/NodePanel'
import FolderInspectPanel, { type BatchResult, type BatchLoader, type BatchSortKey } from './components/FolderInspectPanel'
import { HoveredEdgeContext, type HoveredEdge } from './components/hoveredEdge'
import { TOOL_DEF_MAP } from './types/tools'
import './App.css'

declare global {
  interface Window {
    electronAPI?: {
      openFile: (filters?: Electron.FileFilter[]) => Promise<string | null>
      saveFile: (filters?: Electron.FileFilter[]) => Promise<string | null>
      saveRecipe: (path: string, content: string) => Promise<boolean>
      loadRecipe: (path: string) => Promise<string>
      engineRun: (recipe: unknown) => Promise<{ ok?: boolean; error?: string }>
      enginePing: () => Promise<{ connected: boolean }>
      engineIsReady: () => Promise<{ connected: boolean }>
      engineRestart: () => Promise<{ ok?: boolean }>
      onEngineEvent: (cb: (data: unknown) => void) => () => void
      openFolder: () => Promise<string | null>
      listFolderImages: (dir: string) => Promise<{ name: string; path: string; mtimeMs: number }[]>
      // 폴더검사(배치) 전용 워커풀
      batchStart: (count?: number) => Promise<{ ok?: boolean; workerIds?: number[]; error?: string }>
      batchStop: () => Promise<{ ok?: boolean }>
      batchRun: (workerId: number, recipe: unknown) => Promise<{ ok?: boolean; error?: string }>
      batchPrefetch: (
        workerId: number, path: string, xResMm: number, yResMm: number, zResMm: number
      ) => Promise<{ ok?: boolean; error?: string }>
      batchRespawn: (workerId: number) => Promise<{ ok?: boolean; error?: string }>
      onBatchEvent: (cb: (data: unknown) => void) => () => void
    }
  }
}

const initialNodes: Node[] = []
const initialEdges: Edge[] = []

export default function App() {
  const [resultVisible, setResultVisible] = useState(true)
  const [panelWidth, setPanelWidth] = useState(280)
  const [lastTotalMs, setLastTotalMs] = useState<number | null>(null)
  const [hoveredEdge, setHoveredEdge] = useState<HoveredEdge | null>(null)
  const [nodes, setNodes, onNodesChange] = useNodesState(initialNodes)
  const [edges, setEdges, onEdgesChange] = useEdgesState(initialEdges)
  const [selectedNodeId, setSelectedNodeId] = useState<string | null>(null)
  const [panelPinned, setPanelPinned] = useState(false)
  const [running, setRunning] = useState(false)
  const [engineConnected, setEngineConnected] = useState(false)
  const [results, setResults] = useState<MeasureResult[]>([])
  const [logs, setLogs] = useState<LogEntry[]>([])
  const [overallPass, setOverallPass] = useState<boolean | null>(null)
  const [nodeResults, setNodeResults] = useState<Record<string, Record<string, unknown>>>({})
  const nodesRef = useRef(nodes)
  const edgesRef = useRef(edges)
  useEffect(() => { nodesRef.current = nodes }, [nodes])
  useEffect(() => { edgesRef.current = edges }, [edges])

  // ── 폴더검사(연속검사) 상태 ──
  const [batchOpen, setBatchOpen] = useState(false)
  const [batchRunning, setBatchRunning] = useState(false)
  const [batchLoaders, setBatchLoaders] = useState<BatchLoader[]>([])   // folder 모드 ZMapLoader 목록
  const [batchSortKey, setBatchSortKey] = useState<BatchSortKey>('name') // 폴더 간 매칭 정렬 기준
  const [batchIndex, setBatchIndex] = useState(0)
  const [batchCycle, setBatchCycle] = useState(1)
  const [batchRepeat, setBatchRepeat] = useState(false)
  const [batchPreview, setBatchPreview] = useState(false)   // 배치 중 디스플레이 갱신(미리보기 생성) 여부
  const [batchResults, setBatchResults] = useState<BatchResult[]>([])
  // 'done'/'error' 이벤트를 한 실행씩 await하기 위한 resolver, 중지 플래그
  const pendingDoneRef = useRef<((r: { pass: boolean; totalMs: number; error?: boolean }) => void) | null>(null)
  const batchStopRef = useRef(false)

  // Subscribe to engine events
  useEffect(() => {
    const api = window.electronAPI
    if (!api) return

    // 마운트 시 현재 연결 상태 즉시 조회
    api.engineIsReady().then(({ connected }) => {
      if (connected) {
        setEngineConnected(true)
        setLogs(l => [...l, { level: 'info', msg: 'VisionEngine connected' }])
      }
    })

    const unsub = api.onEngineEvent((raw) => {
      const data = raw as Record<string, unknown>
      const event = data.event as string

      if (event === 'ready') {
        setEngineConnected(true)
        setLogs(l => [...l, { level: 'info', msg: 'VisionEngine connected' }])
      }
      if (event === 'log') {
        setLogs(l => [...l, {
          level: (data.level as LogEntry['level']) ?? 'info',
          msg: data.msg as string
        }])
      }
      if (event === 'result') {
        const r = data as unknown as MeasureResult
        setResults(prev => [...prev, r])
        // Store per-node result
        const nodeId = data.id as string
        if (nodeId) {
          setNodeResults(prev => ({ ...prev, [nodeId]: data as Record<string, unknown> }))
          // Update node data so ToolNode can show inline result
          setNodes(nds => nds.map(n =>
            n.id === nodeId ? { ...n, data: { ...n.data, result: data } } : n
          ))
        }
      }
      if (event === 'done') {
        const pass = data.pass as boolean
        const totalMs = typeof data.totalMs === 'number' ? data.totalMs : 0
        setOverallPass(pass)
        if (totalMs) setLastTotalMs(totalMs)
        // 폴더검사가 한 장을 기다리는 중이면 그 Promise를 풀고 루프가 진행을 맡음
        if (pendingDoneRef.current) {
          const resolve = pendingDoneRef.current
          pendingDoneRef.current = null
          resolve({ pass, totalMs })
          return
        }
        setRunning(false)
        setLogs(l => [...l, { level: 'info', msg: `Pipeline done — ${pass ? 'PASS' : 'FAIL'}` }])
      }
      if (event === 'error') {
        if (pendingDoneRef.current) {
          const resolve = pendingDoneRef.current
          pendingDoneRef.current = null
          setLogs(l => [...l, { level: 'error', msg: data.msg as string }])
          resolve({ pass: false, totalMs: 0, error: true })
          return
        }
        setRunning(false)
        setLogs(l => [...l, { level: 'error', msg: data.msg as string }])
      }
      if (event === 'pong') {
        setEngineConnected(true)
      }
    })
    return unsub
  }, [])

  const onConnect = useCallback(
    (params: Connection) => setEdges((eds) => addEdge({ ...params, animated: false }, eds)),
    [setEdges]
  )

  // Stable ref — always uses latest nodes/edges without re-creating
  const handleNodeRun = useCallback(async (nodeId: string) => {
    const api = window.electronAPI
    if (!api) return
    const target = nodesRef.current.find(n => n.id === nodeId)
    if (!target) return

    setNodeResults(prev => { const next = { ...prev }; delete next[nodeId]; return next })
    setNodes(nds => nds.map(n =>
      n.id === nodeId ? { ...n, data: { ...n.data, result: undefined } } : n
    ))
    setLogs(l => [...l, { level: 'info', msg: `실행: ${(target.data as { label: string }).label}` }])

    // 타겟 노드의 모든 조상(입력 체인)을 수집 — 상류가 실행돼야 입력 데이터가 채워짐
    const allNodes = nodesRef.current
    const allEdges = edgesRef.current
    const needed = new Set<string>([nodeId])
    const queue = [nodeId]
    while (queue.length) {
      const cur = queue.shift()!
      for (const e of allEdges) {
        if (e.target === cur && !needed.has(e.source)) {
          needed.add(e.source)
          queue.push(e.source)
        }
      }
    }

    const recipe = {
      nodes: allNodes.filter(n => needed.has(n.id)).map(n => ({
        id: n.id,
        type: (n.data as { toolType: string }).toolType,
        params: (n.data as { params: Record<string, unknown> }).params ?? {}
      })),
      edges: allEdges
        .filter(e => needed.has(e.source) && needed.has(e.target))
        .map(e => ({ source: e.source, target: e.target })),
      useCache: true,    // 파라미터 안 바뀐 상류는 캐시 재사용 (재실행 안 함)
      forceNode: nodeId  // 이 노드는 항상 재실행 (정확한 실행시간)
    }
    const res = await api.engineRun(recipe)
    if (res.error) setLogs(l => [...l, { level: 'error', msg: res.error! }])
  }, [setNodes])  // stable — reads latest via ref

  // 현재 열려있는(또는 마지막으로 저장한) 레시피 파일 경로. null이면 아직 저장 안 됨.
  const [recipePath, setRecipePath] = useState<string | null>(null)

  // 현재 그래프(노드/엣지/파라미터/위치)를 레시피 JSON 문자열로 직렬화
  const buildRecipeJson = useCallback(() => JSON.stringify({
    nodes: nodesRef.current.map(n => ({
      id: n.id,
      type: (n.data as { toolType: string }).toolType,
      label: (n.data as { label: string }).label,
      params: (n.data as { params: Record<string, unknown> }).params ?? {},
      position: n.position,
    })),
    edges: edgesRef.current.map(e => ({
      id: e.id, source: e.source, target: e.target,
      sourceHandle: e.sourceHandle, targetHandle: e.targetHandle,
    })),
  }, null, 2), [])

  const saveToPath = useCallback(async (path: string) => {
    const api = window.electronAPI
    if (!api?.saveRecipe) return
    await api.saveRecipe(path, buildRecipeJson())
    setRecipePath(path)
    setLogs(l => [...l, { level: 'info', msg: `레시피 저장됨: ${path}` }])
  }, [buildRecipeJson])

  // 다른 이름으로 저장 — 항상 경로를 새로 지정
  const handleSaveRecipeAs = useCallback(async () => {
    const api = window.electronAPI
    if (!api?.saveFile) return
    const path = await api.saveFile([{ name: 'Recipe', extensions: ['json'] }, { name: 'All Files', extensions: ['*'] }])
    if (path) await saveToPath(path)
  }, [saveToPath])

  // 저장(Ctrl+S) — 현재 파일 경로가 있으면 그 경로에 덮어쓰기, 없으면 다른 이름으로 저장
  const handleSaveRecipe = useCallback(async () => {
    if (recipePath) await saveToPath(recipePath)
    else await handleSaveRecipeAs()
  }, [recipePath, saveToPath, handleSaveRecipeAs])

  // 레시피 불러오기
  const handleLoadRecipe = useCallback(async () => {
    const api = window.electronAPI
    if (!api?.loadRecipe) return
    const path = await api.openFile([{ name: 'Recipe', extensions: ['json'] }, { name: 'All Files', extensions: ['*'] }])
    if (!path) return
    try {
      const content = await api.loadRecipe(path)
      const recipe = JSON.parse(content) as {
        nodes: { id: string; type: string; label: string; params: Record<string, unknown>; position: { x: number; y: number } }[]
        edges: { id: string; source: string; target: string; sourceHandle?: string | null; targetHandle?: string | null }[]
      }
      setNodes(recipe.nodes.map(n => ({
        id: n.id, type: 'toolNode', position: n.position ?? { x: 0, y: 0 },
        data: { label: n.label, toolType: n.type, params: n.params ?? {}, onRun: handleNodeRun },
      })))
      setEdges(recipe.edges.map(e => ({ ...e, animated: false })))
      setNodeResults({}); setResults([]); setOverallPass(null); setSelectedNodeId(null)
      setRecipePath(path)   // 이후 Ctrl+S는 이 경로에 덮어쓰기
      setLogs(l => [...l, { level: 'info', msg: `레시피 불러옴: ${path}` }])
    } catch (err) {
      setLogs(l => [...l, { level: 'error', msg: `레시피 로드 실패: ${String(err)}` }])
    }
  }, [setNodes, setEdges, handleNodeRun])

  // 단축키: Ctrl+S 저장(현재 경로), Ctrl+Shift+S 다른 이름으로 저장
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && (e.key === 's' || e.key === 'S')) {
        e.preventDefault()
        if (e.shiftKey) handleSaveRecipeAs()
        else handleSaveRecipe()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [handleSaveRecipe, handleSaveRecipeAs])

  // 엔진 강제 재시작 / 재연결
  const handleRestartEngine = useCallback(async () => {
    const api = window.electronAPI
    if (!api?.engineRestart) return
    setEngineConnected(false)
    setLogs(l => [...l, { level: 'info', msg: '엔진 재시작 중…' }])
    await api.engineRestart()
    setTimeout(async () => {
      const { connected } = await api.engineIsReady()
      setEngineConnected(connected)
      setLogs(l => [...l, {
        level: connected ? 'info' : 'error',
        msg: connected ? 'VisionEngine connected' : '엔진 연결 실패 — 빌드/포트(9000)를 확인하세요'
      }])
    }, 1500)
  }, [])


  const addNode = useCallback((toolType: string, label: string, position: { x: number; y: number }) => {
    const def = TOOL_DEF_MAP[toolType]
    setNodes((nds) => {
      // 기존 노드 최대 번호 +1 — HMR로 ref가 리셋돼도 ID 충돌 없음
      const maxId = nds.reduce((m, n) => Math.max(m, parseInt(n.id.split('-')[1]) || 0), 0)
      const id = `node-${maxId + 1}`
      return [...nds, {
        id,
        type: 'toolNode',
        position,
        data: { label, toolType, params: { ...def?.defaultParams }, onRun: handleNodeRun },
      }]
    })
  }, [setNodes, handleNodeRun])

  const onNodeClick: NodeMouseHandler = useCallback((_, node) => {
    setSelectedNodeId(node.id)
  }, [])

  const onPaneClick = useCallback(() => {
    if (!panelPinned) setSelectedNodeId(null)
  }, [panelPinned])

  const onParamChange = useCallback((nodeId: string, params: Record<string, unknown>) => {
    setNodes((nds) => nds.map(n =>
      n.id === nodeId ? { ...n, data: { ...n.data, params } } : n
    ))
  }, [setNodes])

  const handleRun = useCallback(async () => {
    if (!window.electronAPI) return
    setRunning(true)
    setResults([])
    setOverallPass(null)
    setNodeResults({})
    // Clear inline results from nodes
    setNodes(nds => nds.map(n => ({ ...n, data: { ...n.data, result: undefined } })))
    setLogs(l => [...l, { level: 'info', msg: `Run started (${nodes.length} nodes)` }])

    const recipe = {
      nodes: nodes.map(n => ({
        id: n.id,
        type: (n.data as { toolType: string }).toolType,
        params: (n.data as { params: Record<string, unknown> }).params ?? {}
      })),
      edges: edges.map(e => ({
        source: e.source,
        target: e.target
      }))
    }

    const res = await window.electronAPI.engineRun(recipe)
    if (res.error) {
      setRunning(false)
      setLogs(l => [...l, { level: 'error', msg: res.error! }])
    }
  }, [nodes, edges])

  const handleStop = useCallback(() => {
    setRunning(false)
    setLogs(l => [...l, { level: 'warn', msg: 'Stopped by user' }])
  }, [])

  // ── 폴더검사 ──
  // folder 모드인 ZMapLoader 노드 수집 (id, 라벨, 폴더). 여러 로더가 각자 폴더를 순회.
  const folderLoaderNodes = useCallback(() => nodesRef.current.filter(n => {
    const d = n.data as { toolType: string; params?: Record<string, unknown> }
    return d.toolType === 'ZMapLoader' && d.params?.mode === 'folder' && !!d.params?.folder
  }), [])

  // 폴더별 이미지 나열 후 정렬키(name/time) 적용
  const listSorted = useCallback(async (folder: string, key: BatchSortKey) => {
    const files = (await window.electronAPI?.listFolderImages(folder)) ?? []
    return key === 'time' ? [...files].sort((a, b) => a.mtimeMs - b.mtimeMs) : files  // name은 main에서 이미 정렬
  }, [])

  // 패널이 열려 있거나 그래프가 바뀌면 로더 목록/개수 갱신 (표시용)
  useEffect(() => {
    if (!batchOpen) return
    let cancelled = false
    const api = window.electronAPI
    if (!api) return
    const loaders = folderLoaderNodes()
    Promise.all(loaders.map(async n => {
      const d = n.data as { label: string; params: Record<string, unknown> }
      const files = await api.listFolderImages(d.params.folder as string)
      return { id: n.id, label: d.label, folder: d.params.folder as string, count: files.length }
    })).then(infos => { if (!cancelled) setBatchLoaders(infos) })
    return () => { cancelled = true }
  }, [batchOpen, nodes, folderLoaderNodes])

  const handleBatchStop = useCallback(() => {
    batchStopRef.current = true
    setLogs(l => [...l, { level: 'warn', msg: '폴더검사 중지 요청 — 현재 이미지 완료 후 멈춥니다' }])
  }, [])

  const handleBatchStart = useCallback(async () => {
    const api = window.electronAPI
    if (!api) return
    const allNodes = nodesRef.current
    const allEdges = edgesRef.current
    const loaderNodes = folderLoaderNodes()
    if (loaderNodes.length === 0) {
      setLogs(l => [...l, { level: 'error', msg: '폴더검사: 폴더 모드 ZMapLoader가 없습니다' }])
      return
    }

    // 각 로더의 폴더를 나열·정렬(name/time). 로더별 파일 배열을 인덱스로 락스텝 순회.
    const listed = await Promise.all(loaderNodes.map(async n => {
      const d = n.data as { params: Record<string, unknown> }
      return { id: n.id, files: await listSorted(d.params.folder as string, batchSortKey) }
    }))
    const counts = listed.map(l => l.files.length)
    const setCount = Math.min(...counts)
    if (setCount === 0) {
      setLogs(l => [...l, { level: 'error', msg: '폴더검사: 이미지가 없는 폴더가 있습니다' }])
      return
    }
    if (new Set(counts).size > 1) {
      setLogs(l => [...l, { level: 'warn', msg: `폴더별 이미지 개수가 다릅니다 (${counts.join('/')}) — 최소 ${setCount}개까지만 검사` }])
    }
    const pathById = (i: number) => new Map(listed.map(l => [l.id, l.files[i].path]))

    const csv = allNodes.find(n => (n.data as { toolType: string }).toolType === 'CsvWriter')
    const baseEdges = allEdges.map(e => ({ source: e.source, target: e.target }))

    const buildRecipe = (i: number) => {
      const paths = pathById(i)
      // CSV 행 라벨 = 첫 로더의 i번째 파일명
      const setLabel = listed[0].files[i].name
      const recipe = {
        nodes: allNodes.map(n => {
          const params = { ...((n.data as { params?: Record<string, unknown> }).params ?? {}) }
          if (paths.has(n.id)) params.path = paths.get(n.id)   // folder 로더는 i번째 파일로
          if (csv && n.id === csv.id) params.label = setLabel
          return { id: n.id, type: (n.data as { toolType: string }).toolType, params }
        }),
        edges: baseEdges,
        // 디스플레이 갱신 OFF면 미리보기 생략(엔진 인코딩/z스캔 생략) → 가속
        noPreview: !batchPreview,
      }
      return { recipe, setLabel }
    }

    batchStopRef.current = false
    setBatchRunning(true)
    setRunning(true)
    setBatchResults([])
    setLogs(l => [...l, { level: 'info', msg: `폴더검사 시작 — 로더 ${loaderNodes.length}개 · 세트 ${setCount}개${batchRepeat ? ' (무한반복)' : ''}` }])

    // 무한반복 모드는 기존 단일 엔진(인터랙티브용) 순차 방식 그대로 사용
    if (batchRepeat) {
      const runOnce = (recipe: unknown) =>
        new Promise<{ pass: boolean; totalMs: number; error?: boolean }>((resolve) => {
          pendingDoneRef.current = resolve
          api.engineRun(recipe).then(res => {
            if (res.error && pendingDoneRef.current === resolve) {
              pendingDoneRef.current = null
              setLogs(l => [...l, { level: 'error', msg: res.error! }])
              resolve({ pass: false, totalMs: 0, error: true })
            }
          })
        })

      let cycle = 0
      do {
        cycle++
        setBatchCycle(cycle)
        for (let i = 0; i < setCount; i++) {
          if (batchStopRef.current) break
          setBatchIndex(i)
          const { recipe, setLabel } = buildRecipe(i)
          const r = await runOnce(recipe)
          setBatchResults(prev => [...prev, {
            file: setLabel, pass: r.error ? null : r.pass, totalMs: r.totalMs, error: r.error,
          }])
        }
      } while (batchRepeat && !batchStopRef.current)

      if (!batchStopRef.current) setBatchIndex(setCount)
      setBatchRunning(false)
      setRunning(false)
      setLogs(l => [...l, { level: 'info', msg: '폴더검사 완료' }])
      return
    }

    // ── 병렬 워커풀 경로 (무한반복 아닐 때) ──────────────────────────────
    const startRes = await api.batchStart()
    if (startRes.error || !startRes.workerIds?.length) {
      setLogs(l => [...l, { level: 'error', msg: `워커풀 시작 실패: ${startRes.error ?? '알 수 없는 오류'}` }])
      setBatchRunning(false)
      setRunning(false)
      return
    }
    const workerIds = startRes.workerIds
    const N = workerIds.length
    setLogs(l => [...l, { level: 'info', msg: `워커 ${N}개 기동 완료 — 병렬 검사 시작` }])

    const resById = new Map(listed.map(l => {
      const nd = allNodes.find(n => n.id === l.id)
      const p = (nd?.data as { params?: Record<string, unknown> })?.params ?? {}
      return [l.id, {
        xResMm: (p.xResMm as number) ?? 1.0,
        yResMm: (p.yResMm as number) ?? 1.0,
        zResMm: (p.zResMm as number) ?? 0.001,
      }]
    }))

    const pendingByWorker = new Map<number, (r: { pass: boolean; totalMs: number; error?: boolean; died?: boolean }) => void>()
    // 배치 도중 완전히 포기한 워커 슬롯(재기동 한도 초과) — 남은 워커만으로 계속 진행하기 위한 집합
    const aliveWorkers = new Set(workerIds)
    // 죽은 워커가 못다한 몫을 살아있는 다른 워커가 대신 처리하도록 넘겨받는 큐
    const strandedQueue: number[] = []

    const unsub = api.onBatchEvent((raw) => {
      const data = raw as Record<string, unknown>
      const workerId = data.workerId as number
      const event = data.event as string
      if (event === 'log') {
        setLogs(l => [...l, { level: (data.level as LogEntry['level']) ?? 'info', msg: `[w${workerId}] ${data.msg}` }])
        return
      }
      if (event === 'result' && data.ok === false) {
        setLogs(l => [...l, { level: 'error', msg: `[w${workerId}] ${data.tool}[${data.id}] 실패: ${data.msg}` }])
        return
      }
      if (event === 'workerExit') {
        setLogs(l => [...l, { level: 'error', msg: `워커 ${workerId} 비정상 종료 — 재기동 시도` }])
        const resolve = pendingByWorker.get(workerId)
        if (resolve) { pendingByWorker.delete(workerId); resolve({ pass: false, totalMs: 0, error: true, died: true }) }
        return
      }
      if (event === 'done' || event === 'error') {
        const resolve = pendingByWorker.get(workerId)
        if (!resolve) return
        pendingByWorker.delete(workerId)
        if (event === 'error') {
          setLogs(l => [...l, { level: 'error', msg: `[w${workerId}] ${data.msg}` }])
          resolve({ pass: false, totalMs: 0, error: true })
        } else {
          resolve({ pass: data.pass as boolean, totalMs: (data.totalMs as number) ?? 0 })
        }
      }
    })

    let completed = 0

    // 워커 workerId에게 i번째 이미지를 지금부터 백그라운드로 미리 로드해두게 함
    const prefetchFor = (workerId: number, i: number) => {
      if (i >= setCount) return
      const paths = pathById(i)
      for (const l of listed) {
        const p = paths.get(l.id)
        if (!p) continue
        const res = resById.get(l.id)!
        api.batchPrefetch(workerId, p, res.xResMm, res.yResMm, res.zResMm)
      }
    }

    const runOnWorker = (workerId: number, i: number, nextForThisWorker: number, recipe: unknown) =>
      new Promise<{ pass: boolean; totalMs: number; error?: boolean; died?: boolean }>((resolve) => {
        pendingByWorker.set(workerId, resolve)
        // 이 워커가 이어서 처리할 걸로 예정된 이미지를 지금부터 미리 로드 — 워커별 고정 배정
        // 덕분에 "다음에 내가 뭘 처리할지"가 정확해서 프리페치가 항상 적중한다(work-stealing 방식은
        // 다음 인덱스를 다른 워커가 가져가버려 프리페치가 엉뚱한 프로세스 캐시만 데우는 문제가 있었음).
        prefetchFor(workerId, nextForThisWorker)
        api.batchRun(workerId, recipe).then(res => {
          if (res.error && pendingByWorker.get(workerId) === resolve) {
            pendingByWorker.delete(workerId)
            setLogs(l => [...l, { level: 'error', msg: res.error! }])
            resolve({ pass: false, totalMs: 0, error: true })
          }
        })
      })

    // 워커 k는 인덱스 k, k+N, k+2N, ...을 고정으로 담당(라운드로빈 배정이 아니라 고정 배정).
    // 자기 몫을 다 마쳤거나 다른 워커가 못다한 몫(strandedQueue)이 있으면 그걸 이어받는다.
    const workerLoop = async (workerId: number, startSlot: number) => {
      // 모든 워커가 정확히 동시에 첫 이미지를 콜드 로드하며 부딪히는 "파도" 현상을 피하려고
      // 시작 시점을 살짝 어긋나게 둔다 — 이후로는 처리시간차 덕분에 자연스럽게 계속 어긋난 채로 흐른다.
      await new Promise(r => setTimeout(r, startSlot * 250))
      let i = startSlot
      while (!batchStopRef.current) {
        if (i >= setCount) {
          if (strandedQueue.length === 0) break
          i = strandedQueue.shift()!
        }
        const { recipe, setLabel } = buildRecipe(i)
        const nextForThisWorker = strandedQueue.length > 0 ? strandedQueue[0] : i + N
        const r = await runOnWorker(workerId, i, nextForThisWorker, recipe)

        if (r.died) {
          if (!aliveWorkers.has(workerId)) break
          const resp = await api.batchRespawn(workerId)
          if (resp.error) {
            aliveWorkers.delete(workerId)
            setLogs(l => [...l, {
              level: 'error',
              msg: `워커 ${workerId} 재기동 불가 — 이 슬롯은 제외하고 남은 워커 ${aliveWorkers.size}개로 계속 진행`
            }])
            setBatchResults(prev => [...prev, { file: setLabel, pass: null, totalMs: 0, error: true }])
            completed++
            setBatchIndex(completed)
            // 이 워커가 앞으로 맡았을 나머지 몫은 살아있는 워커들이 대신 처리하도록 큐에 넘김
            for (let j = i + N; j < setCount; j += N) strandedQueue.push(j)
            break
          }
          continue   // 재기동 성공 — 같은 이미지를 재시도 (아직 completed 처리 안 함)
        }

        completed++
        setBatchIndex(completed)
        setBatchResults(prev => [...prev, {
          file: setLabel, pass: r.error ? null : r.pass, totalMs: r.totalMs, error: r.error,
        }])
        i = strandedQueue.length > 0 ? strandedQueue.shift()! : i + N
      }
    }

    await Promise.all(workerIds.map((id, slot) => workerLoop(id, slot)))
    unsub()
    await api.batchStop()

    if (!batchStopRef.current) setBatchIndex(setCount)
    setBatchRunning(false)
    setRunning(false)
    const allDead = aliveWorkers.size === 0
    setLogs(l => [...l, { level: 'info', msg: allDead ? '폴더검사 중단됨 (모든 워커 종료)' : '폴더검사 완료' }])
  }, [folderLoaderNodes, listSorted, batchSortKey, batchRepeat, batchPreview])

  const selectedNode = nodes.find(n => n.id === selectedNodeId)

  // PlaneFit / HeightMeasure / LineCenter 노드용: ZMap 입력(input-0) 소스의 결과를 ROI 에디터 배경으로
  const upstreamRes = (() => {
    if (!selectedNode) return undefined
    const tt = (selectedNode.data as { toolType: string }).toolType
    if (tt !== 'PlaneFit' && tt !== 'HeightMeasure' && tt !== 'LineCenter' && tt !== 'Align' && tt !== 'NoiseFilter') return undefined
    const tEdges = edges.filter(e => e.target === selectedNode.id)
    if (tEdges.length === 0) return undefined
    // ZMap 입력 포트(input-0) 엣지 우선, 없으면 첫 엣지
    const zEdge = tEdges.find(e => (e.targetHandle ?? 'input-0') === 'input-0') ?? tEdges[0]
    return nodeResults[zEdge.source] as
      { preview?: string; zMin?: number; zMax?: number; xResMm?: number; yResMm?: number; offCol?: number; offRow?: number } | undefined
  })()
  const upstreamPreview = upstreamRes?.preview
  const upstreamZMin = upstreamRes?.zMin
  const upstreamZMax = upstreamRes?.zMax
  const upstreamResX = upstreamRes?.xResMm
  const upstreamResY = upstreamRes?.yResMm
  const upstreamOriginCol = upstreamRes?.offCol
  const upstreamOriginRow = upstreamRes?.offRow

  return (
    <div className="app">
      <Toolbar
        running={running}
        engineConnected={engineConnected}
        onRun={handleRun}
        onStop={handleStop}
        onToggleResult={() => setResultVisible(v => !v)}
        onSaveRecipe={handleSaveRecipe}
        onSaveRecipeAs={handleSaveRecipeAs}
        recipeName={recipePath ? recipePath.split(/[\\/]/).pop()! : null}
        onLoadRecipe={handleLoadRecipe}
        onRestartEngine={handleRestartEngine}
        onOpenFolderInspect={() => setBatchOpen(true)}
      />
      <div className="workspace">
        <ToolboxPanel />
        <HoveredEdgeContext.Provider value={hoveredEdge}>
          <NodeCanvas
            nodes={nodes}
            edges={edges}
            onNodesChange={onNodesChange}
            onEdgesChange={onEdgesChange}
            onConnect={onConnect}
            onAddNode={addNode}
            onNodeClick={onNodeClick}
            onPaneClick={onPaneClick}
            onEdgeMouseEnter={(_, edge) => setHoveredEdge({
              source: edge.source, sourceHandle: edge.sourceHandle,
              target: edge.target, targetHandle: edge.targetHandle,
            })}
            onEdgeMouseLeave={() => setHoveredEdge(null)}
          />
        </HoveredEdgeContext.Provider>
        {selectedNode && (
          <NodePanel
            nodeId={selectedNode.id}
            toolType={(selectedNode.data as { toolType: string }).toolType}
            label={(selectedNode.data as { label: string }).label}
            params={(selectedNode.data as { params: Record<string, unknown> }).params ?? {}}
            result={nodeResults[selectedNode.id] as Record<string, unknown> | undefined}
            upstreamPreview={upstreamPreview}
            upstreamZMin={upstreamZMin}
            upstreamZMax={upstreamZMax}
            upstreamResX={upstreamResX}
            upstreamResY={upstreamResY}
            upstreamOriginCol={upstreamOriginCol}
            upstreamOriginRow={upstreamOriginRow}
            width={panelWidth}
            onWidthChange={setPanelWidth}
            onParamChange={onParamChange}
            onRun={handleNodeRun}
            pinned={panelPinned}
            onTogglePin={() => setPanelPinned(v => !v)}
            onClose={() => { setSelectedNodeId(null); setPanelPinned(false) }}
          />
        )}
      </div>
      {batchOpen && (
        <FolderInspectPanel
          running={batchRunning}
          loaders={batchLoaders}
          sortKey={batchSortKey}
          index={batchIndex}
          cycle={batchCycle}
          repeat={batchRepeat}
          showPreview={batchPreview}
          results={batchResults}
          onSortKey={setBatchSortKey}
          onToggleRepeat={setBatchRepeat}
          onTogglePreview={setBatchPreview}
          onStart={handleBatchStart}
          onStop={handleBatchStop}
          onClose={() => { if (!batchRunning) setBatchOpen(false) }}
        />
      )}
      {resultVisible && (
        <ResultPanel
          results={results}
          logs={logs}
          overallPass={overallPass}
          onClear={() => { setResults([]); setLogs([]); setOverallPass(null) }}
        />
      )}
      <div className="statusbar">
        <span className="status-item">
          {engineConnected ? '● Engine 연결됨' : '○ Engine 대기 중'}
        </span>
        <span className="status-item">노드 {nodes.length}개</span>
        {selectedNode && (() => {
          const r = nodeResults[selectedNode.id] as { elapsedMs?: number } | undefined
          return r?.elapsedMs !== undefined
            ? <span className="status-item status-time">⏱ {(selectedNode.data as { label: string }).label}: {Math.round(r.elapsedMs)} ms</span>
            : null
        })()}
        {lastTotalMs !== null && (
          <span className="status-item status-time">전체: {Math.round(lastTotalMs)} ms</span>
        )}
        <span className="status-item status-ready">
          {running ? 'Running...' : 'Ready'}
        </span>
      </div>
    </div>
  )
}
