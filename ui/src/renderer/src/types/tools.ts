export type PortType = 'HeightMap' | 'Region' | 'Plane' | 'Heights' | 'PointCloud3D' | 'Point' | 'Any'

// T0-2 P3: 포트 다중성. isArray=true면 배열 포트(엔진이 원소별 브로드캐스트).
// 기존 노드 정의는 PortType 문자열 그대로 두고, 배열 포트만 { type, isArray } 로 적는다.
export interface PortSpec { type: PortType; isArray?: boolean }
export type PortDecl = PortType | PortSpec

export function portType(p: PortDecl): PortType { return typeof p === 'string' ? p : p.type }
export function portIsArray(p: PortDecl): boolean { return typeof p === 'string' ? false : (p.isArray ?? false) }

export interface ToolDef {
  type: string
  label: string
  category: string
  inputs: PortDecl[]
  outputs: PortDecl[]
  inputLabels?: string[]    // 포트 표시 라벨 (없으면 타입명)
  outputLabels?: string[]
  defaultParams: Record<string, unknown>
}

export const PORT_COLORS: Record<PortType, string> = {
  HeightMap:          '#00bcd4',
  Region:        '#66bb6a',
  Plane:         '#26a69a',
  Heights:       '#ffc107',
  PointCloud3D:  '#9c27b0',
  Point:         '#ec407a',
  Any:           '#888',
}

export const TOOL_DEFS: ToolDef[] = [
  {
    type: 'HeightMapLoader', label: 'HeightMap Loader', category: '입력',
    inputs: [], outputs: ['HeightMap'],
    defaultParams: { mode: 'file', path: '', folder: '', index: 0, xResMm: 1.0, yResMm: 1.0, zResMm: 0.001 },
  },
  {
    type: 'ExposureMerge', label: 'Exposure Split', category: '필터',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: { splitCount: 2, outputStage: 0 },
  },
  {
    type: 'RowStretch', label: 'Row Stretch', category: '필터',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: { rois: [] },
  },
  {
    type: 'ExposureMerge2', label: 'Exposure Merge', category: '필터',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: {
      matchTol: 20, reflTol: -1, tolX: 10, tolY: 100, gapK: 2, halfRes: true,
      chunkMode: false, chunkRows: 1000, overlapRows: 320,
    },
  },
  {
    type: 'ExposureMerge3', label: 'Exposure Merge (3)', category: '필터',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: {
      matchTol: 20, reflTol: 30, tolX: 10, tolY: 100, gapK: 2, halfRes: true,
      removeReflection: true,
      chunkMode: false, chunkRows: 1000, overlapRows: 180,
    },
  },
  {
    type: 'Threshold', label: 'Threshold', category: '분할',
    inputs: ['HeightMap'], outputs: ['Region'],
    defaultParams: { channel: 0, thresholdMm: 0, keepAbove: true },
  },
  {
    type: 'CreateROI', label: 'Create ROI', category: '분할',
    inputs: ['HeightMap'], outputs: ['Region'],
    defaultParams: { rois: [] },
  },
  {
    type: 'ReduceDomain', label: 'Reduce Domain', category: '변환',
    inputs: ['HeightMap', 'Region'], outputs: ['HeightMap'],
    inputLabels: ['HeightMap', 'Region'],
    defaultParams: {},
  },
  {
    type: 'RegionMeasure', label: 'Region Measure', category: '측정',
    inputs: ['Region', 'HeightMap'], outputs: ['Heights'],
    inputLabels: ['Region', 'HeightMap'],
    defaultParams: {},
  },
  {
    type: 'LineCenter', label: 'Line Finder', category: '정렬',
    inputs: ['HeightMap'], outputs: ['Point'],
    defaultParams: { rois: [], threshold: 1, xRoi: 0, yRoi: 0 },
  },
  {
    type: 'NoiseFilter', label: 'Noise Filter', category: '필터',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: { rois: [], filterType: 'median', kernelSizeX: 3, kernelSizeY: 3, stdRatio: 2.0, sigmaRangeMm: 0.02, radius: 1.0, minNeighbors: 5 },
  },
  {
    type: 'GapFill', label: 'Gap Fill', category: '필터',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: { method: 'neighbor', maxGap: 5, minValidNeighbors: 3, idwRadius: 8, idwPower: 2, edgeSigma: 30, outputStage: 0 },
  },
  {
    type: 'PlaneFit', label: 'Plane Fit', category: '측정',
    inputs: ['HeightMap'], outputs: ['Plane'],
    defaultParams: {
      rois: [],
      algorithm: 'LeastSquares',
      ransacThreshold: 0.05,
      ransacIterations: 200,
      maxCloudPoints: 200000,
    },
  },
  {
    type: 'HeightMeasure', label: 'Height Measure', category: '측정',
    inputs: ['HeightMap', 'Plane'], outputs: ['Heights'],
    defaultParams: {
      rois: [],
      aggregation: 'Mean',
      highTailPct: 20,
      useTolerance: false,
      nominalMm: 0,
      toleranceMm: 0.05,
    },
  },
  {
    type: 'Align', label: 'Align', category: '정렬',
    inputs: ['HeightMap', 'Point'], outputs: ['HeightMap'],
    defaultParams: {},
  },
  {
    type: 'CsvWriter', label: 'CSV Writer', category: '출력',
    inputs: ['Heights'], outputs: [],
    defaultParams: { path: '', label: '' },
  },
  {
    type: 'ImageSaver', label: 'Image Saver', category: '출력',
    inputs: ['Any'], outputs: [],
    defaultParams: { folder: '', filename: '', format: 'png' },
  },
  {
    type: 'HeightMapToCloud', label: 'HeightMap to Cloud', category: '변환',
    inputs: ['HeightMap'], outputs: ['PointCloud3D'],
    defaultParams: { step: 1 },
  },
  {
    type: 'ExposureMergeCloud', label: 'Exposure Merge (Cloud)', category: '변환',
    inputs: ['HeightMap'], outputs: ['PointCloud3D'],
    defaultParams: { matchTol: 20, tolX: 5, tolY: 30, gapK: 0 },
  },
  {
    type: 'CloudSaver', label: 'Cloud Saver', category: '출력',
    inputs: ['PointCloud3D'], outputs: [],
    defaultParams: { folder: '', filename: '', format: 'ply' },
  },
]

export const TOOL_DEF_MAP: Record<string, ToolDef> = Object.fromEntries(
  TOOL_DEFS.map(d => [d.type, d])
)

// ─────────────────────────────────────────────────────────────────
//  T0-2 P4 — 축약(reduction) 노드 인터페이스 정의만. (설계 §4.6)
//  구현은 후속(T2-1/T2-2). TOOL_DEFS에 등록하지 않는다 → 아직 드롭/실행 불가.
//  배열 입력을 받아 축약하는 노드들의 포트/파라미터 계약을 여기 못박아 둔다.
//    Collect — 브로드캐스트 결과(배열)를 단일 배열로 모음 (identity gather)
//    Filter  — 조건(predicate)으로 배열 원소를 걸러 부분 배열 출력
//    Select  — 인덱스로 배열에서 원소 하나(스칼라)를 뽑음
// ─────────────────────────────────────────────────────────────────
export const PLANNED_REDUCTION_NODES: ToolDef[] = [
  {
    type: 'Collect', label: 'Collect', category: '축약',
    inputs: [{ type: 'Any', isArray: true }], outputs: [{ type: 'Any', isArray: true }],
    defaultParams: {},
  },
  {
    type: 'Filter', label: 'Filter', category: '축약',
    inputs: [{ type: 'Any', isArray: true }], outputs: [{ type: 'Any', isArray: true }],
    inputLabels: ['items'],
    // metric: 걸러낼 기준 필드, min/max: 통과 범위(mm 등)
    defaultParams: { metric: '', min: 0, max: 0 },
  },
  {
    type: 'Select', label: 'Select', category: '축약',
    inputs: [{ type: 'Any', isArray: true }], outputs: [{ type: 'Any', isArray: false }],
    inputLabels: ['items'],
    defaultParams: { index: 0 },
  },
]
