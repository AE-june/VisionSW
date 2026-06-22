import { TOOL_DEFS } from '../types/tools'

function groupByCategory() {
  const map: Record<string, { type: string; label: string }[]> = {}
  for (const t of TOOL_DEFS) {
    if (!map[t.category]) map[t.category] = []
    map[t.category].push({ type: t.type, label: t.label })
  }
  return map
}

export default function ToolboxPanel() {
  const categories = groupByCategory()
  const onDragStart = (e: React.DragEvent, toolType: string, label: string) => {
    e.dataTransfer.setData('toolType', toolType)
    e.dataTransfer.setData('toolLabel', label)
    e.dataTransfer.effectAllowed = 'move'
  }

  return (
    <div className="toolbox-panel">
      <div className="panel-header">툴박스</div>
      <div className="toolbox-list">
        {Object.entries(categories).map(([catName, tools]) => (
          <div key={catName} className="toolbox-category">
            <div className="category-label">{catName}</div>
            {tools.map((tool) => (
              <div
                key={tool.type}
                className="toolbox-item"
                draggable
                onDragStart={(e) => onDragStart(e, tool.type, tool.label)}
                title={`${tool.label} 노드 추가`}
              >
                {tool.label}
              </div>
            ))}
          </div>
        ))}
      </div>
    </div>
  )
}
