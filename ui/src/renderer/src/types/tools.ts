export type PortType = 'ZMap' | 'PlaneZMap' | 'Image2D' | 'PointCloud3D' | 'Any'

export interface ToolDef {
  type: string
  label: string
  category: string
  inputs: PortType[]
  outputs: PortType[]
  defaultParams: Record<string, unknown>
}

export const PORT_COLORS: Record<PortType, string> = {
  ZMap:          '#00bcd4',
  PlaneZMap:     '#26a69a',
  Image2D:       '#ff9800',
  PointCloud3D:  '#9c27b0',
  Any:           '#888',
}

export const TOOL_DEFS: ToolDef[] = [
  {
    type: 'ZMapLoader', label: 'ZMap Loader', category: '입력',
    inputs: [], outputs: ['ZMap'],
    defaultParams: { path: '', xResMm: 1.0, yResMm: 1.0, zResMm: 0.001 },
  },
  {
    type: 'ImageLoader', label: 'Image Loader', category: '입력',
    inputs: [], outputs: ['Image2D'],
    defaultParams: { path: '' },
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
    inputs: ['ZMap'], outputs: ['PlaneZMap'],
    defaultParams: {
      rois: [],
      algorithm: 'LeastSquares',
      ransacThreshold: 0.05,
      ransacIterations: 200,
    },
  },
  {
    type: 'HeightFromPlane', label: 'Height from Plane', category: '측정',
    inputs: ['PlaneZMap'], outputs: ['PlaneZMap'],
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
]

export const TOOL_DEF_MAP: Record<string, ToolDef> = Object.fromEntries(
  TOOL_DEFS.map(d => [d.type, d])
)
