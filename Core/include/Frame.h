#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <cstddef>

namespace vision {

// ─────────────────────────────────────────────────────────────────────
//  Transform2D — rigid 변환 (2D 회전 + 평행이동 + Z 오프셋).
//    p_to = R(angleDeg) * p_from + (tx, ty)
//    z_to = z_from + tz
//  rigid 제한 이유: 거리 보존 → 프레임을 옮겨도 mm 측정값 유효.
//  (Open eVision이 world→ZMap 변환에 규정한 것과 같은 제약)
// ─────────────────────────────────────────────────────────────────────
struct Transform2D {
    double angleDeg = 0.0;
    double tx = 0.0, ty = 0.0, tz = 0.0;

    static Transform2D identity() { return {}; }

    bool isIdentity() const {
        return angleDeg == 0.0 && tx == 0.0 && ty == 0.0 && tz == 0.0;
    }

    void apply(double& x, double& y) const {
        if (angleDeg == 0.0) { x += tx; y += ty; return; }
        const double r = angleDeg * (3.14159265358979323846 / 180.0);
        const double c = std::cos(r), s = std::sin(r);
        const double nx = c * x - s * y + tx;
        const double ny = s * x + c * y + ty;
        x = nx; y = ny;
    }
    double applyZ(double z) const { return z + tz; }

    // this 적용 후 next 적용에 해당하는 합성 변환.
    // T1.then(T2): p → T2(T1(p)) = (R2·R1)p + (R2·t1 + t2)
    Transform2D then(const Transform2D& next) const {
        Transform2D out;
        out.angleDeg = angleDeg + next.angleDeg;
        out.tz = tz + next.tz;
        if (next.angleDeg == 0.0) {
            out.tx = tx + next.tx;
            out.ty = ty + next.ty;
        } else {
            const double r2 = next.angleDeg * (3.14159265358979323846 / 180.0);
            const double c2 = std::cos(r2), s2 = std::sin(r2);
            out.tx = c2 * tx - s2 * ty + next.tx;
            out.ty = s2 * tx + c2 * ty + next.ty;
        }
        return out;
    }

    // 역변환. T.inverse().then(T) == identity.
    // Forward: q = R(θ)·p + t  →  Inverse: p = R(-θ)·q + R(-θ)·(-t)
    Transform2D inverse() const {
        Transform2D inv;
        inv.angleDeg = -angleDeg;
        inv.tz = -tz;
        if (angleDeg == 0.0) {
            inv.tx = -tx;
            inv.ty = -ty;
        } else {
            // R(-θ)·(-tx, -ty): c=cos(θ), s=sin(θ)
            // R(-θ) = [[c, s], [-s, c]]  →  R(-θ)·(-t) = (-c·tx - s·ty, s·tx - c·ty)
            const double r = angleDeg * (3.14159265358979323846 / 180.0);
            const double c = std::cos(r), s = std::sin(r);
            inv.tx = -c * tx - s * ty;
            inv.ty =  s * tx - c * ty;
        }
        return inv;
    }
};

// ─────────────────────────────────────────────────────────────────────
//  Frame — 부모 프레임으로의 rigid 변환 + 부모 링크.
//  parentId 빈 문자열 = 루트. 루트 관례 id = "world".
// ─────────────────────────────────────────────────────────────────────
struct Frame {
    std::string id;
    std::string parentId;     // "" = root
    Transform2D toParent;     // 이 프레임 좌표 → 부모 좌표
};

// ─────────────────────────────────────────────────────────────────────
//  FrameRegistry — 실행 1회분의 프레임 트리.
//  데이터에는 라벨(frameId)만 두고 트리는 여기 한 곳에만 둔다.
// ─────────────────────────────────────────────────────────────────────
class FrameRegistry {
public:
    // 같은 id 재정의 = 덮어쓰기. 자기 조상을 부모로 지정하면 거부(사이클 방어).
    void define(const Frame& f) {
        if (!f.parentId.empty()) {
            std::unordered_set<std::string> visited;
            std::string cur = f.parentId;
            while (!cur.empty()) {
                if (cur == f.id) return; // cycle: f.id would become its own ancestor
                if (visited.count(cur)) return; // already-cyclic ancestor chain
                visited.insert(cur);
                auto it = m_frames.find(cur);
                if (it == m_frames.end()) break; // parent not yet defined, OK
                cur = it->second.parentId;
            }
        }
        m_frames[f.id] = f;
    }

    bool exists(const std::string& id) const {
        return m_frames.count(id) != 0;
    }

    const Frame* get(const std::string& id) const {
        auto it = m_frames.find(id);
        return it == m_frames.end() ? nullptr : &it->second;
    }

    // from → to 변환. 공통 조상까지 올라가 합성.
    // 실패(연결 없음/미정의/사이클) 시 false.
    bool transform(const std::string& from,
                   const std::string& to,
                   Transform2D& out) const
    {
        if (from == to) { out = Transform2D::identity(); return true; }

        std::vector<std::string> fromIds, toIds;
        std::vector<Transform2D> fromTPs, toTPs;
        if (!buildAncestorPath(from, fromIds, fromTPs)) return false;
        if (!buildAncestorPath(to,   toIds,   toTPs))   return false;

        // Find common ancestor (first id in toIds that appears in fromIds)
        std::unordered_map<std::string, std::size_t> fromIdxMap;
        for (std::size_t i = 0; i < fromIds.size(); ++i)
            fromIdxMap[fromIds[i]] = i;

        std::size_t commonFromIdx = static_cast<std::size_t>(-1);
        std::size_t commonToIdx   = static_cast<std::size_t>(-1);
        for (std::size_t j = 0; j < toIds.size(); ++j) {
            auto it = fromIdxMap.find(toIds[j]);
            if (it != fromIdxMap.end()) {
                commonFromIdx = it->second;
                commonToIdx   = j;
                break;
            }
        }
        if (commonFromIdx == static_cast<std::size_t>(-1)) return false;

        // from → common ancestor: compose fromTPs[0..commonFromIdx-1]
        Transform2D fromToCommon = Transform2D::identity();
        for (std::size_t i = 0; i < commonFromIdx; ++i)
            fromToCommon = fromToCommon.then(fromTPs[i]);

        // to → common ancestor (then invert → common → to)
        Transform2D toToCommon = Transform2D::identity();
        for (std::size_t j = 0; j < commonToIdx; ++j)
            toToCommon = toToCommon.then(toTPs[j]);

        out = fromToCommon.then(toToCommon.inverse());
        return true;
    }

    bool compatible(const std::string& a, const std::string& b) const {
        Transform2D tmp;
        return transform(a, b, tmp);
    }

    void clear() { m_frames.clear(); }

    std::vector<std::string> ids() const {
        std::vector<std::string> result;
        result.reserve(m_frames.size());
        for (const auto& kv : m_frames) result.push_back(kv.first);
        return result;
    }

private:
    std::unordered_map<std::string, Frame> m_frames;

    // ids[i] = frame at depth i from start; transforms[i] = ids[i]→ids[i+1].
    // transforms.size() == ids.size() - 1 (no transform at root).
    bool buildAncestorPath(const std::string& start,
                           std::vector<std::string>& ids,
                           std::vector<Transform2D>& transforms) const
    {
        std::unordered_set<std::string> visited;
        std::string cur = start;
        while (true) {
            if (visited.count(cur)) return false; // cycle
            visited.insert(cur);
            ids.push_back(cur);
            auto it = m_frames.find(cur);
            if (it == m_frames.end() || it->second.parentId.empty()) break; // root
            transforms.push_back(it->second.toParent);
            cur = it->second.parentId;
        }
        return true;
    }
};

// 관례 상수 — 문자열 리터럴 산포 방지
namespace frames {
    inline constexpr const char* kWorld = "world";
    inline constexpr const char* kUnset = "";   // 미지정 = 검사 생략 (Phase 1~3 호환)
}

} // namespace vision
