# 노치 포인트클라우드 필터 — C++ 구현 플랜

원통형 배터리 캔캡 노치 두께측정용. SmartRay ECCO X025 의 Unfiltered Point Cloud
(컬럼당 다중 피크 후보 포함)를 입력으로 받아, **리플렉션/병합피크를 제거하고
랜드(상면) + 노치 바닥 [+ 벽] 만 남긴 포인트클라우드**를 출력한다.
바닥 확정 로직은 두 가지 방식을 선택 가능하게 구현한다.

---

## 0. 배경 수치 (실측 데이터에서 확인된 값 — 상수 기본값 근거)

이 값들은 실제 스캔 데이터(exp60/th40, exp120/th40, exp120/th10, 36000 프로파일 ×
1410~1860만 점) 분석에서 나온 것이므로 기본 파라미터 값의 근거로 사용한다.

| 항목 | 값 |
|---|---|
| 횡방향(lateral, y) 출력 피치 | 6.3 µm |
| 스캔방향(transport, x) 피치 | 3.998 µm (PLY 헤더 `transportResolution`) |
| 노치 깊이 | 약 400 ~ 430 µm (기준면 정의에 따라) |
| 노치 개구폭 | 약 470 ~ 500 µm |
| 벽 10–90% 전이폭 | 30 ~ 62 µm |
| 코너 침하(아티팩트) 깊이 | 바닥보다 100 ~ 130 µm 더 깊게 찍힘 |
| 컬럼당 후보 2개 이상 비율 | 약 20 % (랜드/벽/바닥 모두 비슷) |
| 후보쌍 중 z간격 < 5 µm 비율 | 63 ~ 65 % (같은 피크의 분열 → 병합 대상) |
| 후보쌍 중 z간격 > 100 µm 비율 | 9 ~ 14 % (실제 다른 면) |
| llt (laser line thickness, 행) | 랜드 중앙값 8 (p90 11) / 바닥 16 / 벽 4 (p90 7~8) |
| 원주 편심 | ±331 µm (1회전 1주기) |

---

## 1. 입출력 규격

### 입력
- SmartRay Studio 에서 export 한 **binary_little_endian PLY**
- 정점 속성 순서 고정: `float x, float y, float z, ushort intensity, ushort llt`
  - `x` = 스캔(transport) 방향 [mm]
  - `y` = 횡방향(lateral, 노치를 가로지르는 방향) [mm]
  - `z` = 높이 [mm]
  - `intensity` = 피크 강도 (0~255 범위로 관측됨, 자료형은 ushort)
  - `llt` = 레이저 라인 두께 (카메라 행 단위, 3~63)
- 헤더 comment 안에 JSON 파라미터 블록이 있으면 `transportResolution` 을 파싱해 사용,
  없으면 CLI 인자로 받는다.

### 출력
- 동일 형식 PLY + `uchar cls` 속성 1개 추가
  - `cls = 1` 랜드(상면)
  - `cls = 2` 노치 바닥
  - `cls = 3` 벽 (옵션 활성 시에만)
  - 리플렉션/병합피크로 분류된 점은 **출력에 포함하지 않음**
- 부가 출력 (CSV 권장): 프로파일별 측정 결과
  `profile_index, x_mm, land_ref_z, floor_z, floor_center_y, depth_um, floor_width_um, corner_left_y, corner_right_y, valid_flag, method_used`

### CLI
```
notch_filter <in.ply> <out.ply>
    --method flat|corner        (기본 flat)
    --keep-wall                 (벽 점 출력 포함)
    --csv <out.csv>
    --transport-res <mm>        (헤더에 없을 때)
    --config <params.json>      (아래 파라미터 오버라이드)
```

---

## 2. 자료구조

```cpp
struct Point {                 // 입력 정점 (POD, 파일 레이아웃과 일치시켜 read 가능)
    float x, y, z;
    uint16_t intensity;
    uint16_t llt;
};

struct Candidate {             // 프로파일 처리용 (컬럼 인덱스 부여 후)
    int32_t col;               // round(y / LATERAL_PITCH)
    float y, z;
    float rel;                 // 랜드 기준 상대높이 [µm], 음수 = 아래
    uint16_t intensity, llt;
    uint32_t src_index;        // 원본 배열 인덱스 (라벨 되쓰기용)
};

struct ProfileResult {
    bool valid = false;
    double land_coef[4];       // 랜드 다항식 계수 (deg 3)
    double notch_lo_y, notch_hi_y;   // 노치 개구 좌우 경계
    double floor_center_y;
    double floor_z_rel;        // 랜드 대비 [µm], 음수
    double floor_width_um;
    double corner_left_y, corner_right_y;   // corner 방식에서만 유효
    double land_llt_p95;
};
```

프로파일 인덱스: `pi = lround(x / transport_res)`.
입력을 `pi` 로 **stable sort** 후, 각 프로파일의 `[start, end)` 구간을
`std::lower_bound` / `upper_bound` 로 만들어 둔다. (프로파일 수 약 36,000)

---

## 3. 파이프라인 전체 흐름

```
[1] PLY 읽기 → Point 배열
[2] 프로파일 인덱스 계산 → stable sort → 프로파일 구간 인덱스 생성
[3] Pass 1 : 프로파일별 독립 해석 (병렬화 대상)
      3-1 컬럼별 최상단 후보 추출 (upper envelope)
      3-2 랜드 다항식 강건 피팅 → rel 계산
      3-3 노치 개구 구간 검출
      3-4 바닥 확정  ← 여기서 method 분기 (flat / corner)
      3-5 랜드 llt p95 계산 (벽 판정용)
[4] Pass 1.5 : 원주 방향 안정화 (프로파일 간 이동 median)
[5] Pass 2 : 전체 후보 라벨링 (병렬화 대상)
      5-1 랜드 / 바닥 / 벽 분류
      5-2 컬럼 내 분열 피크 병합
      5-3 나머지 폐기
[6] PLY 쓰기 + CSV 쓰기
```

Pass 1, Pass 2 는 프로파일 단위로 완전히 독립적이므로 OpenMP `#pragma omp parallel for`
로 병렬화한다. Pass 1.5 는 순차 처리(가볍다).

---

## 4. Pass 1 상세

### 3-1 컬럼별 최상단 후보 (공통)

각 프로파일에서 `col = lround(y / 0.0063)` 계산.
동일 `col` 내에서 **z 최대(가장 얕은 = 센서에 가까운)** 점 1개만 남긴 배열
`env[]` 를 만든다. y 오름차순 정렬.

> 근거: 리플렉션은 광로가 더 길어 항상 실제 면보다 z가 낮게(더 깊게) 찍힌다.
> 따라서 "최상단 우선"은 구조적으로 리플렉션을 배제한다.

정렬은 `col` 오름차순 + `z` 내림차순으로 한 번 정렬한 뒤 각 col의 첫 원소만 취하면 된다.

### 3-2 랜드 강건 피팅 (공통)

`env[]` 에 대해 **3차 다항식 반복 트림 피팅**:

```
keep = all true
repeat 4 times:
    coef = polyfit(y[keep], z[keep], deg=3)
    res  = z - poly(coef, y)
    s    = 1.4826 * median(|res - median(res)|)      // robust sigma
    keep = (res > -3s) && (res < 3s)
```

- 노치는 아래로만 벗어나므로 트림이 노치를 자연히 제외한다.
- deg 3 이유: 캔캡 상면이 곡면이고 좌우 랜드 기울기가 다르다(좌 −5.5°, 우 −0.8°).
  실측에서 좌우 랜드 높이차가 −57 µm 로 일정하게 나타났다.
- 최소소요 점수 미달(`keep.count() < deg+6`) 시 프로파일 invalid.
- `polyfit` 은 정규방정식(4×4) + Cholesky 로 직접 구현하거나 Eigen 사용.
  Eigen 을 쓰면 `A.householderQr().solve(b)` 로 간단하다.

이후 모든 후보에 대해 `rel = (z - poly(coef, y)) * 1000.0` [µm] 를 계산.

### 3-3 노치 개구 구간 (공통)

`env[]` 에서 `rel < NOTCH_TRIG_UM (기본 -150)` 인 컬럼들을 표시.
**최장 연속 구간(run)** 을 선택 (중간에 y 간격이 `MAX_GAP_UM = 50` 을 넘으면 끊김으로 처리).
그 구간의 좌우 끝 y → `notch_lo_y`, `notch_hi_y`.
연속 구간 길이가 `MIN_NOTCH_COLS = 20` 미만이면 invalid.

### 3-4A 방식 1 — 평탄도 탐색 (`--method flat`)

노치 구간 안에서 **폭 `FLOOR_WIN_UM = 150` µm 창을 6 µm 씩 이동**시키며,
창 안 `env[]` 의 `rel` 에 대해 `spread = p95 - p05` 를 계산.
`spread` 가 최소가 되는 창을 바닥으로 확정.

제약:
- 창 안 점 개수 ≥ 12
- 창 안 `rel` 의 최댓값이 `NOTCH_TRIG_UM` 보다 커지면(=랜드에 걸치면) 후보 제외
- 탐색 범위: `notch_lo_y - 50µm` ~ `notch_hi_y + 50µm`

결과: `floor_center_y = 창 중심`, `floor_z_rel = median(창 안 rel)`,
`floor_width_um = FLOOR_WIN_UM`

> 실측 반복성: 깊이 σ 11.8 µm (exp120/th10), 15.1 µm (exp60/th40)
> 장점: 벽이 없어도 동작 (스마트레이는 벽이 33% 프로파일에서만 나옴)
> 단점: "바닥이 평평하다"는 가정에 의존, 바닥 폭을 고정값으로 가정

### 3-4B 방식 2 — 코너 검출 (`--method corner`)

**(a) 횡방향 스무딩** — `env[]` 의 `rel` 에 이동평균 적용.
창 폭 = `SMOOTH_COLS = 3` (기본, 약 19 µm).

> ★ 중요 제약: 이 값은 **반드시 코너 전이폭(30~60 µm)보다 좁아야 한다.**
> 실측 결과 스무딩 폭에 따른 깊이 σ / 개구폭:
> | 스무딩 | 깊이 σ | 개구폭 |
> |---|---|---|
> | 6.3 µm (없음) | 44.0 / 21.8 µm | 416 / 348 µm |
> | **18.9 µm** | **17.9 / 17.7** | 450 / 361 |
> | 31.5 µm | 16.4 / 23.6 | 482 / 373 |
> | 56.7 µm | 18.5 / 29.8 | 567 / 428 |
> | 94.5 µm | 38.7 / 48.8 | **635 / 489 (개구폭 부풀림)** |
>
> 57 µm 이상에서 코너가 뭉개져 개구폭이 부풀고 깊이가 얕아진다.
> `SMOOTH_COLS` 는 3~5 (19~32 µm) 로 clamp 하고, 그 이상 값은 경고를 출력한다.

**(b) 좌우 각각 코너 탐색** — 노치 중심 추정값에서 바깥쪽으로 진행:

```
for side in {left(-1), right(+1)}:
    prev_slope = NaN
    for c = center_guess ; |c - center_guess| < 500µm ; c += side * SLOPE_STEP_UM(20):
        m = { env : |y - c| < SLOPE_HALFWIN_UM(30) }
        if m.count() < 5: continue
        slope = linear_fit_slope(y[m], rel[m])
        if prev_slope valid && |slope| < |prev_slope| * SLOPE_DROP_RATIO(0.35):
            corner[side] = c ; break
        prev_slope = slope
```

`center_guess` 는 `(notch_lo_y + notch_hi_y)/2` 에서 각각 ∓50 µm 지점에서 시작.

**(c) 바닥 산출** — 두 코너 사이 구간만 사용:
```
floor_center_y = (corner_left + corner_right) / 2
floor_width_um = (corner_right - corner_left) * 1000
floor_z_rel    = median(env.rel where corner_left < y < corner_right)
```
코너 둘 중 하나라도 못 찾으면 → **방식 1(평탄도)로 폴백**하고 `method_used` 컬럼에 기록.

> 실측 반복성: 깊이 σ 17.7~17.9 µm (스무딩 3컬럼)
> 장점: 문턱값·평탄 가정에 덜 의존, 바닥 폭을 실측, 코너 위치를 직접 얻음
> 단점: 벽 점이 희박하면 기울기 추정이 불안 → 폴백 필요 (스마트레이 기준 폴백률 상당)

### 3-5 랜드 llt p95 (공통)

`|rel| < LAND_TOL_UM (30)` 인 **모든 후보**(env 아님)의 `llt` 에서 p95 를 구해 저장.
벽 판정 임계값으로 쓴다.

---

## 5. Pass 1.5 — 원주 방향 안정화

노치는 **연속된 원형 홈**이므로 프로파일 간 값이 급변할 수 없다.
`floor_center_y`, `floor_z_rel`, `notch_lo_y`, `notch_hi_y`, `corner_left_y`, `corner_right_y`
각각에 대해:

1. **NaN 무시 이동 median** (창 `XSMOOTH = 51` 프로파일 ≈ 204 µm 호길이) 로 참조값 생성
2. invalid 프로파일은 참조값으로 채움
3. 참조값과의 편차가 임계를 넘으면 참조값으로 교체
   - `floor_center_y` : 편차 > 50 µm → 교체
   - `floor_z_rel`    : 편차 > 60 µm → 교체
4. 교체된 프로파일은 CSV `valid_flag` 에 별도 코드로 표기 (2 = 이웃값으로 보정)

> 구현 팁: 이동 median 은 창 51 이므로 단순 `nth_element` 반복으로 충분하다
> (36,000 × 51 = 180만 회, 무시할 부하). 정렬 유지 컨테이너까지 갈 필요 없음.

**주의**: 원주 시작/끝은 실제로 이어져 있으나(회전 스캔), 스캔 시작·종료 구간은
데이터가 불안정하므로 **순환(wrap) 처리하지 말고** 양 끝 각 200 프로파일은
`valid_flag = 3` (경계, 측정 제외 권장) 으로 표기한다.

---

## 6. Pass 2 — 라벨링

프로파일별로 모든 후보를 순회하며 분류한다. (env 가 아닌 **전체 후보**)

```
in_notch = (y > notch_lo_y - 20µm) && (y < notch_hi_y + 20µm)

// 랜드
if (!in_notch && |rel| < LAND_TOL_UM(30))            cls = 1

// 바닥
if (|y - floor_center_y| < floor_half &&
    |rel - floor_z_rel| < FLOOR_TOL_UM(40))          cls = 2
    // floor_half : flat 방식 = 75µm 고정
    //              corner 방식 = floor_width_um / 2

// 벽 (--keep-wall 일 때만)
if (in_notch &&
    rel < -LAND_TOL_UM &&
    rel > floor_z_rel + FLOOR_TOL_UM &&
    llt <= land_llt_p95 + WALL_LLT_MARGIN(4) &&
    이 컬럼에서 최상단 후보이면)                       cls = 3

// 그 외 = 폐기 (리플렉션, 코너 침하, 병합피크)
```

벽 판정에 llt 상한을 두는 근거: 벽 점의 llt 중앙값은 4행(p90 7~8)으로 얇고,
코너 침하부의 병합피크는 12~19행으로 두껍다. llt 가 병합피크 판별자로 유효하다.
(단, **바닥 점에 llt 필터를 적용하면 오히려 정확도가 나빠짐**을 실측 확인했으므로
바닥 분류에는 llt 조건을 넣지 않는다.)

### 5-2 컬럼 내 분열 피크 병합

`cls > 0` 인 점들에 대해 컬럼별로 z 정렬 후, **z 간격 < `DUP_MERGE_UM (10)`** 인
인접 쌍은 같은 피크의 분열로 보고 **intensity 가 큰 쪽만 남긴다.**

> 근거: 다중후보쌍의 63~65%가 z간격 5 µm 미만 = 동일 피크 분열.
> 이 단계에서 출력 점 수가 유의하게 줄고 후속 처리가 가벼워진다.

---

## 7. 파라미터 기본값 정리

```cpp
struct Params {
    // 기하
    double lateral_pitch_mm   = 0.0063;
    double transport_res_mm   = 0.00399813;   // 헤더에서 덮어씀

    // 랜드
    int    land_poly_deg      = 3;
    int    land_fit_iters     = 4;
    double land_tol_um        = 30.0;

    // 노치 개구
    double notch_trig_um      = -150.0;
    double notch_max_gap_um   = 50.0;
    int    notch_min_cols     = 20;

    // 방식 1 (flat)
    double floor_win_um       = 150.0;
    double floor_search_step_um = 6.0;
    int    floor_min_pts      = 12;

    // 방식 2 (corner)
    int    smooth_cols        = 3;      // 3~5 로 clamp (19~32 µm)
    double slope_halfwin_um   = 30.0;
    double slope_step_um      = 20.0;
    double slope_drop_ratio   = 0.35;
    double corner_search_um   = 500.0;

    // 공통 출력
    double floor_tol_um       = 40.0;
    double dup_merge_um       = 10.0;
    int    wall_llt_margin    = 4;

    // 원주 안정화
    int    xsmooth_profiles   = 51;
    double center_reject_um   = 50.0;
    double floor_reject_um    = 60.0;
    int    edge_guard_profiles = 200;
};
```

JSON 설정파일로 오버라이드 가능하게 한다 (nlohmann/json 등).

---

## 8. 검증 (구현 후 반드시 확인)

기존 스캔 데이터로 아래 값이 재현되는지 확인한다. 벗어나면 구현 오류 의심.

| 검증 항목 | 기대값 |
|---|---|
| 입력 점 수 (exp60/th40) | 14,104,733 / 36,001 프로파일 |
| 방식 1 깊이 (exp60/th40) | 409 ± 2 µm, σ 15 ± 2 µm |
| 방식 1 깊이 (exp120/th10) | 409 ± 2 µm, σ 12 ± 2 µm |
| 방식 2 깊이 (smooth_cols=3) | 408~417 µm, σ 18 ± 3 µm |
| 폐기 점 비율 (--keep-wall) | 약 46 % |
| 랜드 좌우 높이차 | −57 ± 4 µm |
| 방식 2 개구폭 (smooth_cols=3) | 360~450 µm |

추가 회귀 테스트:
- `smooth_cols` 를 15 로 올리면 개구폭이 600 µm 이상으로 부풀어야 한다
  (스무딩 부작용이 재현되는지 = 로직이 의도대로 동작하는지 확인)
- 원주방향 median 창을 1로 하면 σ 가 눈에 띄게 커져야 한다

---

## 9. 파일 구성 제안

```
src/
  main.cpp              CLI 파싱, 전체 흐름
  ply_io.h/.cpp         PLY 읽기/쓰기, 헤더 JSON 파싱
  profile.h/.cpp        Pass 1 : 랜드 피팅, 노치 검출
  floor_flat.h/.cpp     방식 1
  floor_corner.h/.cpp   방식 2
  stabilize.h/.cpp      Pass 1.5 원주 안정화
  classify.h/.cpp       Pass 2 라벨링 + 분열피크 병합
  polyfit.h             최소자승 다항 피팅 (Eigen 또는 직접 구현)
  params.h/.cpp         파라미터 구조체 + JSON 로딩
  csv_out.h/.cpp        프로파일별 결과 CSV
tests/
  test_polyfit.cpp
  test_corner_synthetic.cpp   합성 노치 프로파일로 코너 검출 정확도 검증
  test_regression.cpp         위 8절 기대값 확인
```

의존성: Eigen (헤더온리), nlohmann/json (헤더온리), OpenMP.
외부 포인트클라우드 라이브러리(PCL 등)는 필요 없다 — PLY 직접 읽고 쓰는 게 훨씬 빠르다.

---

## 10. 구현 시 주의점

1. **PLY 정점 레이아웃은 반드시 헤더에서 검증**하고, 예상과 다르면 즉시 에러.
   `sizeof(Point) == 14` 이므로 `#pragma pack(1)` 또는 명시적 오프셋 읽기 필요
   (기본 정렬로 16바이트가 되면 파일과 안 맞는다). ★ 가장 흔한 버그 지점.
2. `x` 는 float 이고 값이 최대 144 mm 이므로, 프로파일 인덱스 계산 시
   float 정밀도로 인해 인접 프로파일이 섞일 수 있다. `double` 로 승격 후 `lround`.
3. 메모리: 1860만 점 × 14 B ≈ 260 MB. sort 시 인덱스 배열만 정렬하고
   본체는 한 번만 재배치하여 피크 메모리를 줄인다.
4. 병렬화 시 `ProfileResult` 배열에만 쓰기 → race 없음. 라벨 배열도
   프로파일별 구간이 겹치지 않으므로 안전.
5. 좌표계 부호: `rel` 은 **아래가 음수**로 일관되게 유지한다. 부호 혼동이
   코너 탐색 로직을 조용히 망가뜨린다.
6. 방식 2의 폴백 발생률을 로그로 반드시 출력할 것. 폴백률이 높으면
   실질적으로 방식 1과 같은 결과가 나오므로, 비교 실험의 해석이 달라진다.
