// Profile 형상 차트 — z(세로) vs x(가로). NaN은 갭. 점(scatter) 또는 선(line).
import { useState, useCallback, useRef, useEffect, useId } from 'react'

export interface CaliperFeature {
  sMm: number
  zMm: number
  kind: string
  label?: string
}

export interface CaliperLineFit {
  fromMm: number
  toMm: number
  slope: number
  intercept: number
}

export interface ProfileRange {
  fromMm: number
  toMm: number
  color: string
  label: string
  /** z(높이) 범위. zTo>zFrom 이면 세로로 박스 형태로 그림. 없으면 전체 높이 밴드 */
  zFrom?: number
  zTo?: number
}

/** 측정 주석 오버레이 (거리선/수선 + 라벨) */
export interface ProfileAnnotation {
  kind: 'segment' | 'perp' | 'angle'
  // segment: (s1,z1)→(s2,z2) 두 점 잇는 점선
  // perp:    (s1,z1)에서 라인에 내린 수선. (s2,z2)=수선의 발 (mm 공간)
  //          lineSlope/lineIntercept 제공 시 화면 픽셀 기준 수선으로 재계산
  // angle:   교차부 호(옵션). 현재는 단순 라벨 배치
  s1: number; z1: number; s2: number; z2: number
  label: string; color: string
  lineSlope?: number; lineIntercept?: number
}

interface Props {
  x: number[]
  z: (number | null)[]
  mode?: 'line' | 'points'
  height?: number
  features?: CaliperFeature[]
  lineFits?: CaliperLineFit[]
  /** 컬러 밴드 오버레이 (피처 검색범위, 라인피팅 구간 등) */
  ranges?: ProfileRange[]
  /** 측정 주석 오버레이 (거리선/수선 + 라벨). 없으면 아무것도 안 그림 */
  annotations?: ProfileAnnotation[]
  /** 드래그로 범위 선택 시 콜백. 제공되면 좌클릭 드래그 = 범위 선택 모드 */
  onRangeDrag?: (fromMm: number, toMm: number) => void
  /** 2D 박스 ROI 드래그 콜백. 제공되면 좌클릭 드래그 = 2D 박스 선택(x+z) 모드 (onRangeDrag보다 우선) */
  onRoiDrag?: (fromMm: number, toMm: number, zFrom: number, zTo: number) => void
  /** 편집 가능한 range 인덱스. 설정되면 해당 range 박스에 리사이즈/이동 핸들 표시 (onRoiDrag 필요) */
  editRangeIndex?: number
}

const ZOOM_IN = 0.85
const ZOOM_OUT = 1 / ZOOM_IN

export default function ProfileChart({ x, z, mode = 'line', height = 220, features, lineFits, ranges, annotations, onRangeDrag, onRoiDrag, editRangeIndex }: Props) {
  const W = 480, H = height
  const padL = 44, padR = 10, padT = 10, padB = 26

  const svgRef = useRef<SVGSVGElement | null>(null)
  const clipId = useId()

  const [tooltip, setTooltip] = useState<{ svgX: number; svgY: number; dx: number; dz: number } | null>(null)
  const [lockedRange, setLockedRange] = useState<{ xMin: number; xMax: number; zMin: number; zMax: number } | null>(null)
  const [fixedByUser, setFixedByUser] = useState(false)
  const [isPanning, setIsPanning] = useState(false)
  const panRef = useRef<{ mouseX: number; mouseY: number; xMinAtStart: number; xMaxAtStart: number; zMinAtStart: number; zMaxAtStart: number } | null>(null)

  // 범위 드래그 상태 (2D: x는 mm, z는 데이터값)
  const [dragRange, setDragRange] = useState<{ startMm: number; curMm: number; startZ: number; curZ: number } | null>(null)
  const dragRangeRef = useRef<{ startMm: number; startZ: number } | null>(null)

  // ROI 편집(리사이즈/이동) 드래그 상태
  type EditMode = 'resize-l' | 'resize-r' | 'resize-t' | 'resize-b' | 'move'
  const [editBox, setEditBox] = useState<{ fromMm: number; toMm: number; zFrom: number; zTo: number } | null>(null)
  const editDragRef = useRef<{
    mode: EditMode
    startSvgX: number; startSvgY: number
    origFrom: number; origTo: number; origZFrom: number; origZTo: number
  } | null>(null)

  // 좌클릭 드래그(범위/ROI) 활성 여부
  const dragEnabled = !!(onRoiDrag || onRangeDrag)

  // 유효 샘플 범위 (auto)
  let xMinAuto = Infinity, xMaxAuto = -Infinity, zMinAuto = Infinity, zMaxAuto = -Infinity
  for (let i = 0; i < x.length; i++) {
    const zi = z[i]
    if (zi === null || Number.isNaN(zi as number)) continue
    if (x[i] < xMinAuto) xMinAuto = x[i]; if (x[i] > xMaxAuto) xMaxAuto = x[i]
    if ((zi as number) < zMinAuto) zMinAuto = zi as number
    if ((zi as number) > zMaxAuto) zMaxAuto = zi as number
  }
  // 유효 샘플 없음 판정 — early return은 훅 이후로 미룸 (Rules of Hooks: 조건부 early return 금지)
  const noValidSamples = !Number.isFinite(xMinAuto)

  const xMin = lockedRange?.xMin ?? xMinAuto
  const xMax = lockedRange?.xMax ?? xMaxAuto
  const zMin = lockedRange?.zMin ?? zMinAuto
  const zMax = lockedRange?.zMax ?? zMaxAuto

  const xr = xMax - xMin || 1, zr = zMax - zMin || 1
  const sx = (v: number) => padL + ((v - xMin) / xr) * (W - padL - padR)
  const sy = (v: number) => padT + (1 - (v - zMin) / zr) * (H - padT - padB)
  // 줌 레벨에 따라 선 두께 / 폰트 크기 조정: 줌아웃 시 비례적으로 얇게
  const svgPerMm = (W - padL - padR) / xr   // SVG 단위 / mm
  const sw = (basePx: number) => Math.min(basePx, Math.max(0.4, basePx * Math.sqrt(svgPerMm / 20)))
  // SVG x → data mm
  const svgXToMm = (svgX: number) => xMin + ((svgX - padL) / (W - padL - padR)) * xr
  // SVG y → data z (sy 역함수)
  const svgYToZ = (svgY: number) => zMin + (1 - (svgY - padT) / (H - padT - padB)) * zr

  // 편집 가능한 range(=editRangeIndex) 박스. onRoiDrag가 있고 x범위(to>from)만 있으면 편집(조작) 활성.
  // z범위가 없으면 전체 높이 박스로 간주 → 이후 z핸들 드래그로 z범위 지정. (박스 있으면 새로 안 그림)
  const editRange = (editRangeIndex !== undefined && onRoiDrag && ranges) ? ranges[editRangeIndex] : undefined
  const editHasBox = !!(editRange && editRange.toMm > editRange.fromMm)
  const editHasZ = !!(editRange && editRange.zFrom !== undefined && editRange.zTo !== undefined
    && (editRange.zTo as number) > (editRange.zFrom as number))
  // 편집 중이면 preview(editBox) 사용, 아니면 원본 range 값(z 없으면 전체 높이) 사용
  const editGeom = editHasBox ? {
    fromMm: editBox?.fromMm ?? (editRange as ProfileRange).fromMm,
    toMm:   editBox?.toMm   ?? (editRange as ProfileRange).toMm,
    zFrom:  editBox?.zFrom  ?? (editHasZ ? (editRange as ProfileRange).zFrom as number : zMinAuto),
    zTo:    editBox?.zTo    ?? (editHasZ ? (editRange as ProfileRange).zTo as number   : zMaxAuto),
  } : null

  // 선분: NaN 경계에서 끊음 → 여러 polyline
  const segments: string[] = []
  let cur: string[] = []
  for (let i = 0; i < x.length; i++) {
    const zi = z[i]
    if (zi === null || Number.isNaN(zi as number)) { if (cur.length) { segments.push(cur.join(' ')); cur = [] } continue }
    cur.push(`${sx(x[i]).toFixed(1)},${sy(zi as number).toFixed(1)}`)
  }
  if (cur.length) segments.push(cur.join(' '))

  const getSvgXFromEvent = (e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect()
    return (e.clientX - rect.left) / rect.width * W
  }
  const getSvgYFromEvent = (e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect()
    return (e.clientY - rect.top) / rect.height * H
  }

  const handleMouseMove = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect()

    // ROI 편집(리사이즈/이동) 드래그 중
    if (editDragRef.current) {
      const ed = editDragRef.current
      const svgX = (e.clientX - rect.left) / rect.width * W
      const svgY = (e.clientY - rect.top) / rect.height * H
      const curMm = svgXToMm(svgX)
      const curZ = svgYToZ(svgY)
      const startMm = svgXToMm(ed.startSvgX)
      const startZ = svgYToZ(ed.startSvgY)
      const dMm = curMm - startMm
      const dZ = curZ - startZ
      let nFrom = ed.origFrom, nTo = ed.origTo, nZFrom = ed.origZFrom, nZTo = ed.origZTo
      if (ed.mode === 'resize-l') nFrom = curMm
      else if (ed.mode === 'resize-r') nTo = curMm
      else if (ed.mode === 'resize-b') nZFrom = curZ
      else if (ed.mode === 'resize-t') nZTo = curZ
      else if (ed.mode === 'move') { nFrom += dMm; nTo += dMm; nZFrom += dZ; nZTo += dZ }
      // 리사이즈 시 min/max 정규화 (좌우/상하 뒤집힘 허용 → 정규화)
      const fromMm = Math.min(nFrom, nTo), toMm = Math.max(nFrom, nTo)
      const zFrom = Math.min(nZFrom, nZTo), zTo = Math.max(nZFrom, nZTo)
      setEditBox({ fromMm, toMm, zFrom, zTo })
      return
    }

    // 우클릭 팬 중
    if (panRef.current) {
      const p = panRef.current
      const scaleX = W / rect.width
      const scaleY = H / rect.height
      const dxPx = (e.clientX - p.mouseX) * scaleX
      const dyPx = (e.clientY - p.mouseY) * scaleY
      const rangeX = p.xMaxAtStart - p.xMinAtStart
      const rangeZ = p.zMaxAtStart - p.zMinAtStart
      const dDataX = -(dxPx / (W - padL - padR)) * rangeX
      const dDataZ = (dyPx / (H - padT - padB)) * rangeZ
      setLockedRange({
        xMin: p.xMinAtStart + dDataX,
        xMax: p.xMaxAtStart + dDataX,
        zMin: p.zMinAtStart + dDataZ,
        zMax: p.zMaxAtStart + dDataZ,
      })
      return
    }

    // 범위/ROI 드래그 중
    if (dragRangeRef.current) {
      const svgX = (e.clientX - rect.left) / rect.width * W
      const svgY = (e.clientY - rect.top) / rect.height * H
      const curMm = svgXToMm(svgX)
      const curZ = svgYToZ(svgY)
      setDragRange({ startMm: dragRangeRef.current.startMm, curMm, startZ: dragRangeRef.current.startZ, curZ })
      return
    }

    const ratioX = (e.clientX - rect.left) / rect.width
    const svgX = ratioX * W
    const dataX = xMin + ((svgX - padL) / (W - padL - padR)) * xr

    let best = -1, bestDist = Infinity
    for (let i = 0; i < x.length; i++) {
      const zi = z[i]
      if (zi === null || Number.isNaN(zi as number)) continue
      const dist = Math.abs(x[i] - dataX)
      if (dist < bestDist) { bestDist = dist; best = i }
    }
    if (best < 0) { setTooltip(null); return }
    const dz = z[best] as number
    setTooltip({ svgX: sx(x[best]), svgY: sy(dz), dx: x[best], dz })
  }, [x, z, xMin, xr, zMin, zr, dragRangeRef])
  // 주의: svgXToMm/svgYToZ 는 xMin/xr/zMin/zr에 의존하므로 위 deps로 충분히 갱신됨

  const handleMouseDown = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button === 2) {
      // 우클릭: 팬
      e.preventDefault()
      const curXMin = lockedRange?.xMin ?? xMinAuto
      const curXMax = lockedRange?.xMax ?? xMaxAuto
      const curZMin = lockedRange?.zMin ?? zMinAuto
      const curZMax = lockedRange?.zMax ?? zMaxAuto
      panRef.current = { mouseX: e.clientX, mouseY: e.clientY, xMinAtStart: curXMin, xMaxAtStart: curXMax, zMinAtStart: curZMin, zMaxAtStart: curZMax }
      setIsPanning(true)
      setTooltip(null)
      return
    }
    if (e.button === 0 && dragEnabled) {
      // 좌클릭: 범위/ROI 드래그 시작
      e.preventDefault()
      const svgX = getSvgXFromEvent(e)
      const svgY = getSvgYFromEvent(e)

      // 편집 가능한 ROI 박스가 있으면 핸들/바디 히트테스트 먼저
      if (editGeom) {
        const bx1 = sx(editGeom.fromMm), bx2 = sx(editGeom.toMm)
        const by1 = sy(editGeom.zTo),   by2 = sy(editGeom.zFrom) // by1=top, by2=bottom
        const HIT = 6
        const insideX = svgX >= Math.min(bx1, bx2) - HIT && svgX <= Math.max(bx1, bx2) + HIT
        const insideY = svgY >= Math.min(by1, by2) - HIT && svgY <= Math.max(by1, by2) + HIT
        let mode: EditMode | null = null
        if (insideY && Math.abs(svgX - bx1) <= HIT) mode = 'resize-l'
        else if (insideY && Math.abs(svgX - bx2) <= HIT) mode = 'resize-r'
        else if (insideX && Math.abs(svgY - by1) <= HIT) mode = 'resize-t'
        else if (insideX && Math.abs(svgY - by2) <= HIT) mode = 'resize-b'
        else if (svgX > Math.min(bx1, bx2) && svgX < Math.max(bx1, bx2) &&
                 svgY > Math.min(by1, by2) && svgY < Math.max(by1, by2)) mode = 'move'
        if (mode) {
          editDragRef.current = {
            mode, startSvgX: svgX, startSvgY: svgY,
            origFrom: editGeom.fromMm, origTo: editGeom.toMm,
            origZFrom: editGeom.zFrom, origZTo: editGeom.zTo,
          }
          setEditBox({ fromMm: editGeom.fromMm, toMm: editGeom.toMm, zFrom: editGeom.zFrom, zTo: editGeom.zTo })
          setTooltip(null)
          return
        }
        // 이미 박스가 있으면 바깥 클릭해도 새로 그리지 않음 (조작 전용).
        // 재배치는 박스 내부 드래그(이동) 또는 엣지 드래그(리사이즈)로.
        return
      }

      // 박스가 아직 없을 때(최초 1회)만 새로 그림
      const startMm = svgXToMm(svgX)
      const startZ = svgYToZ(svgY)
      dragRangeRef.current = { startMm, startZ }
      setDragRange({ startMm, curMm: startMm, startZ, curZ: startZ })
      setTooltip(null)
    }
  }, [lockedRange, xMinAuto, xMaxAuto, zMinAuto, zMaxAuto, dragEnabled, editGeom, xMin, xr, zMin, zr])

  const handleMouseUp = useCallback((e: React.MouseEvent<SVGSVGElement>) => {
    if (e.button === 2) {
      panRef.current = null
      setIsPanning(false)
      return
    }
    // ROI 편집(리사이즈/이동) 커밋
    if (e.button === 0 && editDragRef.current) {
      const b = editBox
      editDragRef.current = null
      setEditBox(null)
      if (b && onRoiDrag && (b.toMm - b.fromMm > 0.001) && (b.zTo - b.zFrom > 0.001)) {
        onRoiDrag(b.fromMm, b.toMm, b.zFrom, b.zTo)
      }
      return
    }
    if (e.button === 0 && dragRangeRef.current && dragRange && dragEnabled) {
      const from = Math.min(dragRange.startMm, dragRange.curMm)
      const to = Math.max(dragRange.startMm, dragRange.curMm)
      const zFrom = Math.min(dragRange.startZ, dragRange.curZ)
      const zTo = Math.max(dragRange.startZ, dragRange.curZ)
      if (to - from > 0.001) {
        if (onRoiDrag) onRoiDrag(from, to, zFrom, zTo)
        else if (onRangeDrag) onRangeDrag(from, to)
      }
      dragRangeRef.current = null
      setDragRange(null)
    }
  }, [dragRange, onRangeDrag, onRoiDrag, dragEnabled, editBox])

  const handleMouseLeave = useCallback(() => {
    panRef.current = null
    setIsPanning(false)
    dragRangeRef.current = null
    setDragRange(null)
    editDragRef.current = null
    setEditBox(null)
    setTooltip(null)
  }, [])

  // 현재 줌 상태를 매 렌더마다 ref에 반영 → 안정적 native wheel 핸들러가 stale closure 없이 읽음
  const zoomStateRef = useRef({ xMin, xMax, zMin, zMax })
  zoomStateRef.current = { xMin, xMax, zMin, zMax }

  // native non-passive wheel 리스너: React onWheel은 passive여서 preventDefault가 무시됨.
  // 부모 .node-panel-body 스크롤 방지를 위해 직접 등록한다.
  useEffect(() => {
    const el = svgRef.current
    if (!el) return
    const handler = (e: WheelEvent) => {
      e.preventDefault()
      e.stopPropagation()
      const rect = el.getBoundingClientRect()
      const svgX = (e.clientX - rect.left) / rect.width * W
      const svgY = (e.clientY - rect.top) / rect.height * H

      const factor = e.deltaY < 0 ? ZOOM_IN : ZOOM_OUT

      const { xMin: curXMin, xMax: curXMax, zMin: curZMin, zMax: curZMax } = zoomStateRef.current

      const pivotX = curXMin + ((svgX - padL) / (W - padL - padR)) * (curXMax - curXMin)
      const pivotZ = curZMin + (1 - (svgY - padT) / (H - padT - padB)) * (curZMax - curZMin)

      const nearYAxis = svgX < padL + 8
      const nearXAxis = svgY > H - padB - 8

      let newXMin = curXMin, newXMax = curXMax
      let newZMin = curZMin, newZMax = curZMax

      if (!nearYAxis) {
        newXMin = pivotX + (curXMin - pivotX) * factor
        newXMax = pivotX + (curXMax - pivotX) * factor
      }
      if (!nearXAxis) {
        newZMin = pivotZ + (curZMin - pivotZ) * factor
        newZMax = pivotZ + (curZMax - pivotZ) * factor
      }

      setLockedRange({ xMin: newXMin, xMax: newXMax, zMin: newZMin, zMax: newZMax })
    }
    el.addEventListener('wheel', handler, { passive: false })
    return () => el.removeEventListener('wheel', handler)
  }, [W, H, padL, padR, padT, padB, noValidSamples])

  const fixRange = useCallback(() => {
    setLockedRange({ xMin, xMax, zMin, zMax })
    setFixedByUser(true)
  }, [xMin, xMax, zMin, zMax])

  const resetZoom = useCallback(() => {
    setLockedRange(null)
    setFixedByUser(false)
  }, [])

  const isDraggingRange = dragRangeRef.current !== null
  const isEditingBox = editDragRef.current !== null
  const cursor = isPanning ? 'grabbing' : isEditingBox ? 'move' : isDraggingRange ? 'crosshair' : dragEnabled ? 'crosshair' : 'crosshair'

  const tipW = 110, tipH = 32
  const tipX = tooltip ? (tooltip.svgX + tipW + 6 > W ? tooltip.svgX - tipW - 6 : tooltip.svgX + 6) : 0
  const tipY = tooltip ? Math.max(padT, Math.min(tooltip.svgY - tipH / 2, H - padB - tipH)) : 0

  const btnX = W - padR - 40, btnY = padT

  // 범위/ROI 드래그 시각화
  const dragX1 = dragRange ? sx(Math.min(dragRange.startMm, dragRange.curMm)) : 0
  const dragX2 = dragRange ? sx(Math.max(dragRange.startMm, dragRange.curMm)) : 0
  // 2D 박스(ROI) 모드일 때만 세로 경계 사용, 아니면 전체 높이
  const dragY1 = dragRange && onRoiDrag ? sy(Math.max(dragRange.startZ, dragRange.curZ)) : padT
  const dragY2 = dragRange && onRoiDrag ? sy(Math.min(dragRange.startZ, dragRange.curZ)) : H - padB

  // ── 라벨 디클러터(decluttering) 레이아웃 ─────────────────────────────
  // 플롯 사각형 경계
  const plotL = padL, plotR = W - padR, plotT = padT, plotB = H - padB
  const clampX = (v: number) => Math.max(plotL, Math.min(v, plotR))
  const clampY = (v: number) => Math.max(plotT, Math.min(v, plotB))

  // 1) 측정 주석(annotation) 값 배지: 플롯 상단의 예약 레인에 가로 한 줄로 배치.
  //    타깃(세그먼트 중점) x 로 정렬 후 좌→우로 1-D 간격 패킹, 넘치면 전체를 왼쪽으로 시프트.
  const ANNO_LANE_Y = padT + 8          // 배지 top y (padT 바로 아래)
  const ANNO_LANE_H = 14                // 배지 높이
  const ANNO_LANE_BOTTOM = ANNO_LANE_Y + ANNO_LANE_H // 리더선/충돌 판정용 레인 하단
  const annoBadges = (annotations ?? []).map((a, i) => {
    const ax1 = sx(a.s1), ay1 = sy(a.z1)
    const ax2 = sx(a.s2), ay2 = sy(a.z2)
    const targetX = clampX((ax1 + ax2) / 2)         // 리더선이 향하는 원래 중점 x
    const targetY = clampY((ay1 + ay2) / 2)         // 원래 중점 y
    const w = a.label.length * 6.5 + 8              // 추정 픽셀 폭
    return { i, label: a.label, color: a.color, targetX, targetY, w, cx: (ax1 + ax2) / 2 }
  })
  // targetX 기준 정렬 후 좌→우 패킹 (원래 인덱스는 보존)
  const annoOrder = annoBadges.map((b) => b.i).sort((p, q) => annoBadges[p].cx - annoBadges[q].cx)
  const GAP = 4
  const annoPlaced: Record<number, { x: number; w: number }> = {}
  let prevRight = plotL
  for (const idx of annoOrder) {
    const b = annoBadges[idx]
    let x = b.targetX - b.w / 2               // 배지 좌측 x (중앙정렬 기준)
    if (x < prevRight + (prevRight > plotL ? GAP : 0)) x = prevRight + (prevRight > plotL ? GAP : 0)
    if (x < plotL) x = plotL
    annoPlaced[idx] = { x, w: b.w }
    prevRight = x + b.w
  }
  // 마지막 배지가 우측 경계를 넘으면 전체 행을 왼쪽으로 시프트
  if (prevRight > plotR && annoOrder.length) {
    const shift = prevRight - plotR
    for (const idx of annoOrder) {
      annoPlaced[idx].x = Math.max(plotL, annoPlaced[idx].x - shift)
    }
  }

  // 2) 피처(포인트/엘리먼트) 라벨: 마커는 (fx,fz) 고정, 텍스트는 오프셋 후 1-D 충돌 회피.
  //    ROI 많아도 안 정신없게 단일 중립색. 구분은 라벨(R0/R1)로.
  const FEAT_COLOR = '#9aa7b4'
  // 표시 중인 프로파일에서 s(mm) 위치의 z를 선형보간 — 마커를 측정 zMm이 아니라
  // 실제 곡선 위에 앉힌다(측정 zMm이 다른 프로파일/이진값이면 허공에 뜨는 문제 방지).
  const zAtMm = (sMm: number): number | null => {
    if (x.length === 0) return null
    const val = (k: number) => (z[k] === null || Number.isNaN(z[k] as number)) ? null : (z[k] as number)
    if (sMm <= x[0]) return val(0)
    for (let i = 1; i < x.length; i++) {
      if (sMm <= x[i]) {
        const z0 = val(i - 1), z1 = val(i)
        if (z0 === null || z1 === null) return null
        const t = (sMm - x[i - 1]) / ((x[i] - x[i - 1]) || 1)
        return z0 + t * (z1 - z0)
      }
    }
    return val(x.length - 1)
  }
  const featLayout = (features ?? []).map((f, i) => {
    const fx = sx(f.sMm)
    const onCurve = zAtMm(f.sMm)          // 곡선 위 z 우선, 없으면(갭) 측정 zMm 폴백
    const fz = sy(onCurve ?? f.zMm)
    const label = f.label ?? `F${i}`
    const color = FEAT_COLOR
    const w = label.length * 6 + 4
    // 기본 라벨 위치 (마커 우상단), 플롯 안으로 클램프
    let lx = clampX(fx + 7)
    let ly = clampY(fz - 6)
    return { i, fx, fz, label, color, w, lx, ly, nudged: false }
  })
  // x가 가까운(라벨폭 겹침) + y도 가까운 라벨끼리 세로로 쌓기
  const LBL_H = 12
  const sortedFeat = [...featLayout].sort((p, q) => p.lx - q.lx)
  for (let a = 0; a < sortedFeat.length; a++) {
    for (let b = 0; b < a; b++) {
      const A = sortedFeat[a], B = sortedFeat[b]
      const xOverlap = A.lx < B.lx + B.w + GAP && B.lx < A.lx + A.w + GAP
      const yOverlap = Math.abs(A.ly - B.ly) < LBL_H
      if (xOverlap && yOverlap) {
        // 위로 밀되, 상단 레인/경계를 넘으면 아래로 밀기
        let ny = B.ly - LBL_H
        if (ny < plotT + LBL_H) ny = B.ly + LBL_H
        A.ly = clampY(ny)
        A.nudged = true
      }
    }
  }

  // 모든 훅 호출 이후에 early return (Rules of Hooks 준수)
  if (noValidSamples) return <div className="param-empty">유효 샘플 없음</div>

  return (
    <svg
      ref={svgRef}
      width="100%"
      viewBox={`0 0 ${W} ${H}`}
      style={{ background: '#14161a', borderRadius: 4, cursor }}
      onMouseMove={handleMouseMove}
      onMouseDown={handleMouseDown}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseLeave}
      onContextMenu={e => e.preventDefault()}
    >
      <defs>
        <clipPath id={clipId}>
          <rect x={plotL} y={plotT} width={plotR - plotL} height={plotB - plotT} />
        </clipPath>
      </defs>
      {/* 축 */}
      <line x1={padL} y1={H - padB} x2={W - padR} y2={H - padB} stroke="#555" strokeWidth={1} />
      <line x1={padL} y1={padT} x2={padL} y2={H - padB} stroke="#555" strokeWidth={1} />
      {/* 범위 라벨 */}
      <text x={4} y={padT + 8} fill="#999" fontSize={10}>{zMax.toFixed(3)}</text>
      <text x={4} y={H - padB} fill="#999" fontSize={10}>{zMin.toFixed(3)}</text>
      <text x={padL} y={H - 6} fill="#999" fontSize={10}>{xMin.toFixed(2)}</text>
      <text x={W - padR} y={H - 6} fill="#999" fontSize={10} textAnchor="end">{xMax.toFixed(2)} mm</text>

      {/* 범위 밴드 오버레이 */}
      {ranges?.map((r, i) => {
        const rx1 = Math.max(sx(r.fromMm), padL)
        const rx2 = Math.min(sx(r.toMm), W - padR)
        if (rx2 <= rx1) return null
        // z 범위가 지정되면 세로로 제한된 박스, 아니면 전체 높이 밴드
        const hasZ = r.zFrom !== undefined && r.zTo !== undefined && (r.zTo as number) > (r.zFrom as number)
        const ry1 = hasZ ? Math.min(Math.max(sy(r.zTo as number), padT), H - padB) : padT
        const ry2 = hasZ ? Math.min(Math.max(sy(r.zFrom as number), padT), H - padB) : H - padB
        return (
          <g key={`range-${i}`}>
            <rect x={rx1} y={ry1} width={rx2 - rx1} height={Math.max(0, ry2 - ry1)}
              fill={r.color + '28'} stroke={r.color} strokeWidth={sw(1)}
              strokeDasharray="4 3" opacity={0.8} />
            <rect x={rx1 + 2} y={ry1 + 2} width={r.label.length * 6 + 4} height={11}
              fill="#1e233099" rx={2} />
            <text x={rx1 + 4} y={ry1 + 11} fill={r.color} fontSize={9}>{r.label}</text>
          </g>
        )
      })}

      {/* 프로파일 데이터 */}
      {mode === 'line'
        ? segments.map((pts, i) => (
            <polyline key={i} points={pts} fill="none" stroke="#00e5ff" strokeWidth={sw(1.5)} />
          ))
        : x.map((xi, i) => {
            const zi = z[i]
            if (zi === null || Number.isNaN(zi as number)) return null
            return <circle key={i} cx={sx(xi)} cy={sy(zi as number)} r={1.5} fill="#00e5ff" />
          })}

      {/* 라인피팅 오버레이 (밝은 주황 실선, 프로파일 위로 뚜렷하게) */}
      {lineFits?.map((lf, i) => {
        const x0 = sx(lf.fromMm), x1 = sx(lf.toMm)
        const y0 = sy(lf.slope * lf.fromMm + lf.intercept)
        const y1 = sy(lf.slope * lf.toMm   + lf.intercept)
        // 뷰 전체를 가로지르는 옅은 연장선 — 점-선 거리 시 어느 선까지의 거리인지 알 수 있게.
        // 참기울기 유지(클램프 X) + plot 영역 clip으로 넘침만 잘라 줌/팬에 정확히 추종.
        const extEnabled = Number.isFinite(lf.slope) && Number.isFinite(lf.intercept)
        const ext = extEnabled ? {
          x1: sx(xMin), y1: sy(lf.slope * xMin + lf.intercept),
          x2: sx(xMax), y2: sy(lf.slope * xMax + lf.intercept),
        } : null
        // 한쪽 끝점 근처에 라벨 배치
        const lblX = x1 >= x0 ? x1 : x0
        const lblY = x1 >= x0 ? y1 : y0
        const rLabel = `R${i}`
        const rW = rLabel.length * 6 + 6
        // 배지 좌상단을 플롯 안으로 클램프
        let rectX = clampX(lblX + 3)
        if (rectX + rW > plotR) rectX = plotR - rW
        let rectY = clampY(lblY - 13)
        // 상단 주석 레인과 겹치면 아래로 살짝 오프셋
        if (rectY < ANNO_LANE_BOTTOM + 2) rectY = ANNO_LANE_BOTTOM + 2
        rectY = Math.min(rectY, plotB - 12)
        return (
          <g key={`lf-${i}`}>
            {ext && (
              <line x1={ext.x1} y1={ext.y1} x2={ext.x2} y2={ext.y2}
                stroke="#ff8f00" strokeWidth={1} strokeDasharray="3 4" opacity={0.35}
                clipPath={`url(#${clipId})`} />
            )}
            <line x1={x0} y1={y0} x2={x1} y2={y1}
              stroke="#ff8f00" strokeWidth={sw(2.5)}
              strokeLinecap="round" opacity={0.95} />
            <rect x={rectX} y={rectY} width={rW} height={12}
              fill="#1e2330cc" rx={2} />
            <text x={rectX + 3} y={rectY + 9} fill="#ff8f00" fontSize={9} fontWeight={700}>{rLabel}</text>
          </g>
        )
      })}

      {/* 피처 마커 (색상 점 + 라벨, 수직선 제거) — 라벨은 1-D 충돌 회피 후 배치 */}
      {featLayout.map((f) => (
        <g key={`feat-${f.i}`}>
          <circle cx={f.fx} cy={f.fz} r={Math.max(3.5, sw(5))} fill={f.color} stroke="#14161a" strokeWidth={sw(1)} opacity={0.95} />
          {/* 라벨이 마커에서 밀려났으면 리더선 표시 */}
          {f.nudged && (
            <line x1={f.fx} y1={f.fz} x2={f.lx} y2={f.ly - 3}
              stroke={f.color} strokeWidth={sw(1)} opacity={0.6} />
          )}
          <text x={f.lx} y={f.ly} fill={f.color} fontSize={10} fontWeight={700}>{f.label}</text>
        </g>
      ))}

      {/* 측정 주석 오버레이 (양방향 화살표 + 라벨 배지) */}
      {annotations?.map((a, i) => {
        const ax1 = sx(a.s1), ay1 = sy(a.z1)
        const ax2 = sx(a.s2), ay2 = sy(a.z2)
        const isArrow = a.kind === 'segment' || a.kind === 'perp'
        // 라벨 배지는 상단 레인으로 이동 (아래 별도 렌더). 여기서는 화살표만.
        const badge = annoBadges[i]
        const placed = annoPlaced[i]

        // 세그먼트 방향 단위벡터 (화살촉 방향용)
        const dx = ax2 - ax1, dy = ay2 - ay1
        const len = Math.hypot(dx, dy) || 1
        const ux = dx / len, uy = dy / len
        // 화살촉 크기
        const ah = 8, aw = 4
        // (px,py) 끝점에서 (dirX,dirY) 바깥 방향을 가리키는 삼각형 poly points
        const arrowHead = (px: number, py: number, dirX: number, dirY: number) => {
          // 밑변 중심은 끝점에서 안쪽으로 ah만큼
          const bx = px - dirX * ah, by = py - dirY * ah
          // 세그먼트에 수직인 벡터
          const nx = -dirY, ny = dirX
          const p1x = bx + nx * aw, p1y = by + ny * aw
          const p2x = bx - nx * aw, p2y = by - ny * aw
          return `${px.toFixed(1)},${py.toFixed(1)} ${p1x.toFixed(1)},${p1y.toFixed(1)} ${p2x.toFixed(1)},${p2y.toFixed(1)}`
        }

        return (
          <g key={`anno-${i}`}>
            {a.kind !== 'angle' && (
              <line x1={ax1} y1={ay1} x2={ax2} y2={ay2}
                stroke={a.color} strokeWidth={1.4} opacity={0.9} />
            )}
            {a.kind === 'angle' && (() => {
              const fs = 9
              const th = 14
              const tw = a.label.length * fs * 0.72 + fs
              // 교점은 가리지 않게 라벨을 위로 올리고 리더선으로 연결
              const tx = Math.min(Math.max(ax1 - tw / 2, padL), W - padR - tw)
              const ty = Math.max(ay1 - th - 22, padT + 2)
              const cx = tx + tw / 2
              return (
                <g>
                  <circle cx={ax1} cy={ay1} r={sw(3)} fill="none" stroke={a.color} strokeWidth={sw(1.2)} />
                  <line x1={ax1} y1={ay1} x2={cx} y2={ty + th}
                    stroke={a.color} strokeWidth={sw(0.7)} opacity={0.5} strokeDasharray="3 2" />
                  <rect x={tx} y={ty} width={tw} height={th} rx={2} fill="#000000cc" />
                  <text x={cx} y={ty + th * 0.72}
                    textAnchor="middle" fill={a.color} fontSize={fs} fontWeight={700}>{a.label}</text>
                </g>
              )
            })()}
            {isArrow && (
              <>
                {/* 화살촉: 시작점(안쪽 방향은 -u) */}
                <polygon points={arrowHead(ax1, ay1, -ux, -uy)} fill={a.color} opacity={0.9} />
                {/* 화살촉: 끝점(바깥 방향은 +u) */}
                <polygon points={arrowHead(ax2, ay2, ux, uy)} fill={a.color} opacity={0.9} />
              </>
            )}
            {/* 상단 레인 배지로 향하는 리더선 (angle은 직접 텍스트 처리) */}
            {placed && a.kind !== 'angle' && (
              <line x1={placed.x + placed.w / 2} y1={ANNO_LANE_BOTTOM}
                x2={badge.targetX} y2={badge.targetY}
                stroke={a.color} strokeWidth={1} opacity={0.45} />
            )}
          </g>
        )
      })}

      {/* 측정 주석 값 배지 — 상단 예약 레인에 가로 한 줄 배치 (겹침 회피) */}
      {annoBadges.map((b) => {
        const placed = annoPlaced[b.i]
        if (!placed) return null
        const origAnno = (annotations ?? [])[b.i]
        if (origAnno?.kind === 'angle') return null   // angle은 교차점 직접 텍스트로 처리
        return (
          <g key={`anno-badge-${b.i}`}>
            <rect x={placed.x} y={ANNO_LANE_Y} width={placed.w} height={ANNO_LANE_H} rx={2} fill="#000000cc" />
            <text x={placed.x + placed.w / 2} y={ANNO_LANE_Y + ANNO_LANE_H - 4}
              textAnchor="middle" fill={b.color} fontSize={9} fontWeight={700}>{b.label}</text>
          </g>
        )
      })}

      {/* 드래그 중 범위/ROI 선택 시각화 */}
      {dragRange && (
        <rect x={dragX1} y={dragY1} width={Math.max(0, dragX2 - dragX1)} height={Math.max(0, dragY2 - dragY1)}
          fill="#ffffff18" stroke="#ffffffaa" strokeWidth={1} strokeDasharray="3 2" />
      )}

      {/* 편집 가능한 ROI 박스: 밝은 테두리 + 리사이즈/이동 핸들 */}
      {editGeom && (() => {
        const ec = (editRange as ProfileRange).color
        const bx1 = sx(editGeom.fromMm), bx2 = sx(editGeom.toMm)
        const byTop = sy(editGeom.zTo), byBot = sy(editGeom.zFrom)
        const ex1 = Math.min(bx1, bx2), ex2 = Math.max(bx1, bx2)
        const ey1 = Math.min(byTop, byBot), ey2 = Math.max(byTop, byBot)
        const cxMid = (ex1 + ex2) / 2, cyMid = (ey1 + ey2) / 2
        const hs = 6 // 핸들 한 변 크기
        const handle = (hx: number, hy: number, key: string) => (
          <rect key={key} x={hx - hs / 2} y={hy - hs / 2} width={hs} height={hs}
            fill={ec} stroke="#fff" strokeWidth={1} />
        )
        return (
          <g>
            <rect x={ex1} y={ey1} width={ex2 - ex1} height={ey2 - ey1}
              fill="none" stroke={ec} strokeWidth={1.6} opacity={1} />
            {/* left, right, top, bottom edge-mid 핸들 */}
            {handle(ex1, cyMid, 'h-l')}
            {handle(ex2, cyMid, 'h-r')}
            {handle(cxMid, ey1, 'h-t')}
            {handle(cxMid, ey2, 'h-b')}
          </g>
        )
      })()}

      {/* 투명 히트 영역 */}
      <rect x={padL} y={padT} width={W - padL - padR} height={H - padT - padB} fill="transparent" />

      {/* Fix/Auto 버튼 */}
      {!fixedByUser ? (
        <g onClick={fixRange} style={{ cursor: 'pointer' }}>
          <rect x={btnX} y={btnY} width={38} height={14} rx={3} fill="#1a2030" stroke="#445" strokeWidth={1} />
          <text x={btnX + 19} y={btnY + 10} fill="#778" fontSize={9} textAnchor="middle" style={{ userSelect: 'none' }}>▶ Fix</text>
        </g>
      ) : (
        <g onClick={resetZoom} style={{ cursor: 'pointer' }}>
          <rect x={btnX} y={btnY} width={38} height={14} rx={3} fill="#1a3a2a" stroke="#00c853" strokeWidth={1} />
          <text x={btnX + 19} y={btnY + 10} fill="#00c853" fontSize={9} textAnchor="middle" style={{ userSelect: 'none' }}>↺ Auto</text>
        </g>
      )}

      {/* tooltip 오버레이 */}
      {!isPanning && !isDraggingRange && !isEditingBox && tooltip && (
        <>
          <line x1={tooltip.svgX} y1={padT} x2={tooltip.svgX} y2={H - padB}
            stroke="#ffffff44" strokeWidth={1} strokeDasharray="3 3" />
          <line x1={padL} y1={tooltip.svgY} x2={W - padR} y2={tooltip.svgY}
            stroke="#ffffff44" strokeWidth={1} strokeDasharray="3 3" />
          <circle cx={tooltip.svgX} cy={tooltip.svgY} r={3.5} fill="#fff" stroke="#00e5ff" strokeWidth={1.5} />
          <rect x={tipX} y={tipY} width={tipW} height={tipH} rx={3} fill="#1e2330ee" stroke="#00e5ff55" strokeWidth={1} />
          <text x={tipX + 6} y={tipY + 12} fill="#aaa" fontSize={9.5}>x</text>
          <text x={tipX + 16} y={tipY + 12} fill="#e0e0e0" fontSize={9.5}>{tooltip.dx.toFixed(4)} mm</text>
          <text x={tipX + 6} y={tipY + 24} fill="#aaa" fontSize={9.5}>z</text>
          <text x={tipX + 16} y={tipY + 24} fill="#00e5ff" fontSize={9.5}>{tooltip.dz.toFixed(4)}</text>
        </>
      )}
    </svg>
  )
}
