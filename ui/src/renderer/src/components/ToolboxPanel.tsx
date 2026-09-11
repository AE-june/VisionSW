import { useState, useMemo } from 'react'
import { TOOL_DEFS, ToolDef } from '../types/tools'

const CATEGORY_ORDER = [
  '입수', '전처리', '기준/정렬', '영역', '프로파일',
  '측정', '표면 연산', '변환', '판정', '출력', 'SDC 전용',
]

function groupByCategory(tools: ToolDef[]): [string, ToolDef[]][] {
  const map: Record<string, ToolDef[]> = {}
  for (const t of tools) {
    if (!map[t.category]) map[t.category] = []
    map[t.category].push(t)
  }
  const ordered: [string, ToolDef[]][] = []
  for (const cat of CATEGORY_ORDER) {
    if (map[cat]) ordered.push([cat, map[cat]])
  }
  for (const [cat, ts] of Object.entries(map)) {
    if (!CATEGORY_ORDER.includes(cat)) ordered.push([cat, ts])
  }
  return ordered
}

interface TooltipState { text: string; x: number; y: number }

export default function ToolboxPanel() {
  const [tooltip, setTooltip] = useState<TooltipState | null>(null)
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set())

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase()
    if (!q) return TOOL_DEFS
    return TOOL_DEFS.filter(t =>
      t.label.toLowerCase().includes(q) ||
      t.type.toLowerCase().includes(q) ||
      (t.description ?? '').toLowerCase().includes(q) ||
      (t.tooltip ?? '').toLowerCase().includes(q)
    )
  }, [query])

  const categories = useMemo(() => groupByCategory(filtered), [filtered])

  const toggleCat = (cat: string) => {
    setCollapsed(prev => {
      const next = new Set(prev)
      next.has(cat) ? next.delete(cat) : next.add(cat)
      return next
    })
  }

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

      <div className="toolbox-search">
        <input
          className="toolbox-search-input"
          type="text"
          placeholder="툴 검색..."
          value={query}
          onChange={e => setQuery(e.target.value)}
        />
        {query && (
          <button className="toolbox-search-clear" onClick={() => setQuery('')}>✕</button>
        )}
      </div>

      <div className="toolbox-list">
        {categories.length === 0 && (
          <div className="toolbox-no-result">검색 결과 없음</div>
        )}
        {categories.map(([catName, tools]) => {
          const open = query || !collapsed.has(catName)
          return (
            <div key={catName} className="toolbox-category">
              <div className="category-label" onClick={() => toggleCat(catName)}>
                <span className="category-arrow">{open ? '▾' : '▸'}</span>
                <span className="category-name">{catName}</span>
                <span className="category-count">{tools.length}</span>
              </div>
              {open && tools.map(tool => {
                const tip = tool.tooltip ?? tool.description ?? ''
                return (
                  <div
                    key={tool.type}
                    className="toolbox-item"
                    draggable
                    onDragStart={e => onDragStart(e, tool.type, tool.label)}
                    onMouseEnter={e => tip && showTooltip(e, tip)}
                    onMouseLeave={() => setTooltip(null)}
                  >
                    {tool.label}
                  </div>
                )
              })}
            </div>
          )
        })}
      </div>

      {tooltip && (
        <div className="toolbox-tooltip" style={{ left: tooltip.x, top: tooltip.y }}>
          {tooltip.text}
        </div>
      )}
    </div>
  )
}
