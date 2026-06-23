import { useEffect, useRef } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'

interface Props {
  a: number
  b: number
  c: number
  points: [number, number, number][]
}

const VIEW_H = 260

export default function PlaneView3D({ a, b, c, points }: Props) {
  const mountRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    const mount = mountRef.current
    if (!mount || !points || points.length === 0) return

    const W = mount.clientWidth || 300
    const H = VIEW_H

    const scene = new THREE.Scene()
    scene.background = new THREE.Color(0x14161a)

    const cam = new THREE.PerspectiveCamera(50, W / H, 0.01, 1000)
    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setSize(W, H)
    renderer.setPixelRatio(window.devicePixelRatio)
    mount.appendChild(renderer.domElement)

    const controls = new OrbitControls(cam, renderer.domElement)
    // damping 관성을 끔 — 드래그를 멈추면 즉시 정지
    controls.enableDamping = false

    // ── 바운딩 박스 → 중심화 + 등방 스케일 (실제 비율 유지) ──
    let minX = Infinity, minY = Infinity, minZ = Infinity
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity
    for (const [x, y, z] of points) {
      minX = Math.min(minX, x); maxX = Math.max(maxX, x)
      minY = Math.min(minY, y); maxY = Math.max(maxY, y)
      minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z)
    }
    const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2, cz = (minZ + maxZ) / 2
    const range = Math.max(maxX - minX, maxY - minY, maxZ - minZ) || 1
    const s = 2 / range
    // 데이터(x,y,높이z) → three(x, up=높이, z=깊이)
    const tf = (x: number, y: number, z: number): [number, number, number] =>
      [(x - cx) * s, (z - cz) * s, (y - cy) * s]

    // ── 잔차(평면 대비 편차) 색상 ──
    const resid = points.map(([x, y, z]) => z - (a * x + b * y + c))
    let maxAbs = 1e-9
    for (const r of resid) maxAbs = Math.max(maxAbs, Math.abs(r))

    const geo = new THREE.BufferGeometry()
    const pos = new Float32Array(points.length * 3)
    const col = new Float32Array(points.length * 3)
    points.forEach(([x, y, z], i) => {
      const [tx, ty, tz] = tf(x, y, z)
      pos[i * 3] = tx; pos[i * 3 + 1] = ty; pos[i * 3 + 2] = tz
      const t = resid[i] / maxAbs            // -1(아래) ~ +1(위)
      // 파랑(아래) — 흰색(0) — 빨강(위)
      col[i * 3]     = t > 0 ? 1 : 1 + t
      col[i * 3 + 1] = 1 - Math.abs(t) * 0.7
      col[i * 3 + 2] = t < 0 ? 1 : 1 - t
    })
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3))
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3))
    const pmat = new THREE.PointsMaterial({ size: 0.012, vertexColors: true })
    const cloud = new THREE.Points(geo, pmat)
    scene.add(cloud)

    // ── 피팅 평면 (x,y bbox 네 모서리) ──
    const corners: [number, number][] = [[minX, minY], [maxX, minY], [maxX, maxY], [minX, maxY]]
    const pgeo = new THREE.BufferGeometry()
    const pp = new Float32Array(4 * 3)
    corners.forEach(([x, y], i) => {
      const z = a * x + b * y + c
      const [tx, ty, tz] = tf(x, y, z)
      pp[i * 3] = tx; pp[i * 3 + 1] = ty; pp[i * 3 + 2] = tz
    })
    pgeo.setAttribute('position', new THREE.BufferAttribute(pp, 3))
    pgeo.setIndex([0, 1, 2, 0, 2, 3])
    const planeMat = new THREE.MeshBasicMaterial({
      color: 0x26a69a, transparent: true, opacity: 0.35, side: THREE.DoubleSide
    })
    scene.add(new THREE.Mesh(pgeo, planeMat))
    const edges = new THREE.LineSegments(
      new THREE.EdgesGeometry(pgeo),
      new THREE.LineBasicMaterial({ color: 0x26a69a })
    )
    scene.add(edges)

    // ── 축 + 그리드 + 라벨 (평면 기울기 방향 파악용) ──
    // three 좌표 매핑: x=데이터X, y(up)=높이Z, z=데이터Y
    scene.add(new THREE.AxesHelper(1.3))
    const grid = new THREE.GridHelper(3, 12, 0x444444, 0x2a2a2a)
    grid.position.y = -1.05
    scene.add(grid)

    const makeLabel = (text: string, color: string): THREE.Sprite => {
      const c = document.createElement('canvas')
      c.width = 64; c.height = 64
      const x = c.getContext('2d')!
      x.fillStyle = color
      x.font = 'bold 44px sans-serif'
      x.textAlign = 'center'; x.textBaseline = 'middle'
      x.fillText(text, 32, 32)
      const sp = new THREE.Sprite(new THREE.SpriteMaterial({
        map: new THREE.CanvasTexture(c), depthTest: false
      }))
      sp.scale.set(0.28, 0.28, 0.28)
      return sp
    }
    const labels = [
      { t: 'X', col: '#ff5252', pos: [1.45, 0, 0] as const },  // three X = 데이터 X
      { t: 'Z', col: '#69f0ae', pos: [0, 1.45, 0] as const },  // three Y(up) = 높이 Z
      { t: 'Y', col: '#448aff', pos: [0, 0, 1.45] as const },  // three Z = 데이터 Y
    ]
    const labelSprites = labels.map(l => {
      const sp = makeLabel(l.t, l.col)
      sp.position.set(...l.pos)
      scene.add(sp)
      return sp
    })

    cam.position.set(2.2, 1.6, 2.2)
    controls.target.set(0, 0, 0)
    controls.update()

    let raf = 0
    const animate = () => {
      raf = requestAnimationFrame(animate)
      controls.update()
      renderer.render(scene, cam)
    }
    animate()

    const ro = new ResizeObserver(() => {
      const w = mount.clientWidth || 300
      renderer.setSize(w, H)
      cam.aspect = w / H
      cam.updateProjectionMatrix()
    })
    ro.observe(mount)

    return () => {
      cancelAnimationFrame(raf)
      ro.disconnect()
      controls.dispose()
      geo.dispose(); pmat.dispose(); pgeo.dispose(); planeMat.dispose()
      grid.dispose()
      labelSprites.forEach(sp => { sp.material.map?.dispose(); sp.material.dispose() })
      renderer.dispose()
      if (renderer.domElement.parentNode === mount) mount.removeChild(renderer.domElement)
    }
  }, [a, b, c, points])

  return (
    <div className="plane-3d">
      <div ref={mountRef} className="plane-3d-canvas" />
      <div className="plane-3d-legend">
        <span><i style={{ background: '#3b82f6' }} /> 아래</span>
        <span><i style={{ background: '#e5e5e5' }} /> 평면</span>
        <span><i style={{ background: '#ef4444' }} /> 위</span>
        <span className="plane-3d-hint">드래그 회전 · 휠 줌</span>
      </div>
    </div>
  )
}
