import { useCallback, useRef } from 'react'
import {
  ReactFlow,
  Background,
  Controls,
  MiniMap,
  useReactFlow,
  ReactFlowProvider,
} from '@xyflow/react'
import type { Node, Edge, OnNodesChange, OnEdgesChange, Connection, NodeMouseHandler } from '@xyflow/react'
import '@xyflow/react/dist/style.css'
import ToolNode from './ToolNode'
import { TOOL_DEF_MAP } from '../types/tools'
import type { PortType } from '../types/tools'

const nodeTypes = { toolNode: ToolNode }

interface Props {
  nodes: Node[]
  edges: Edge[]
  onNodesChange: OnNodesChange
  onEdgesChange: OnEdgesChange
  onConnect: (params: Connection) => void
  onAddNode: (toolType: string, label: string, position: { x: number; y: number }) => void
  onNodeClick: NodeMouseHandler
  onPaneClick: () => void
  onEdgeMouseEnter?: (event: React.MouseEvent, edge: Edge) => void
  onEdgeMouseLeave?: (event: React.MouseEvent, edge: Edge) => void
}

function isCompatible(a: PortType, b: PortType) {
  return a === 'Any' || b === 'Any' || a === b
}

function Canvas({ nodes, edges, onNodesChange, onEdgesChange, onConnect, onAddNode, onNodeClick, onPaneClick, onEdgeMouseEnter, onEdgeMouseLeave }: Props) {
  const { screenToFlowPosition } = useReactFlow()
  const canvasRef = useRef<HTMLDivElement>(null)

  const isValidConnection = useCallback((connection: Connection) => {
    const sourceNode = nodes.find(n => n.id === connection.source)
    const targetNode = nodes.find(n => n.id === connection.target)
    if (!sourceNode || !targetNode) return false

    const sourceDef = TOOL_DEF_MAP[(sourceNode.data as { toolType: string }).toolType]
    const targetDef = TOOL_DEF_MAP[(targetNode.data as { toolType: string }).toolType]
    if (!sourceDef || !targetDef) return false

    const outIdx = parseInt((connection.sourceHandle ?? 'output-0').split('-')[1])
    const inIdx  = parseInt((connection.targetHandle ?? 'input-0').split('-')[1])

    const outType = sourceDef.outputs[outIdx]
    const inType  = targetDef.inputs[inIdx]
    if (!outType || !inType) return false

    return isCompatible(outType, inType)
  }, [nodes])

  const onDragOver = useCallback((e: React.DragEvent) => {
    e.preventDefault()
    e.dataTransfer.dropEffect = 'move'
  }, [])

  const onDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault()
    const toolType = e.dataTransfer.getData('toolType')
    const label    = e.dataTransfer.getData('toolLabel')
    if (!toolType) return
    const position = screenToFlowPosition({ x: e.clientX, y: e.clientY })
    onAddNode(toolType, label, position)
  }, [screenToFlowPosition, onAddNode])

  return (
    <div className="node-canvas" ref={canvasRef}>
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={nodeTypes}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onConnect={onConnect}
        onDragOver={onDragOver}
        onDrop={onDrop}
        onNodeClick={onNodeClick}
        onPaneClick={onPaneClick}
        onEdgeMouseEnter={onEdgeMouseEnter}
        onEdgeMouseLeave={onEdgeMouseLeave}
        isValidConnection={isValidConnection}
        fitView
        deleteKeyCode="Delete"
      >
        <Background color="#2a2a2a" gap={20} />
        <Controls />
        <MiniMap
          style={{ background: '#1e1e1e', border: '1px solid #333' }}
          nodeColor="#00bcd4"
          maskColor="rgba(0,0,0,0.4)"
        />
      </ReactFlow>
    </div>
  )
}

export default function NodeCanvas(props: Props) {
  return (
    <ReactFlowProvider>
      <Canvas {...props} />
    </ReactFlowProvider>
  )
}
