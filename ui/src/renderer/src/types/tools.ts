export type PortType = 'ZMap' | 'Plane' | 'Heights' | 'Image2D' | 'PointCloud3D' | 'Point' | 'Any'

export interface ToolDef {
  type: string
  label: string
  category: string
  inputs: PortType[]
  outputs: PortType[]
  inputLabels?: string[]    // 포트 표시 라벨 (없으면 타입명)
  outputLabels?: string[]
  defaultParams: Record<string, unknown>
}

export const PORT_COLORS: Record<PortType, string> = {
  ZMap:          '#00bcd4',
  Plane:         '#26a69a',
  Heights:       '#ffc107',
  Image2D:       '#ff9800',
  PointCloud3D:  '#9c27b0',
  Point:         '#ec407a',
  Any:           '#888',
}

export const TOOL_DEFS: ToolDef[] = [
  {
    type: 'ZMapLoader', label: 'ZMap Loader', category: '입력',
    inputs: [], outputs: ['ZMap'],
    defaultParams: { mode: 'file', path: '', folder: '', index: 0, xResMm: 1.0, yResMm: 1.0, zResMm: 0.001 },
  },
  {
    type: 'ImageLoader', label: 'Image Loader', category: '입력',
    inputs: [], outputs: ['Image2D'],
    defaultParams: { path: '' },
  },
  {
    type: 'LineCenter', label: 'Line Center', category: '정렬',
    inputs: ['ZMap'], outputs: ['Point'],
    defaultParams: { rois: [], threshold: 1, xRoi: 0, yRoi: 0 },
  },
  {
    type: 'NoiseFilter', label: 'Noise Filter', category: '필터',
    inputs: ['Any'], outputs: ['Any'],
    defaultParams: { kernelSize: 3, radius: 1.0, minNeighbors: 5 },
  },
  {
    type: 'EdgeDetector', label: 'Edge Detector', category: '필터',
    inputs: ['Image2D'], outputs: ['Image2D'],
    defaultParams: {},
  },
  {
    type: 'LineFitHeight', label: 'LineFit Height', category: '측정',
    inputs: ['ZMap'], outputs: ['ZMap'],
    defaultParams: {
      roiFit1:    { x: 0, y: 0, w: 100, h: 20 },
      roiFit2:    { x: 0, y: 80, w: 100, h: 20 },
      roiMeasure: { x: 0, y: 40, w: 100, h: 20 },
      aggregation: 'Mean',
      useRansac: false,
      ransacIterations: 200,
      ransacThreshold: 0.05,
    },
  },
  {
    type: 'PlaneFit', label: 'Plane Fit', category: '측정',
    inputs: ['ZMap'], outputs: ['Plane'],
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
    inputs: ['ZMap', 'Plane'], outputs: ['Heights'],
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
    type: 'ThicknessMeasure', label: 'Thickness', category: '측정',
    inputs: ['PointCloud3D'], outputs: ['PointCloud3D'],
    defaultParams: {
      roi: { xMin: 0, xMax: 100, yMin: 0, yMax: 100 },
      nominalMm: 0,
      toleranceMm: 0.05,
    },
  },
  {
    type: 'Align', label: '좌표계 변환', category: '정렬',
    inputs: ['ZMap', 'Point'], outputs: ['ZMap'],
    defaultParams: {},
  },
  {
    type: 'CsvWriter', label: 'CSV Writer', category: '출력',
    inputs: ['Heights'], outputs: [],
    defaultParams: { path: '', label: '' },
  },
]

export const TOOL_DEF_MAP: Record<string, ToolDef> = Object.fromEntries(
  TOOL_DEFS.map(d => [d.type, d])
)
