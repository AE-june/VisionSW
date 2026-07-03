import { useState, useRef, useCallback, useEffect } from 'react'
import { addEdge, useNodesState, useEdgesState } from '@xyflow/react'
import type { Connection, Node, Edge, NodeMouseHandler } from '@xyflow/react'
import Toolbar from './components/Toolbar'
import ToolboxPanel from './components/ToolboxPanel'
import NodeCanvas from './components/NodeCanvas'
import ResultPanel from './components/ResultPanel'
import type { MeasureResult, LogEntry } from './components/ResultPanel'
import NodePanel from './components/NodePanel'
import FolderInspectPanel, { type BatchFile, type BatchResult } from './components/FolderInspectPanel'
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
      listFolderImages: (dir: string) => Promise<{ name: string; path: string }[]>
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
  const [batchFolder, setBatchFolder] = useState<string | null>(null)
  const [batchFiles, setBatchFiles] = useState<BatchFile[]>([])
  const [batchIndex, setBatchIndex] = useState(0)
  const [batchCycle, setBatchCycle] = useState(1)
  const [batchRepeat, setBatchRepeat] = useState(false)
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

  // 레시피 저장 — 노드/엣지/파라미터/위치를 JSON으로
  const handleSaveRecipe = useCallback(async () => {
    const api = window.electronAPI
    if (!api?.saveFile) return
    const path = await api.saveFile([{ name: 'Recipe', extensions: ['json'] }, { name: 'All Files', extensions: ['*'] }])
    if (!path) return
    const recipe = {
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
    }
    await api.saveRecipe(path, JSON.stringify(recipe, null, 2))
    setLogs(l => [...l, { level: 'info', msg: `레시피 저장됨: ${path}` }])
  }, [])

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
      setLogs(l => [...l, { level: 'info', msg: `레시피 불러옴: ${path}` }])
    } catch (err) {
      setLogs(l => [...l, { level: 'error', msg: `레시피 로드 실패: ${String(err)}` }])
    }
  }, [setNodes, setEdges, handleNodeRun])

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
    setSelectedNodeId(null)
  }, [])

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
  const handlePickFolder = useCallback(async () => {
    const api = window.electronAPI
    if (!api?.openFolder) return
    const dir = await api.openFolder()
    if (!dir) return
    const files = await api.listFolderImages(dir)
    setBatchFolder(dir)
    setBatchFiles(files)
    setBatchResults([])
    setLogs(l => [...l, { level: 'info', msg: `폴더 선택: ${dir} (이미지 ${files.length}개)` }])
  }, [])

  const handleBatchStop = useCallback(() => {
    batchStopRef.current = true
    setLogs(l => [...l, { level: 'warn', msg: '폴더검사 중지 요청 — 현재 이미지 완료 후 멈춥니다' }])
  }, [])

  const handleBatchStart = useCallback(async () => {
    const api = window.electronAPI
    if (!api || batchFiles.length === 0) return
    const allNodes = nodesRef.current
    const allEdges = edgesRef.current
    const zmap = allNodes.find(n => (n.data as { toolType: string }).toolType === 'ZMapLoader')
    if (!zmap) {
      setLogs(l => [...l, { level: 'error', msg: '폴더검사: ZMapLoader 노드가 없습니다' }])
      return
    }
    const csv = allNodes.find(n => (n.data as { toolType: string }).toolType === 'CsvWriter')
    const baseEdges = allEdges.map(e => ({ source: e.source, target: e.target }))

    batchStopRef.current = false
    setBatchRunning(true)
    setRunning(true)
    setBatchResults([])
    setLogs(l => [...l, { level: 'info', msg: `폴더검사 시작 — ${batchFiles.length}개${batchRepeat ? ' (무한반복)' : ''}` }])

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
      for (let i = 0; i < batchFiles.length; i++) {
        if (batchStopRef.current) break
        const file = batchFiles[i]
        setBatchIndex(i)
        const recipe = {
          nodes: allNodes.map(n => {
            const params = { ...((n.data as { params?: Record<string, unknown> }).params ?? {}) }
            if (n.id === zmap.id) params.path = file.path
            if (csv && n.id === csv.id) params.label = file.name
            return { id: n.id, type: (n.data as { toolType: string }).toolType, params }
          }),
          edges: baseEdges,
          noPreview: true,   // 배치 검사: 미리보기 생략(엔진 인코딩/z스캔 생략) → 가속
        }
        const r = await runOnce(recipe)
        setBatchResults(prev => [...prev, {
          file: file.name, pass: r.error ? null : r.pass, totalMs: r.totalMs, error: r.error,
        }])
      }
    } while (batchRepeat && !batchStopRef.current)

    setBatchRunning(false)
    setRunning(false)
    setLogs(l => [...l, { level: 'info', msg: '폴더검사 완료' }])
  }, [batchFiles, batchRepeat])

  const hasZMapLoader = nodes.some(n => (n.data as { toolType: string }).toolType === 'ZMapLoader')

  const selectedNode = nodes.find(n => n.id === selectedNodeId)

  // PlaneFit / HeightMeasure / LineCenter 노드용: ZMap 입력(input-0) 소스의 결과를 ROI 에디터 배경으로
  const upstreamRes = (() => {
    if (!selectedNode) return undefined
    const tt = (selectedNode.data as { toolType: string }).toolType
    if (tt !== 'PlaneFit' && tt !== 'HeightMeasure' && tt !== 'LineCenter' && tt !== 'Align') return undefined
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
            onClose={() => setSelectedNodeId(null)}
          />
        )}
      </div>
      {batchOpen && (
        <FolderInspectPanel
          running={batchRunning}
          folder={batchFolder}
          files={batchFiles}
          index={batchIndex}
          cycle={batchCycle}
          repeat={batchRepeat}
          results={batchResults}
          hasZMap={hasZMapLoader}
          onPickFolder={handlePickFolder}
          onToggleRepeat={setBatchRepeat}
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
