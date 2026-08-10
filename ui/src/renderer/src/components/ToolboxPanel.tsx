import { useState } from 'react'
import { TOOL_DEFS, ToolDef } from '../types/tools'

function groupByCategory() {
  const map: Record<string, ToolDef[]> = {}
  for (const t of TOOL_DEFS) {
    if (!map[t.category]) map[t.category] = []
    map[t.category].push(t)
  }
  return map
}

interface TooltipState { text: string; x: number; y: number }

export default function ToolboxPanel() {
  const categories = groupByCategory()
  const [tooltip, setTooltip] = useState<TooltipState | null>(null)

  const onDragStart = (e: React.DragEvent, toolType: string, label: string) => {
    e.dataTransfer.setData('toolType', toolType)
    e.dataTransfer.setData('toolLabel', label)
    e.dataTransfer.effectAllowed = 'move'
    setTooltip(null)
  }

  const showTooltip = (e: React.MouseEvent, text: string) => {
    const rect = (e.currentTarget as HTMLElement).getBoundingClientRect()
    setTooltip({ text, x: rect.right + 8, y: rect.top + rect.height / 2 })
  }

  return (
    <div className="toolbox-panel">
      <div className="panel-header">툴박스</div>
      <div className="toolbox-list">
        {Object.entries(categories).map(([catName, tools]) => (
          <div key={catName} className="toolbox-category">
            <div className="category-label">{catName}</div>
            {tools.map((tool) => {
              const tip = tool.tooltip ?? tool.description ?? ''
              return (
                <div
                  key={tool.type}
                  className="toolbox-item"
                  draggable
                  onDragStart={(e) => onDragStart(e, tool.type, tool.label)}
                  onMouseEnter={(e) => tip && showTooltip(e, tip)}
                  onMouseLeave={() => setTooltip(null)}
                >
                  {tool.label}
                </div>
              )
            })}
          </div>
        ))}
      </div>
      {tooltip && (
        <div
          className="toolbox-tooltip"
          style={{ left: tooltip.x, top: tooltip.y }}
        >
          {tooltip.text}
        </div>
      )}
    </div>
  )
}
