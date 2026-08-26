import { TOOL_DEF_MAP, portType, portIsArray } from './types/tools'

// 엔진 전송용 노드 payload. 엔진 브로드캐스트가 포트 arity를 알도록 inputPorts 동봉.
export function nodeToEnginePayload(
  id: string,
  toolType: string,
  params: Record<string, unknown>
) {
  const def = TOOL_DEF_MAP[toolType]
  const inputPorts = (def?.inputs ?? []).map(p => ({
    type: portType(p),
    isArray: portIsArray(p),
  }))
  return { id, type: toolType, params: params ?? {}, inputPorts }
}
