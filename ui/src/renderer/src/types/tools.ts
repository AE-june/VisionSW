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
    type: 'ExposureMerge', label: 'Exposure Split', category: '필터',
    inputs: ['ZMap'], outputs: ['ZMap'],
    defaultParams: { outputStage: 0 },
  },
  {
    type: 'RowStretch', label: 'Row Stretch', category: '필터',
    inputs: ['ZMap'], outputs: ['ZMap'],
    defaultParams: { rois: [] },
  },
  {
    type: 'ExposureMerge2', label: 'Exposure Merge', category: '필터',
    inputs: ['ZMap'], outputs: ['ZMap'],
    defaultParams: {
      matchTol: 20, reflTol: 30, tolX: 10, tolY: 100, gapK: 2, halfRes: true,
      chunkMode: false, chunkRows: 1000, overlapRows: 320,
    },
  },
  {
    type: 'ExposureMerge3', label: 'Exposure Merge (3)', category: '필터',
    inputs: ['ZMap'], outputs: ['ZMap'],
    defaultParams: {
      matchTol: 20, reflTol: 30, tolX: 10, tolY: 100, gapK: 2, halfRes: true,
      removeReflection: true,
    },
  },
  {
    type: 'LineCenter', label: 'Line Finder', category: '정렬',
    inputs: ['ZMap'], outputs: ['Point'],
    defaultParams: { rois: [], threshold: 1, xRoi: 0, yRoi: 0 },
  },
  {
    type: 'NoiseFilter', label: 'Noise Filter', category: '필터',
    inputs: ['ZMap'], outputs: ['ZMap'],
    defaultParams: { rois: [], filterType: 'median', kernelSizeX: 3, kernelSizeY: 3, stdRatio: 2.0, sigmaRangeMm: 0.02, radius: 1.0, minNeighbors: 5 },
  },
  {
    type: 'GapFill', label: 'Gap Fill', category: '필터',
    inputs: ['ZMap'], outputs: ['ZMap'],
    defaultParams: { method: 'neighbor', maxGap: 5, minValidNeighbors: 3, idwRadius: 8, idwPower: 2, edgeSigma: 30, outputStage: 0 },
  },
  {
    type: 'EdgeDetector', label: 'Edge Detector', category: '필터',
    inputs: ['Image2D'], outputs: ['Image2D'],
    defaultParams: {},
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
    type: 'ThicknessMeasure', label: 'Thickness Measure', category: '측정',
    inputs: ['PointCloud3D'], outputs: ['PointCloud3D'],
    defaultParams: {
      roi: { xMin: 0, xMax: 100, yMin: 0, yMax: 100 },
      nominalMm: 0,
      toleranceMm: 0.05,
    },
  },
  {
    type: 'Align', label: 'Align', category: '정렬',
    inputs: ['ZMap', 'Point'], outputs: ['ZMap'],
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
    type: 'ZMapToCloud', label: 'ZMap to Cloud', category: '변환',
    inputs: ['ZMap'], outputs: ['PointCloud3D'],
    defaultParams: { step: 1 },
  },
  {
    type: 'ExposureMergeCloud', label: 'Exposure Merge (Cloud)', category: '변환',
    inputs: ['ZMap'], outputs: ['PointCloud3D'],
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
