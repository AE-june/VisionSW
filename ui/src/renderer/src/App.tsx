import { useState, useRef, useCallback, useEffect } from 'react'
import { addEdge, useNodesState, useEdgesState } from '@xyflow/react'
import type { Connection, Node, Edge, NodeMouseHandler } from '@xyflow/react'
import Toolbar from './components/Toolbar'
import ToolboxPanel from './components/ToolboxPanel'
import NodeCanvas from './components/NodeCanvas'
import ResultPanel from './components/ResultPanel'
import type { MeasureResult, LogEntry } from './components/ResultPanel'
import NodePanel from './components/NodePanel'
import { TOOL_DEF_MAP } from './types/tools'
import './App.css'

declare global {
  interface Window {
    electronAPI?: {
      openFile: (filters?: Electron.FileFilter[]) => Promise<string | null>
      engineRun: (recipe: unknown) => Promise<{ ok?: boolean; error?: string }>
      enginePing: () => Promise<{ connected: boolean }>
      engineIsReady: () => Promise<{ connected: boolean }>
      engineRestart: () => Promise<{ ok?: boolean }>
      onEngineEvent: (cb: (data: unknown) => void) => () => void
    }
  }
}

const initialNodes: Node[] = []
const initialEdges: Edge[] = []

export default function App() {
  const [resultVisible, setResultVisible] = useState(true)
  const [nodes, setNodes, onNodesChange] = useNodesState(initialNodes)
  const [edges, setEdges, onEdgesChange] = useEdgesState(initialEdges)
  const [selectedNodeId, setSelectedNodeId] = useState<string | null>(null)
  const [running, setRunning] = useState(false)
  const [engineConnected, setEngineConnected] = useState(false)
  const [results, setResults] = useState<MeasureResult[]>([])
  const [logs, setLogs] = useState<LogEntry[]>([])
  const [overallPass, setOverallPass] = useState<boolean | null>(null)
  const [nodeResults, setNodeResults] = useState<Record<string, Record<string, unknown>>>({})
  const nodeIdRef = useRef(1)
  const nodesRef = useRef(nodes)
  const edgesRef = useRef(edges)
  useEffect(() => { nodesRef.current = nodes }, [nodes])
  useEffect(() => { edgesRef.current = edges }, [edges])

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
        setRunning(false)
        setOverallPass(data.pass as boolean)
        setLogs(l => [...l, {
          level: 'info',
          msg: `Pipeline done — ${data.pass ? 'PASS' : 'FAIL'}`
        }])
      }
      if (event === 'error') {
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
        .map(e => ({ source: e.source, target: e.target }))
    }
    const res = await api.engineRun(recipe)
    if (res.error) setLogs(l => [...l, { level: 'error', msg: res.error! }])
  }, [setNodes])  // stable — reads latest via ref

  const addNode = useCallback((toolType: string, label: string, position: { x: number; y: number }) => {
    const id = `node-${nodeIdRef.current++}`
    const def = TOOL_DEF_MAP[toolType]
    setNodes((nds) => [...nds, {
      id,
      type: 'toolNode',
      position,
      data: { label, toolType, params: { ...def?.defaultParams }, onRun: handleNodeRun },
    }])
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

  const handleOpenFile = useCallback(async () => {
    if (!window.electronAPI) return
    const path = await window.electronAPI.openFile([
      { name: 'ZMap', extensions: ['zmap', 'bin'] },
      { name: 'Image', extensions: ['png', 'jpg', 'bmp'] },
    ])
    if (!path) return
    // If a loader node is selected, update its path param
    if (selectedNodeId) {
      const node = nodes.find(n => n.id === selectedNodeId)
      const toolType = (node?.data as { toolType?: string })?.toolType
      if (toolType === 'ZMapLoader' || toolType === 'ImageLoader') {
        onParamChange(selectedNodeId, {
          ...(node?.data as { params: Record<string, unknown> }).params,
          path
        })
      }
    }
  }, [selectedNodeId, nodes, onParamChange])

  const selectedNode = nodes.find(n => n.id === selectedNodeId)

  // PlaneFit / HeightFromPlane 노드용: ZMap 입력(input-0) 소스의 결과를 ROI 에디터 배경으로
  const upstreamRes = (() => {
    if (!selectedNode) return undefined
    const tt = (selectedNode.data as { toolType: string }).toolType
    if (tt !== 'PlaneFit' && tt !== 'HeightMeasure') return undefined
    const tEdges = edges.filter(e => e.target === selectedNode.id)
    if (tEdges.length === 0) return undefined
    // ZMap 입력 포트(input-0) 엣지 우선, 없으면 첫 엣지
    const zEdge = tEdges.find(e => (e.targetHandle ?? 'input-0') === 'input-0') ?? tEdges[0]
    return nodeResults[zEdge.source] as { preview?: string; zMin?: number; zMax?: number } | undefined
  })()
  const upstreamPreview = upstreamRes?.preview
  const upstreamZMin = upstreamRes?.zMin
  const upstreamZMax = upstreamRes?.zMax

  return (
    <div className="app">
      <Toolbar
        running={running}
        engineConnected={engineConnected}
        onRun={handleRun}
        onStop={handleStop}
        onToggleResult={() => setResultVisible(v => !v)}
        onOpenFile={handleOpenFile}
        onRestartEngine={handleRestartEngine}
      />
      <div className="workspace">
        <ToolboxPanel />
        <NodeCanvas
          nodes={nodes}
          edges={edges}
          onNodesChange={onNodesChange}
          onEdgesChange={onEdgesChange}
          onConnect={onConnect}
          onAddNode={addNode}
          onNodeClick={onNodeClick}
          onPaneClick={onPaneClick}
        />
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
            onParamChange={onParamChange}
            onClose={() => setSelectedNodeId(null)}
          />
        )}
      </div>
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
        <span className="status-item status-ready">
          {running ? 'Running...' : 'Ready'}
        </span>
      </div>
    </div>
  )
}
