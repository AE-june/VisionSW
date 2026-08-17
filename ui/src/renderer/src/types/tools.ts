export type PortType = 'HeightMap' | 'Region' | 'Plane' | 'Line' | 'Geometry' | 'Profile' | 'Measurements' | 'Decisions' | 'PointCloud3D' | 'Point' | 'Any'

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
  description?: string      // 툴박스 툴팁 (hover 시 표시)
  tooltip?: string          // description 대신 쓸 수 있는 별칭
}

export const PORT_COLORS: Record<PortType, string> = {
  HeightMap:    '#00bcd4',
  Region:       '#66bb6a',
  Plane:        '#26a69a',
  Line:         '#ab47bc',
  Geometry:     '#7e57c2',   // 보라 계열 — Plane/Line 통합 기하
  Profile:      '#ff8f00',   // 주황 — 기존 7색과 구분
  Measurements: '#ffc107',
  Decisions:    '#ff7043',
  PointCloud3D: '#9c27b0',
  Point:        '#ec407a',
  Any:          '#888',
}

export const TOOL_DEFS: ToolDef[] = [
  {
    type: 'HeightMapLoader', label: 'HeightMap Loader', category: '입력',
    inputs: [], outputs: ['HeightMap'],
    defaultParams: { mode: 'file', path: '', folder: '', index: 0, xResMm: 1.0, yResMm: 1.0, zResMm: 0.001 },
    description: '파일 또는 폴더에서 3D 표면 데이터(HeightMap)를 로드',
  },
  {
    type: 'ExposureMerge', label: 'Exposure Split', category: 'SDC 전용',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: { splitCount: 2 },
    description: '다중노출 SDC HeightMap을 노출별로 분리',
  },
  {
    type: 'RowStretch', label: 'Row Stretch', category: 'SDC 전용',
    inputs: ['HeightMap', { type: 'Region', optional: true }], outputs: ['HeightMap'],
    inputLabels: ['HeightMap', 'Region(선택)'],
    defaultParams: { scale: 2 },
    description: '지정 행(Region)을 선형 보간해 scale배 업샘플. SDC 노출 정렬용',
  },
  {
    type: 'ExposureMerge2', label: 'Exposure Merge', category: 'SDC 전용',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: {
      matchTol: 20, reflTol: -1, tolX: 10, tolY: 100, gapK: 2, halfRes: true,
    },
    description: '저/장노출 2단계 병합. 반사 제거 + 갭 보완',
  },
  {
    type: 'ExposureMerge3', label: 'Exposure Merge (3)', category: 'SDC 전용',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: {
      matchTol: 20, reflTol: 30, tolX: 10, tolY: 100, gapK: 2, halfRes: true,
      removeReflection: true,
    },
    description: '저/중/장노출 3단계 캐스케이드 병합. 반사 및 갭 완전 제거',
  },
  {
    type: 'Threshold', label: 'Threshold', category: '분할',
    inputs: ['HeightMap'], outputs: ['Region'],
    defaultParams: { channel: 0, thresholdMode: 'mm', thresholdMm: 0, thresholdRaw: 0, keepAbove: true },
    description: '임계값(mm 또는 raw 픽셀값)으로 HeightMap을 Region(마스크)으로 분할',
  },
  {
    type: 'ValidRegion', label: 'Valid Region', category: '분할',
    inputs: ['HeightMap'], outputs: ['Region'],
    defaultParams: { channel: 0, invert: false },
    description: 'NaN이 아닌 유효 픽셀 영역을 Region으로 추출',
  },
  {
    type: 'Level', label: 'Level', category: '변환',
    inputs: ['HeightMap', 'Plane'], outputs: ['HeightMap'],
    inputLabels: ['HeightMap', 'Plane'],
    defaultParams: { mode: 'distance', keepInvalid: true, offsetMm: 0.0 },
    description: 'Plane 기준으로 HeightMap을 평탄화하거나 수직 거리로 변환',
  },
  {
    type: 'SurfaceCrop', label: 'Surface Crop', category: '변환',
    inputs: ['HeightMap', 'Region'], outputs: ['HeightMap'],
    inputLabels: ['HeightMap', 'Region(선택)'],
    defaultParams: { mode: 'rect', rect: { x: 0, y: 0, w: 0, h: 0 }, outsideNaN: true },
    description: '사각형 또는 Region 마스크로 HeightMap을 잘라냄',
  },
  {
    type: 'SurfaceResample', label: 'Surface Resample', category: '변환',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: { mode: 'factor', factor: 2, targetXResMm: 0, targetYResMm: 0, method: 'decimate' },
    description: 'HeightMap 해상도를 배율 또는 목표 분해능으로 다운샘플 (측정 경로 비권장)',
  },
  {
    type: 'CreateROI', label: 'Create ROI', category: '분할',
    inputs: ['HeightMap', { type: 'Line', optional: true }], outputs: [{ type: 'Region', isArray: true }],
    inputLabels: ['HeightMap', 'Line(선택)'],
    defaultParams: { rois: [], bandWidthMm: 5, bandOffsetMm: 3, bandSide: 'both', bandLenMode: 'line', bandLengthMm: 10 },
    description: '사각·원·폴리곤 ROI → Region[]. 포트1에 Line 연결 시 라인 좌/우 밴드 ROI 자동 생성',
  },
  {
    type: 'ReduceDomain', label: 'Reduce Domain', category: '변환',
    inputs: ['HeightMap', { type: 'Region', isArray: true }], outputs: ['HeightMap'],
    inputLabels: ['HeightMap', 'Region[ ]'],
    defaultParams: { invert: false },
    description: 'Region(여러 개 union)으로 처리 범위 제한. invert=off: 안쪽만 유지 · on: 안쪽 제거(제외 마스크)',
  },
  {
    type: 'RegionMeasure', label: 'Region Measure', category: '측정',
    inputs: [{ type: 'Region', isArray: true }, 'HeightMap'], outputs: ['Measurements'],
    inputLabels: ['Region', 'HeightMap(선택)'],
    defaultParams: {
      aggregation: 'Mean',
      highTailPct: 20,
      percentile: 50,
    },
    description: 'Region 면적·무게중심·BBox·방향·Z 집계·체적·평탄도 측정. aggregation: Mean|Median|Max|Min|HighTail|Percentile|StdDev',
  },
  {
    type: 'LineCenter', label: 'Line Finder', category: 'SDC 전용',
    inputs: ['HeightMap', { type: 'Region', optional: true }], outputs: ['Point'],
    inputLabels: ['HeightMap', 'Region(선택)'],
    defaultParams: { scanDir: 'lr', threshold: 1, polarity: 'd2l' },
    description: '스캔 방향으로 라인 엣지를 검출해 중심 좌표(Point) 반환. SDC 정렬용',
  },
  {
    type: 'NoiseFilter', label: 'Noise Filter', category: '필터',
    inputs: ['HeightMap', { type: 'Region', optional: true }], outputs: ['HeightMap'],
    inputLabels: ['HeightMap', 'Region(선택)'],
    defaultParams: { filterType: 'median', kernelSizeX: 3, kernelSizeY: 3, stdRatio: 2.0, sigmaRangeMm: 0.02, radius: 1.0, minNeighbors: 5 },
    tooltip: '포트 1에 Region 연결 시 해당 영역만 필터. 없으면 전체 적용.',
  },
  {
    type: 'GapFill', label: 'Gap Fill', category: '필터',
    inputs: ['HeightMap'], outputs: ['HeightMap'],
    defaultParams: { method: 'neighbor', maxGap: 5, minValidNeighbors: 3, idwRadius: 8, idwPower: 2, edgeSigma: 30 },
    description: '유효하지 않은 픽셀(NaN·갭)을 이웃값 또는 IDW 보간으로 채움',
  },
  {
    type: 'PlaneFit', label: 'Plane Fit', category: '측정',
    inputs: ['HeightMap', { type: 'Region', optional: true }], outputs: ['Plane'],
    inputLabels: ['HeightMap', 'Region(선택)'],
    defaultParams: {
      algorithm: 'LeastSquares',
      ransacThreshold: 0.05,
      ransacIterations: 200,
      maxCloudPoints: 200000,
    },
    tooltip: '포트 1에 Region 연결 시 해당 영역만 평면 피팅. 없으면 전체 HeightMap 사용.',
  },
  {
    type: 'LineFit', label: 'Line Fit', category: '측정',
    inputs: ['HeightMap', { type: 'Region', isArray: true }], outputs: ['Line'],
    inputLabels: ['HeightMap', 'Region[ ](선택)'],
    defaultParams: { feature: 'ridge', scanDir: 'lr', threshold: 0, risingEdge: true,
                     fitMethod: 'leastSquares', ransacTolMm: 0.5, ransacIters: 100 },
    description: '높이 기반 엣지/능선/골 검출 → XY 평면 직선 피팅. 각도·중심·직진도 측정',
  },
  {
    type: 'Align', label: 'Align', category: 'SDC 전용',
    inputs: ['HeightMap', 'Point'], outputs: ['HeightMap'],
    defaultParams: { useX: true, useY: true },
    description: 'Line Finder가 검출한 Point 기준으로 HeightMap을 X/Y 방향 정렬',
  },
  {
    type: 'CsvWriter', label: 'CSV Writer', category: '출력',
    inputs: [{ type: 'Any' }], outputs: [],
    defaultParams: { path: '', label: '' },
    description: '측정값(Measurements) 또는 Profile[]을 CSV 파일로 저장',
  },
  {
    type: 'HeightMapSaver', label: 'HeightMap Saver', category: '출력',
    inputs: ['HeightMap'], outputs: [],
    defaultParams: { folder: '', filename: '', format: 'png' },
    description: 'HeightMap을 파일로 저장. 사이드카 .meta.json에 메타(해상도·원점·zZero 등)를 함께 기록해 HeightMapLoader로 정확히 복원 가능.',
  },
  {
    type: 'HeightMapToCloud', label: 'HeightMap to Cloud', category: '변환',
    inputs: ['HeightMap'], outputs: ['PointCloud3D'],
    defaultParams: { step: 1 },
    description: 'HeightMap의 유효 픽셀을 3D 포인트클라우드로 변환',
  },
  {
    type: 'CloudSaver', label: 'Cloud Saver', category: '출력',
    inputs: ['PointCloud3D'], outputs: [],
    defaultParams: { folder: '', filename: '', format: 'ply' },
    description: '3D 포인트클라우드를 PLY 등 파일 형식으로 저장',
  },
  {
    type: 'CloudLoader', label: 'Cloud Loader', category: '입력',
    inputs: [], outputs: ['PointCloud3D'],
    defaultParams: { path: '' },
    description: '포인트클라우드 파일(ply/xyz/bin) → PointCloud3D 로드',
  },
  {
    type: 'CloudToProfiles', label: 'Cloud to Profiles', category: '변환',
    inputs: ['PointCloud3D'], outputs: [{ type: 'Profile', isArray: true }],
    defaultParams: { scanAxis: 'x', scanStepMm: 0.1, reduce: 'none', latStepMm: 0.1, minPoints: 1 },
    description: '포인트클라우드를 스캔축(기본 X) bin별 Profile[]로 분해. 프로파일 내부=횡축. reduce=none이면 다중 Z 전부 보존',
  },
  {
    type: 'NotchMeasure', label: 'Notch Measure', category: '측정',
    inputs: ['PointCloud3D'], outputs: ['PointCloud3D', { type: 'Profile', isArray: true }],
    defaultParams: {
      method: 'flat',
      transportResMm: 0.003998, lateralPitchMm: 0.0063,
      avgProfiles: 1, avgMethod: 'mean',
      floorAgg: 'median',
      notchTrigUm: -150, landTolUm: 30,
      floorWinUm: 150, smoothCols: 3, floorTolUm: 40,
    },
    description: '배터리 캔캡 노치 깊이 측정. 출력: 필터링Cloud + 청크별 깊이Profile[] + valid_chunks',
  },
  {
    type: 'Collect', label: 'Collect', category: '축약',
    inputs: [{ type: 'Any', isArray: true }], outputs: [{ type: 'Any' }],
    defaultParams: {},
    description: '여러 노드의 측정값·판정을 하나의 출력으로 수집',
  },
  {
    type: 'SurfaceSubtract', label: 'Surface Subtract', category: '표면 변환',
    inputs: ['HeightMap', 'HeightMap'], outputs: ['HeightMap'],
    inputLabels: ['A', 'B'],
    defaultParams: { absolute: false, nanPolicy: 'propagate' },
    tooltip: 'A - B 높이맵 차이 (단위: mm)',
  },
  {
    type: 'ExtractProfile', label: 'Extract Profile', category: '변환',
    inputs: ['HeightMap', { type: 'Region', optional: true }], outputs: ['Profile'],
    inputLabels: ['HeightMap', 'Region(선택)'],
    defaultParams: {
      mode: 'axisX', index: 0, span: 1, repeat: 1, channel: 0,
      p0x: 0, p0y: 0, p1x: 0, p1y: 0, unit: 'mm', count: 0, interp: 'bilinear',
    },
    tooltip: '높이맵 단면 → Profile. axisX/Y: 행/열 그대로 추출(보간 없음). line: 임의 경로 보간.',
  },
  {
    type: 'Compare', label: 'Compare', category: '판정',
    inputs: ['Measurements'], outputs: ['Measurements', 'Decisions'],
    defaultParams: {
      target: '', mode: 'tolerance', nominal: 0, tolerance: 0.05, min: 0, max: 0,
    },
    description: '측정값 → 판정. mode: tolerance(공칭±공차) | range(min~max) | max | min',
  },
  {
    type: 'CombineDecision', label: 'Combine Decision', category: '판정',
    inputs: ['Decisions'], outputs: ['Decisions'],
    defaultParams: { mode: 'all', count: 1, name: 'combined' },
    description: '판정 결합. mode: all(AND) | any(OR) | count(N개 이상)',
  },
  {
    type: 'ProfileFeature', label: 'Profile Feature', category: '측정',
    inputs: [{ type: 'Profile', isArray: true }], outputs: ['Measurements'],
    defaultParams: {
      kind: 'maxZ', searchFromMm: 0, searchToMm: 0, nth: 0, percentile: 50,
      edgeDir: 'any', edgeThresholdMm: 0.05, smoothWindow: 3,
    },
    tooltip: 'Profile 단면 → 집계·특징점 측정. kind: maxZ|minZ|maxS|minS|mean|median|stdDev|percentile|highTail|edge|ridge|valley|corner',
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
