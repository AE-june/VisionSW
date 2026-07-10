import { useState, useRef, useCallback, useEffect, useLayoutEffect, type ReactNode } from 'react'

// jet 컬러맵: 0(낮음, 파랑) → 청록 → 녹색 → 노랑 → 1(높음, 빨강)
const clamp01 = (x: number) => Math.max(0, Math.min(1, x))
function jet(t: number): [number, number, number] {
  return [
    clamp01(1.5 - Math.abs(4 * t - 3)) * 255,
    clamp01(1.5 - Math.abs(4 * t - 2)) * 255,
    clamp01(1.5 - Math.abs(4 * t - 1)) * 255,
  ]
}

export interface DrawRect { xPct: number; yPct: number; wPct: number; hPct: number }

export interface Roi {
  id: string
  type: string
  shape?: 'rect' | 'circle'   // 기본 rect
  xPct: number
  yPct: number
  wPct: number
  hPct: number
  angleDeg?: number           // 중심 기준 회전 (deg, 시계방향). 기본 0
  polarity?: string           // LineCenter 전용: 에지 극성 'd2l'(흑→백) | 'l2d'(백→흑)
}

interface DrawState { startX: number; startY: number; curX: number; curY: number }
interface Pan { x: number; y: number }

type EditMode = 'move' | 'resize' | 'rotate'
interface EditState { id: string; mode: EditMode; handle: string; startX: number; startY: number; orig: Roi }

const HANDLES = ['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w']
const clampUnit = (v: number) => Math.max(0, Math.min(1, v))

// 회전된 ROI의 리사이즈: 픽셀 공간(W=aspect, H=1)에서 로컬 프레임으로 변환해 처리.
// 드래그하지 않는 반대편 모서리/변을 고정점(anchor)으로 유지.
function resizeRotated(e: EditState, x: number, y: number, aspect: number, imgH?: number): Roi {
  const o = e.orig
  const W = aspect > 0 ? aspect : 1, H = 1
  const rad = ((o.angleDeg ?? 0) * Math.PI) / 180
  const cos = Math.cos(rad), sin = Math.sin(rad)
  const cx = (o.xPct + o.wPct / 2) * W, cy = (o.yPct + o.hPct / 2) * H
  const hw = (o.wPct * W) / 2, hh = (o.hPct * H) / 2
  const sx = e.handle.includes('e') ? 1 : e.handle.includes('w') ? -1 : 0
  const sy = e.handle.includes('s') ? 1 : e.handle.includes('n') ? -1 : 0
  // 고정점 = 반대편 모서리(로컬 → 월드)
  const aLx = -sx * hw, aLy = -sy * hh
  const ax = cx + (aLx * cos - aLy * sin), ay = cy + (aLx * sin + aLy * cos)
  // 포인터를 anchor 기준 로컬 프레임으로
  const ddx = x * W - ax, ddy = y * H - ay
  const lx = ddx * cos + ddy * sin, ly = -ddx * sin + ddy * cos
  // 최소 크기 = 1px (이 정규화 공간에선 1px = 1/imgH 단위, 정사각 픽셀 가정)
  const minLen = imgH && imgH > 0 ? 1 / imgH : 0.002
  const newW = sx !== 0 ? Math.max(Math.abs(lx), minLen) : hw * 2
  const newH = sy !== 0 ? Math.max(Math.abs(ly), minLen) : hh * 2
  const cLx = sx !== 0 ? Math.sign(lx) * newW / 2 : 0
  const cLy = sy !== 0 ? Math.sign(ly) * newH / 2 : 0
  const ncx = ax + (cLx * cos - cLy * sin), ncy = ay + (cLx * sin + cLy * cos)
  const wPct = newW / W, hPct = newH / H
  return { ...o, wPct, hPct, xPct: ncx / W - wPct / 2, yPct: ncy / H - hPct / 2 }
}

function applyEdit(e: EditState, x: number, y: number, opts?: { shift?: boolean; aspect?: number; imgW?: number; imgH?: number }): Roi {
  const o = e.orig
  if (e.mode === 'rotate') {
    const W = opts?.aspect && opts.aspect > 0 ? opts.aspect : 1
    const cx = (o.xPct + o.wPct / 2) * W, cy = o.yPct + o.hPct / 2
    const dxp = x * W - cx, dyp = y - cy
    let deg = (Math.atan2(dxp, -dyp) * 180) / Math.PI   // 핸들(로컬 위쪽)이 향하는 각도
    if (opts?.shift) deg = Math.round(deg / 15) * 15
    return { ...o, angleDeg: deg }
  }
  if (e.mode === 'resize' && (o.angleDeg ?? 0) !== 0) {
    return resizeRotated(e, x, y, opts?.aspect ?? 1, opts?.imgH)
  }
  if (e.mode === 'move') {
    const dx = x - e.startX, dy = y - e.startY
    // 회전된 ROI는 회전 전 박스가 이미지 밖으로 나갈 수 있으므로 좌상단이 아닌 '중심'을 [0,1]로 클램프.
    // (좌상단 클램프는 넓은 90° 회전 ROI가 한쪽으로 못 움직이는 문제를 유발함)
    const ncx = clampUnit(o.xPct + o.wPct / 2 + dx)
    const ncy = clampUnit(o.yPct + o.hPct / 2 + dy)
    return { ...o, xPct: ncx - o.wPct / 2, yPct: ncy - o.hPct / 2 }
  }

  // Shift + 원형 리사이즈: 반대편 모서리를 고정한 채 이미지 픽셀 공간에서 정사각(=정원)으로
  if (opts?.shift && o.shape === 'circle') {
    const ar = opts.aspect && opts.aspect > 0 ? opts.aspect : 1   // imgW/imgH
    // 드래그하지 않는 반대편 모서리를 고정점으로
    const ax = e.handle.includes('w') ? o.xPct + o.wPct : o.xPct
    const ay = e.handle.includes('n') ? o.yPct + o.hPct : o.yPct
    const hasX = e.handle.includes('e') || e.handle.includes('w')
    const hasY = e.handle.includes('n') || e.handle.includes('s')
    const pxX = hasX ? Math.abs(x - ax) * ar : 0    // 픽셀 정규화 폭
    const pxY = hasY ? Math.abs(y - ay) : 0
    const size = Math.max(pxX, pxY, 0.005)          // 정사각 한 변(픽셀 정규화)
    const w = size / ar, h = size
    const left = e.handle.includes('w') ? ax - w : ax
    const top = e.handle.includes('n') ? ay - h : ay
    return {
      ...o,
      xPct: clampUnit(left),
      yPct: clampUnit(top),
      wPct: Math.max(0.005, w),
      hPct: Math.max(0.005, h),
    }
  }

  // resize: 핸들에 포함된 방향의 모서리만 마우스 위치로 이동. 반대편(앵커) 모서리는 고정.
  // 드래그하는 모서리만 마우스를 따라가고, 뒤집힘(min/max 교차) 없이 앵커 기준으로 크기 계산.
  const iw = opts?.imgW ?? 0, ih = opts?.imgH ?? 0
  const minW = iw > 0 ? 1 / iw : 0.002   // 최소 1px
  const minH = ih > 0 ? 1 / ih : 0.002
  let xPct = o.xPct, yPct = o.yPct, wPct = o.wPct, hPct = o.hPct
  if (e.handle.includes('e')) {                       // 오른쪽 이동, 왼쪽(xPct) 고정
    wPct = Math.max(minW, x - o.xPct)
  } else if (e.handle.includes('w')) {                // 왼쪽 이동, 오른쪽(xPct+wPct) 고정
    const rightEdge = o.xPct + o.wPct
    xPct = Math.min(x, rightEdge - minW)
    wPct = rightEdge - xPct
  }
  if (e.handle.includes('s')) {                       // 아래쪽 이동, 위(yPct) 고정
    hPct = Math.max(minH, y - o.yPct)
  } else if (e.handle.includes('n')) {                // 위쪽 이동, 아래(yPct+hPct) 고정
    const bottomEdge = o.yPct + o.hPct
    yPct = Math.min(y, bottomEdge - minH)
    hPct = bottomEdge - yPct
  }
  return { ...o, xPct, yPct, wPct, hPct
  }
}

interface Props {
  preview?: string
  /** null/undefined면 팬 모드. 문자열이면 해당 타입 이름으로 ROI 그리기 (프리뷰 색상에 사용) */
  drawMode?: string | null
  /** 그리는 ROI 도형 (프리뷰 모양) */
  drawShape?: 'rect' | 'circle'
  onDrawComplete?: (rect: DrawRect) => void
  /** 표시할 ROI 목록 */
  rois?: Roi[]
  /** ROI 편집(이동/리사이즈/삭제) 활성화 — 변경 콜백. 없으면 읽기전용 */
  onRoisChange?: (rois: Roi[]) => void
  /** ROI 타입 → 라벨 텍스트 */
  roiTypeLabel?: (type: string) => string
  /** ROI별 추가 오버레이 (결과 거리 등). 같은 type 내 0-based index */
  overlayFor?: (roi: Roi, indexInType: number) => ReactNode
  /** 이미지 좌표계 위에 렌더되는 임의 오버레이. 함수면 현재 zoom(native 스케일)을 받아 렌더 */
  overlay?: ReactNode | ((zoom: number) => ReactNode)
  /** 툴바 좌측 커스텀 영역 (그리기 버튼 등) */
  toolbarLeft?: ReactNode
  /** 하단 요약/힌트 영역 */
  footer?: ReactNode
  placeholder?: ReactNode
  /** ZMap 실제 z 범위(raw count) — 컬러맵 range를 실제값 단위로 표시 */
  zMin?: number
  zMax?: number
  /** 이미지 원본 픽셀 크기 콜백 (좌표 입력용) */
  onImageSize?: (w: number, h: number) => void
  /** ROI 회전 핸들 표시 (대각선 검색용) */
  enableRotate?: boolean
  /** 표시 박스(캔버스) 높이(px). 없으면 CSS 기본값(420) */
  canvasHeight?: number
  /** 마우스 오버 좌표 표시용 분해능(mm/px). 있으면 mm 좌표도 함께 표시 */
  resXMm?: number
  resYMm?: number
}

const MAX_ZOOM = 100   // native 스케일 상한 (10000% = 원본 픽셀의 100배)
// ROI가 화면상 이보다 작으면(가로·세로 중 하나라도) 리사이즈 핸들을 숨기고 이동만 허용.
// 줌인해서 이 크기 이상 보이면 그때부터 리사이즈 핸들 노출.
const HANDLE_MIN_PX = 30

export default function ImageViewer({
  preview, drawMode, drawShape, onDrawComplete, rois, onRoisChange, roiTypeLabel, overlayFor,
  overlay, toolbarLeft, footer, placeholder, zMin, zMax, onImageSize, enableRotate, canvasHeight,
  resXMm, resYMm
}: Props) {
  const [zoom, setZoom] = useState(1)
  const [draw, setDraw] = useState<DrawState | null>(null)
  const [edit, setEdit] = useState<EditState | null>(null)
  const [imgAspect, setImgAspect] = useState<number | null>(null)
  const [imgPx, setImgPx] = useState({ w: 0, h: 0 })
  const [hover, setHover] = useState<{ col: number; row: number; val: number | null } | null>(null)   // 마우스 오버 위치(이미지 px)
  const [csize, setCsize] = useState({ w: 0, h: 0 })   // 박스(=패널 폭 × 고정 높이). 클리핑/좌표 기준
  const [colormap, setColormap] = useState(false)
  const [autoRange, setAutoRange] = useState(true)
  const [rangeLo, setRangeLo] = useState(0)
  const [rangeHi, setRangeHi] = useState(255)
  // 디스플레이(캔버스) 높이 — 하단 구분선 드래그로 상하 조절. canvasHeight는 초기값.
  const [canvasH, setCanvasH] = useState(canvasHeight ?? 420)
  const hDragRef = useRef<{ startY: number; startH: number } | null>(null)
  const startHDrag = (e: React.MouseEvent) => {
    e.preventDefault()
    hDragRef.current = { startY: e.clientY, startH: canvasH }
    const onMove = (ev: MouseEvent) => {
      if (!hDragRef.current) return
      const h = hDragRef.current.startH + (ev.clientY - hDragRef.current.startY)
      setCanvasH(Math.max(150, Math.min(1000, h)))
    }
    const onUp = () => {
      hDragRef.current = null
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
  }
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const zoomRef = useRef(1)
  // 스크롤(팬) 드래그 시작 지점 저장
  const panStartRef = useRef<{ mx: number; my: number; sl: number; st: number } | null>(null)
  const containerRef = useRef<HTMLDivElement>(null)   // 스크롤 박스(좌표 기준 & 클리핑)
  const contentRef = useRef<HTMLDivElement>(null)     // 이미지 크기의 콘텐츠(스크롤 대상)
  // 휠 줌 후 커서 고정을 위해 적용할 스크롤 위치를 layout 단계에서 반영
  const pendingScrollRef = useRef<{ sl: number; st: number } | null>(null)
  const boxRef = useRef({ w: 0, h: 0 })
  const imgPxRef = useRef({ w: 0, h: 0 })

  // zoom을 state + ref 동시 적용
  const applyZoom = useCallback((z: number) => { zoomRef.current = z; setZoom(z) }, [])

  // 전체보기 배율: 이미지 전체가 박스에 들어오는 native 스케일 (100%=원본 1:1)
  const fitZoom = (box: { w: number; h: number }, img: { w: number; h: number }) =>
    box.w > 0 && box.h > 0 && img.w > 0 && img.h > 0
      ? Math.min(box.w / img.w, box.h / img.h) : 1

  // 콘텐츠(이미지 표시) 크기 및 박스 안에서의 가운데 정렬 여백
  const contentBox = (box: { w: number; h: number }, img: { w: number; h: number }, zoom: number) => {
    const w = (img.w > 0 ? img.w : box.w) * zoom, h = (img.h > 0 ? img.h : box.h) * zoom
    return { w, h, mL: Math.max(0, (box.w - w) / 2), mT: Math.max(0, (box.h - h) / 2) }
  }

  // client 좌표 → 이미지 상대 퍼센트(0~1). 콘텐츠 rect는 스크롤/여백이 이미 반영됨.
  const toImgPct = useCallback((clientX: number, clientY: number) => {
    const c = contentRef.current
    if (!c) return { x: 0, y: 0 }
    const cr = c.getBoundingClientRect()
    return {
      x: cr.width > 0 ? (clientX - cr.left) / cr.width : 0,
      y: cr.height > 0 ? (clientY - cr.top) / cr.height : 0,
    }
  }, [])

  // ── 휠 줌: 커서 위 이미지 지점을 줌 후에도 커서에 고정 (스크롤 위치 보정) ──
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    const handler = (e: WheelEvent) => {
      e.preventDefault()
      e.stopPropagation()
      const r = el.getBoundingClientRect()
      const cx = e.clientX - r.left, cy = e.clientY - r.top
      const z = zoomRef.current
      const box = { w: r.width, h: r.height }, img = imgPxRef.current
      const lo = Math.min(fitZoom(box, img), MAX_ZOOM)   // 축소 하한 = 전체보기
      const nz = Math.max(lo, Math.min(MAX_ZOOM, z * Math.exp(-e.deltaY * 0.0015)))
      if (nz === z) return
      // 커서 아래 이미지 pct
      const cur = contentRef.current?.getBoundingClientRect()
      const u = cur && cur.width > 0 ? (e.clientX - cur.left) / cur.width : 0.5
      const v = cur && cur.height > 0 ? (e.clientY - cur.top) / cur.height : 0.5
      // 줌 후 콘텐츠에서 커서가 같은 지점을 가리키도록 스크롤 목표 계산
      const nb = contentBox(box, img, nz)
      pendingScrollRef.current = { sl: nb.mL + u * nb.w - cx, st: nb.mT + v * nb.h - cy }
      applyZoom(nz)
    }
    el.addEventListener('wheel', handler, { passive: false })
    return () => el.removeEventListener('wheel', handler)
  }, [applyZoom])

  // 줌 변경 후 커서 고정 스크롤 반영 (렌더 완료 후 동기 적용)
  useLayoutEffect(() => {
    const el = containerRef.current
    const ps = pendingScrollRef.current
    if (el && ps) {
      el.scrollLeft = ps.sl
      el.scrollTop = ps.st
      pendingScrollRef.current = null
    }
  }, [zoom])

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0) return
    if (drawMode) {
      e.preventDefault()
      const { x, y } = toImgPct(e.clientX, e.clientY)
      setDraw({ startX: x, startY: y, curX: x, curY: y })
    } else {
      const el = containerRef.current
      panStartRef.current = { mx: e.clientX, my: e.clientY, sl: el?.scrollLeft ?? 0, st: el?.scrollTop ?? 0 }
    }
  }, [drawMode, toImgPct])

  const onMouseMove = useCallback((e: React.MouseEvent) => {
    // 마우스 오버 좌표(이미지 px) 갱신 — 이미지 영역 안일 때만
    const p = toImgPct(e.clientX, e.clientY)
    const img = imgPxRef.current
    if (img.w > 0 && p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1) {
      const col = p.x * img.w, row = p.y * img.h
      let val: number | null = null
      const cv = canvasRef.current
      if (cv) {
        const ctx = cv.getContext('2d')
        if (ctx) {
          const px = ctx.getImageData(Math.floor(col), Math.floor(row), 1, 1).data
          const gray = px[0]  // R채널 = grayscale
          val = (zMin !== undefined && zMax !== undefined)
            ? (gray === 0 ? null : zMin + (gray / 255) * (zMax - zMin))
            : gray
        }
      }
      setHover({ col, row, val })
    } else {
      setHover(null)
    }
    if (draw) {
      let { x, y } = toImgPct(e.clientX, e.clientY)
      // Shift + 원형: 정원 (이미지 픽셀 공간에서 가로=세로)
      if (e.shiftKey && drawShape === 'circle') {
        const ar = imgAspect ?? (csize.h > 0 ? csize.w / csize.h : 1)  // imgW/imgH
        const dx = x - draw.startX, dy = y - draw.startY
        const size = Math.max(Math.abs(dx) * ar, Math.abs(dy))  // 픽셀 정규화 거리
        x = draw.startX + (dx < 0 ? -1 : 1) * size / ar
        y = draw.startY + (dy < 0 ? -1 : 1) * size
      }
      setDraw(d => d ? { ...d, curX: x, curY: y } : null)
    } else if (panStartRef.current) {
      // 드래그로 스크롤(팬)
      const el = containerRef.current
      if (el) {
        el.scrollLeft = panStartRef.current.sl - (e.clientX - panStartRef.current.mx)
        el.scrollTop = panStartRef.current.st - (e.clientY - panStartRef.current.my)
      }
    }
  }, [draw, drawShape, csize, imgAspect, toImgPct])

  const onMouseUp = useCallback(() => {
    panStartRef.current = null
    if (!draw) return
    const xPct = Math.min(draw.startX, draw.curX)
    const yPct = Math.min(draw.startY, draw.curY)
    const wPct = Math.abs(draw.curX - draw.startX)
    const hPct = Math.abs(draw.curY - draw.startY)
    setDraw(null)
    // 최소 크기 판정을 화면 픽셀 기준으로 — 확대 상태에서도 작은 드래그로 ROI를 추가할 수 있게
    const cb = contentBox(boxRef.current, imgPxRef.current, zoomRef.current)
    if (wPct * cb.w < 4 || hPct * cb.h < 4) return
    onDrawComplete?.({
      xPct: Math.max(0, xPct),
      yPct: Math.max(0, yPct),
      wPct: Math.min(wPct, 1 - Math.max(0, xPct)),
      hPct: Math.min(hPct, 1 - Math.max(0, yPct)),
    })
  }, [draw, onDrawComplete, csize])

  // 리셋 = 전체보기(fit) (여백으로 가운데 정렬됨)
  const resetView = () => applyZoom(fitZoom(boxRef.current, imgPxRef.current))

  // ZMap 실제 z 범위가 도착하면 수동 range 초기값을 그 범위로 맞춤
  useEffect(() => {
    if (zMin !== undefined && zMax !== undefined) { setRangeLo(zMin); setRangeHi(zMax) }
  }, [zMin, zMax])

  // 박스(컨테이너) 크기 추적 — 폭=패널 폭(패널을 늘리면 따라감), 높이=고정. 클리핑/좌표 기준.
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    const update = () => {
      const r = el.getBoundingClientRect()
      setCsize({ w: r.width, h: r.height })
      boxRef.current = { w: r.width, h: r.height }
    }
    update()
    const ro = new ResizeObserver(update)
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  // 이미지 픽셀 크기를 휠/좌표 핸들러에서 읽을 수 있게 ref 동기화 + 처음 준비되면 전체보기로 맞춤
  const fittedRef = useRef(false)
  useEffect(() => {
    imgPxRef.current = imgPx
    if (imgPx.w > 0 && csize.w > 0 && !fittedRef.current) {
      fittedRef.current = true
      applyZoom(fitZoom(csize, imgPx))
    }
  }, [imgPx, csize.w, csize.h, applyZoom])

  // ROI 이동/리사이즈 드래그 — window 리스너로 캔버스 밖까지 추적
  useEffect(() => {
    if (!edit || !onRoisChange || !rois) return
    const aspect = imgAspect ?? (csize.h > 0 ? csize.w / csize.h : 1)
    const onMove = (ev: MouseEvent) => {
      const { x, y } = toImgPct(ev.clientX, ev.clientY)
      onRoisChange(rois.map(r => r.id === edit.id
        ? applyEdit(edit, x, y, { shift: ev.shiftKey, aspect, imgW: imgPx.w, imgH: imgPx.h })
        : r))
    }
    const onUp = () => setEdit(null)
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [edit, rois, onRoisChange, toImgPct, imgAspect, csize.w, csize.h, imgPx])

  // preview(grayscale PNG)를 canvas에 그리고, colormap이면 jet 컬러맵 적용
  useEffect(() => {
    const cv = canvasRef.current
    if (!preview || !cv) return
    const img = new Image()
    img.onload = () => {
      if (img.naturalHeight > 0) setImgAspect(img.naturalWidth / img.naturalHeight)
      // 새 이미지가 로드되면 전체보기로 다시 맞추도록 플래그 리셋
      if (imgPxRef.current.w !== img.naturalWidth || imgPxRef.current.h !== img.naturalHeight)
        fittedRef.current = false
      setImgPx({ w: img.naturalWidth, h: img.naturalHeight })
      onImageSize?.(img.naturalWidth, img.naturalHeight)
      cv.width = img.naturalWidth
      cv.height = img.naturalHeight
      const ctx = cv.getContext('2d')
      if (!ctx) return
      ctx.drawImage(img, 0, 0)
      if (colormap) {
        // 8bit gray(0~255)를 실제 z(raw)로 역산한 뒤 [lo,hi] 범위로 매핑
        const hasRange = zMin !== undefined && zMax !== undefined
        const lo = autoRange ? (hasRange ? zMin! : 0) : rangeLo
        const hi = autoRange ? (hasRange ? zMax! : 255) : rangeHi
        const span = Math.max(1e-6, hi - lo)
        const id = ctx.getImageData(0, 0, cv.width, cv.height)
        const d = id.data
        for (let i = 0; i < d.length; i += 4) {
          const value = hasRange ? zMin! + (d[i] / 255) * (zMax! - zMin!) : d[i]
          const t = clamp01((value - lo) / span)
          const [r, g, b] = jet(t)
          d[i] = r; d[i + 1] = g; d[i + 2] = b
        }
        ctx.putImageData(id, 0, 0)
      }
    }
    img.src = `data:image/jpeg;base64,${preview}`
  }, [preview, colormap, autoRange, rangeLo, rangeHi, zMin, zMax])

  const modeClass = drawMode ? ' pfe-mode-draw' : ' pfe-mode-pan'

  // 콘텐츠(이미지 표시) 크기 + 가운데 여백. 콘텐츠가 박스보다 크면 스크롤바가 생김.
  const cbox = contentBox(csize, imgPx, zoom)
  // ROI/그리기 좌표는 콘텐츠(이미지) 기준 0~1 → 콘텐츠 내부 px
  const drawStyle = draw ? {
    left:   `${Math.min(draw.startX, draw.curX) * cbox.w}px`,
    top:    `${Math.min(draw.startY, draw.curY) * cbox.h}px`,
    width:  `${Math.abs(draw.curX - draw.startX) * cbox.w}px`,
    height: `${Math.abs(draw.curY - draw.startY) * cbox.h}px`,
  } : null

  return (
    <div className="pfe-root">
      <div className="pfe-toolbar">
        {toolbarLeft}
        <div className="pfe-toolbar-right">
          <button
            className={`pfe-btn${colormap ? ' active' : ''}`}
            onClick={() => setColormap(v => !v)}
            title="컬러맵(높이) 토글"
          >🌡</button>
          <span className="pfe-zoom-label">{Math.round(zoom * 100)}%</span>
          <button className="pfe-btn" onClick={resetView} title="줌 리셋">↺</button>
        </div>
      </div>

      {colormap && (() => {
        const sMin = zMin ?? 0, sMax = zMax ?? 255
        const step = Math.max(1, Math.round((sMax - sMin) / 255))
        const setHi = (v: number) => setRangeHi(Math.max(v, rangeLo + step))
        const setLo = (v: number) => setRangeLo(Math.min(v, rangeHi - step))
        // 컬러맵을 [min,max] 구간에만 분포 — 바깥은 범위 밖 색(검정). 슬라이더 위치와 정렬.
        const fullSpan = Math.max(1e-6, sMax - sMin)
        const loPct = clamp01((rangeLo - sMin) / fullSpan) * 100
        const hiPct = clamp01((rangeHi - sMin) / fullSpan) * 100
        const cmStops = '#0000ff, #00bcd4, #00e676, #ffeb3b, #ff5252'
        const rangeBg = `linear-gradient(to right,` +
          ` #000 0%, #000 ${loPct}%,` +
          ` ${cmStops.split(',').map((c, i, a) => `${c.trim()} ${(loPct + (hiPct - loPct) * i / (a.length - 1)).toFixed(2)}%`).join(', ')},` +
          ` #000 ${hiPct}%, #000 100%)`
        return (
          <div className="pfe-colormap-bar">
            <label className="pfe-cm-auto">
              <input type="checkbox" checked={autoRange}
                onChange={e => setAutoRange(e.target.checked)} /> 자동
            </label>
            {autoRange ? (
              <div className="pfe-cm-gradient" />
            ) : (
              <div className="pfe-cm-stack">
                {/* 위 슬라이더 = 상한 */}
                <div className="pfe-cm-srow">
                  <input type="range" min={sMin} max={sMax} step={step} value={rangeHi}
                    onChange={e => setHi(parseFloat(e.target.value))} />
                  <input type="number" className="pfe-cm-num" value={Math.round(rangeHi)}
                    onChange={e => setHi(parseFloat(e.target.value) || 0)} />
                </div>
                <div className="pfe-cm-gradient" style={{ background: rangeBg }} />
                {/* 아래 슬라이더 = 하한 */}
                <div className="pfe-cm-srow">
                  <input type="range" min={sMin} max={sMax} step={step} value={rangeLo}
                    onChange={e => setLo(parseFloat(e.target.value))} />
                  <input type="number" className="pfe-cm-num" value={Math.round(rangeLo)}
                    onChange={e => setLo(parseFloat(e.target.value) || 0)} />
                </div>
              </div>
            )}
          </div>
        )
      })()}

      <div
        ref={containerRef}
        className={`pfe-canvas${modeClass}`}
        style={{ height: `${canvasH}px` }}
        onMouseDown={onMouseDown}
        onMouseMove={onMouseMove}
        onMouseUp={onMouseUp}
        onMouseLeave={() => { onMouseUp(); setHover(null) }}
      >
        {preview ? (
          // 콘텐츠 = 이미지 표시 크기. 박스보다 크면 스크롤바가 생기고, 작으면 여백으로 가운데 정렬.
          <div ref={contentRef} className="pfe-content"
            style={{ width: `${cbox.w}px`, height: `${cbox.h}px`,
              marginLeft: `${cbox.mL}px`, marginTop: `${cbox.mT}px` }}>
            <canvas ref={canvasRef} className="pfe-bg"
              style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }} />

            {drawStyle && drawMode && (
              <div className={`pfe-roi pfe-roi-${drawMode} pfe-roi-drawing`}
                style={{ ...drawStyle, position: 'absolute',
                  borderRadius: drawShape === 'circle' ? '50%' : undefined, borderWidth: '2px' }} />
            )}

            {/* 오버레이(라인/중심/화살표)는 콘텐츠(이미지)에 정렬. 함수면 현재 zoom 전달(크기 고정용) */}
            {overlay && (
              <div style={{ position: 'absolute', inset: 0, pointerEvents: 'none' }}>
                {typeof overlay === 'function' ? overlay(zoom) : overlay}
              </div>
            )}

            <div className="pfe-roi-layer">
          {rois?.map((roi, i, arr) => {
            const idxInType = arr.slice(0, i).filter(r => r.type === roi.type).length
            const label = `${roiTypeLabel?.(roi.type) ?? roi.type} ${idxInType + 1}`
            const editable = !!onRoisChange && !drawMode
            const left = roi.xPct * cbox.w
            const top = roi.yPct * cbox.h
            const width = roi.wPct * cbox.w
            const height = roi.hPct * cbox.h
            const angle = roi.angleDeg ?? 0
            // 화면상 충분히 커야 리사이즈/회전 핸들 노출 (작으면 이동만)
            const resizable = editable && width >= HANDLE_MIN_PX && height >= HANDLE_MIN_PX
            return (
              <div
                key={roi.id}
                className={`pfe-roi pfe-roi-${roi.type}${editable ? ' pfe-roi-editable' : ''}`}
                style={{ left: `${left}px`, top: `${top}px`, width: `${width}px`, height: `${height}px`,
                  borderRadius: roi.shape === 'circle' ? '50%' : undefined,
                  transform: angle ? `rotate(${angle}deg)` : undefined,
                  transformOrigin: 'center',
                  pointerEvents: drawMode ? 'none' : 'auto' }}
                onMouseDown={editable ? (e => {
                  e.stopPropagation(); e.preventDefault()
                  const { x, y } = toImgPct(e.clientX, e.clientY)
                  setEdit({ id: roi.id, mode: 'move', handle: '', startX: x, startY: y, orig: roi })
                }) : undefined}
              >
                <span className="pfe-roi-label"
                  style={angle ? { transform: `rotate(${-angle}deg)`, transformOrigin: 'left top' } : undefined}>{label}</span>
                {editable && (
                  <button className="pfe-roi-del"
                    onMouseDown={e => e.stopPropagation()}
                    onClick={e => { e.stopPropagation(); onRoisChange!(rois.filter(r => r.id !== roi.id)) }}
                  >×</button>
                )}
                {overlayFor && (
                  <div className="pfe-roi-overlay">{overlayFor(roi, idxInType)}</div>
                )}
                {resizable && enableRotate && roi.shape !== 'circle' && (
                  <div className="pfe-roi-rot" title="드래그하여 회전 (Shift: 15° 스냅)"
                    onMouseDown={e => {
                      e.stopPropagation(); e.preventDefault()
                      setEdit({ id: roi.id, mode: 'rotate', handle: '', startX: 0, startY: 0, orig: roi })
                    }} />
                )}
                {resizable && HANDLES.map(h => (
                  <div key={h} className={`pfe-roi-handle h-${h}`}
                    onMouseDown={e => {
                      e.stopPropagation(); e.preventDefault()
                      setEdit({ id: roi.id, mode: 'resize', handle: h, startX: 0, startY: 0, orig: roi })
                    }} />
                ))}
              </div>
            )
          })}
            </div>
          </div>
        ) : (
          <div className="pfe-placeholder">
            {placeholder ?? <>상류 노드를 실행하면<br /><small>이미지가 여기에 표시됩니다</small></>}
          </div>
        )}
      </div>

      {/* 디스플레이 높이 조절 — 캔버스 바로 아래 구분선을 상하로 드래그 */}
      {preview && (
        <div className="pfe-hsplit" onMouseDown={startHDrag} title="드래그하여 디스플레이 높이 조절">
          <span className="pfe-hsplit-grip" />
        </div>
      )}

      {/* 마우스 오버 좌표 — 디스플레이 패널 바로 아래 별도 줄 */}
      <div className="pfe-coord-bar">
        {hover ? (
          <>
            <span>px: ({Math.round(hover.col)}, {Math.round(hover.row)})</span>
            {resXMm && resYMm && (
              <span>mm: ({(hover.col * resXMm).toFixed(3)}, {(hover.row * resYMm).toFixed(3)})</span>
            )}
            {hover.val !== null && (
              <span>val: {(zMin !== undefined && zMax !== undefined)
                ? hover.val.toFixed(1)
                : Math.round(hover.val)}</span>
            )}
          </>
        ) : (
          <span className="pfe-coord-bar-hint">이미지 위에 마우스를 올리면 좌표 표시</span>
        )}
      </div>

      {footer && <div className="pfe-roi-summary">{footer}</div>}
    </div>
  )
}
