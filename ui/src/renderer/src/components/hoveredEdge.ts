import { createContext } from 'react'

// 마우스 오버된 엣지 정보 — 연결된 입출력 핸들/라벨 강조용
export interface HoveredEdge {
  source?: string; sourceHandle?: string | null
  target?: string; targetHandle?: string | null
}

export const HoveredEdgeContext = createContext<HoveredEdge | null>(null)
