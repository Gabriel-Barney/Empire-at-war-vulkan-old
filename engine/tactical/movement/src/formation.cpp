#include "alamo/tactical/movement/formation.hpp"

#include <algorithm>
#include <cmath>

namespace alamo::tactical::movement {

namespace {

int icompare(std::string_view a, std::string_view b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 32);
        if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

float member_width(const FormationMember& f) { return f.spacing + 2.0f * f.radius / kCellSize; }

}  // namespace

std::vector<std::vector<uint32_t>> kmeans_buckets(std::span<const FormationMember> m, float max_formation_area,
                                                  uint32_t max_iterations) {
    const uint32_t n = static_cast<uint32_t>(m.size());
    std::vector<std::vector<uint32_t>> out;
    if (n == 0) return out;
    float area = 0.0f;
    for (const FormationMember& f : m) area += 4.0f * f.radius * f.radius;
    const uint32_t k = static_cast<uint32_t>(std::floor(area / 400.0f / std::max(max_formation_area, 1e-6f))) + 1u;
    if (k == 1 || k > n) {
        out.emplace_back();
        for (uint32_t i = 0; i < n; ++i) out[0].push_back(i);
        return out;
    }
    std::vector<uint32_t> assign(n);
    for (uint32_t i = 0; i < n; ++i) assign[i] = i % k;
    std::vector<V2> cent(k);
    std::vector<uint32_t> count(k);
    for (uint32_t it = 0; it < max_iterations; ++it) {
        std::fill(cent.begin(), cent.end(), V2{});
        std::fill(count.begin(), count.end(), 0u);
        for (uint32_t i = 0; i < n; ++i) {
            cent[assign[i]] = cent[assign[i]] + m[i].pos;
            ++count[assign[i]];
        }
        for (uint32_t b = 0; b < k; ++b)
            if (count[b]) cent[b] = cent[b] * (1.0f / static_cast<float>(count[b]));
        bool moved = false;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t best = assign[i];
            float bd = 3.0e38f;
            for (uint32_t b = 0; b < k; ++b) {
                if (!count[b]) continue;
                const float d = length_sq(m[i].pos - cent[b]);
                if (d < bd) {
                    bd = d;
                    best = b;
                }
            }
            if (best != assign[i]) {
                assign[i] = best;
                moved = true;
            }
        }
        if (!moved) break;
    }
    out.resize(k);
    for (uint32_t i = 0; i < n; ++i) out[assign[i]].push_back(i);
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::vector<uint32_t>& b) { return b.empty(); }), out.end());
    return out;
}

std::vector<std::vector<uint32_t>> split_into_formations(std::span<const FormationMember> m, std::span<const uint32_t> bucket,
                                                         const MovementConstants& k, const CompatibleFn& compatible,
                                                         float double_click_speed_ratio, const ReachFn& reach) {
    std::vector<std::vector<uint32_t>> out;
    if (bucket.empty()) return out;
    V2 c{};
    for (uint32_t i : bucket) c = c + m[i].pos;
    c = c * (1.0f / static_cast<float>(bucket.size()));
    std::vector<uint32_t> remaining(bucket.begin(), bucket.end());
    while (!remaining.empty()) {
        size_t si = 0;
        float bd = 3.0e38f;
        for (size_t i = 0; i < remaining.size(); ++i) {
            const float d = length_sq(m[remaining[i]].pos - c);
            if (d < bd) {
                bd = d;
                si = i;
            }
        }
        const uint32_t seed = remaining[si];
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(si));
        std::vector<uint32_t> f{seed};
        const FormationMember& s = m[seed];
        // s78n t25.2: the attack solo rule (MOVEMENT §4.2 item 6). bkm1 t04 (§4.2 step 2 item 7 test 3 [H]): a short-range
        // seed recruits only units of its own exact type (corrects the s78n t25 fix, which recruited nobody)
        if (s.grouping != kGroupingSolo && !s.solo_attack) {
            // mov2 t03 (§4.2 step 2 item 7 "Seed side" / tests 1, 7 [H]): the joined-teams list starts with the seed's team
            std::vector<ObjectId> joined;
            if (s.team >= 0) joined.push_back(s.team);
            const bool seed_team = s.team_member || s.team >= 0;
            bool grew = true;
            while (grew) {
                grew = false;
                for (size_t i = 0; i < remaining.size();) {
                    const uint32_t cand = remaining[i];
                    const FormationMember& o = m[cand];
                    const auto recruit = [&] {
                        f.push_back(cand);
                        if (o.team >= 0 && std::find(joined.begin(), joined.end(), o.team) == joined.end())
                            joined.push_back(o.team);
                        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
                        grew = true;
                        i = 0;  // bkm1 t04 (§4.2 step 2 item 7 "Scan" [H]): after every recruit the scan restarts
                    };
                    // mov2 t03 (test 1 [H]): a candidate on a joined team joins at once, no further test
                    if (o.team >= 0 && std::find(joined.begin(), joined.end(), o.team) != joined.end()) {
                        recruit();
                        continue;
                    }
                    // mcx4 t11 (BKR2_RE_ANSWERS Q8 [H]): test 3's team clause - a seed with a parent team (a squad member)
                    // recruits nobody while ShouldInfantryTeamsSplitAcrossFormations is set (FoC: true)
                    bool ok = (!seed_team || !k.should_infantry_teams_split_across_formations) &&
                              (!s.short_range_attack || o.type_name == s.type_name);
                    if (!ok) {
                        ++i;
                        continue;
                    }
                    if (s.grouping == kGroupingStandard) ok = o.grouping == kGroupingStandard;
                    else if (s.grouping == kGroupingSameOrder) ok = o.grouping == kGroupingSameOrder && o.order == s.order;
                    else ok = false;  // an unknown grouping (a mod's typo) recruits nobody
                    if (ok && compatible) ok = compatible(seed, cand);
                    // mov2 t03 (test 7 [H]): a candidate with a team needs a seed with one
                    if (ok && (o.team_member || o.team >= 0)) ok = seed_team;
                    if (ok && double_click_speed_ratio > 0.0f && o.max_speed != 0.0f) {  // 0x80 speed band (0x43c1b0)
                        const float hi = std::max(o.max_speed, s.max_speed), lo = std::min(o.max_speed, s.max_speed);
                        ok = lo > 0.0f && !(hi / lo > double_click_speed_ratio);
                    }
                    // bkm1 t04 (b0036, §4.2 step 2 item 7 test 9 [H]): the engagement compatibility, both ways, no tolerance
                    if (ok) ok = !(o.min_engage > s.engage) && !(s.min_engage > o.engage);
                    // test 10 [H]: on an attack order a candidate that is solo by item 6 (melee, in max range, or within
                    // r + E + Solo_Attack_Range of the target) is skipped - it seeds its own formation
                    if (ok) ok = !o.solo_attack;
                    if (ok) {
                        const float rd = (o.collision_class == 5 && !o.team_member) ? k.infantry_formation_recruitment_distance
                                                                                      : k.vehicle_formation_recruitment_distance;
                        // mov2 t03 (test 11 [H]): d_min = the 3-D distance to the nearest current member (the first at
                        // the least distance)
                        float nearest = 3.0e38f;
                        for (uint32_t mm : f) {
                            const float dz = m[mm].z - o.z;
                            const float d = length_sq(m[mm].pos - o.pos) + dz * dz;
                            if (d < nearest) nearest = d;
                        }
                        ok = nearest <= rd * rd;
                        // test 12 [H]: the line candidate -> seed, or a path <= 1.4 x d_min
                        if (ok && reach) ok = reach(seed, cand, std::sqrt(nearest));
                    }
                    if (ok) recruit();
                    else ++i;
                }
            }
        }
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<RankedMember> rank_members(std::span<const FormationMember> m, std::span<const uint32_t> members, RankKey key) {
    std::vector<uint32_t> heroes, others;
    for (uint32_t i : members) (m[i].hero ? heroes : others).push_back(i);
    // s78n t26 review round 2 (MOVEMENT §4.3 correction [H]): the key of 0x41a080 is 0x3749d0, the attack range
    const auto kv = [&](uint32_t i) { return key == RankKey::MaxSpeed ? m[i].max_speed : m[i].attack_range; };
    auto cmp = [&](uint32_t a, uint32_t b) {
        if (kv(a) != kv(b)) return kv(a) < kv(b);
        const int c = icompare(m[a].type_name, m[b].type_name);
        if (c != 0) return c < 0;
        return m[a].id < m[b].id;
    };
    std::stable_sort(heroes.begin(), heroes.end(), cmp);
    std::stable_sort(others.begin(), others.end(), cmp);
    std::vector<uint32_t> rank(others.size(), 0);
    float tol = 0.0f;
    for (int step = 0; step < 4; ++step) {
        uint32_t r = 0;
        float start = others.empty() ? 0.0f : kv(others[0]);
        for (size_t i = 0; i < others.size(); ++i) {
            if (kv(others[i]) - start > tol) {
                ++r;
                start = kv(others[i]);
            }
            rank[i] = r;
        }
        if (others.empty() || r + 1 < 4) break;
        tol += 15.0f;
    }
    std::vector<RankedMember> out;
    for (uint32_t h : heroes) out.push_back({h, 0});
    const uint32_t base = heroes.empty() ? 0u : 1u;
    for (size_t i = 0; i < others.size(); ++i) out.push_back({others[i], rank[i] + base});
    return out;
}

float formation_half_width(std::span<const FormationMember> m, std::span<const uint32_t> members, const MovementConstants& k) {
    float cell = 0.0f, area = 0.0f;
    for (uint32_t i : members) {
        cell = std::max(cell, member_width(m[i]));
        const float d = 2.0f * m[i].radius / kCellSize;
        area += d * d;
    }
    const float side = std::ceil(std::sqrt(static_cast<float>(members.size())));
    return std::max(side * cell + 0.25f, std::sqrt(std::max(1.0f, area * k.spread_out_spacing_coefficient)));
}

std::vector<Slot> layout_formation(std::span<const FormationMember> m, std::span<const uint32_t> members, V2 forward,
                                   const MovementConstants& k, RankKey key, float half_width) {
    std::vector<Slot> out;
    if (members.empty()) return out;
    const V2 right = formation_right(forward);
    const float hw = half_width > 0.0f ? half_width : formation_half_width(m, members, k);

    std::vector<RankedMember> remaining = rank_members(m, members, key);
    float depth = 0.0f;
    uint32_t row = 0;
    while (!remaining.empty()) {
        const uint32_t r = remaining.front().rank;
        std::vector<RankedMember> cand, rest;
        for (const RankedMember& rm : remaining) (rm.rank == r ? cand : rest).push_back(rm);
        std::stable_sort(cand.begin(), cand.end(), [&](const RankedMember& a, const RankedMember& b) {
            const float pa = dot(m[a.member].pos, forward), pb = dot(m[b.member].pos, forward);
            if (pa != pb) return pa > pb;
            return m[a.member].id < m[b.member].id;
        });
        std::vector<RankedMember> kept, overflow;
        float sum = 0.0f;
        for (const RankedMember& rm : cand) {
            const float w = member_width(m[rm.member]);
            if (kept.empty() || sum + w <= 2.0f * hw) {
                kept.push_back(rm);
                sum += w;
            } else {
                overflow.push_back(rm);
            }
        }
        remaining = overflow;
        remaining.insert(remaining.end(), rest.begin(), rest.end());
        std::stable_sort(kept.begin(), kept.end(), [&](const RankedMember& a, const RankedMember& b) {
            const float la = dot(m[a.member].pos, right), lb = dot(m[b.member].pos, right);
            if (la != lb) return la < lb;
            return m[a.member].id < m[b.member].id;
        });
        const float gap = (2.0f * hw - sum) / static_cast<float>(kept.size() + 1);
        float acc = 0.0f, maxw = 0.0f;
        for (const RankedMember& rm : kept) {
            const float w = member_width(m[rm.member]);
            Slot s;
            s.member = rm.member;
            s.lateral = (acc + gap + 0.5f * w - hw) * kCellSize;
            s.back = depth;
            s.row = row;
            out.push_back(s);
            acc += gap + w;
            maxw = std::max(maxw, w);
        }
        depth += maxw * kCellSize;
        ++row;
    }
    // Centre the block on the destination.
    const float last_row_back = out.back().back;
    for (Slot& s : out) s.back -= 0.5f * last_row_back;
    return out;
}

V2 destination_try_offset(uint32_t k, bool first_unblocked, V2 v, float m) {
    const uint32_t r = k % 8u, round = k / 8u;
    const float sign = !first_unblocked ? 1.0f : ((k & 1u) == 0u ? 0.5f : -0.375f);  // halved: x 0.5 x (+1 / -0.75)
    if (r < 4u) return v * (7.0f * static_cast<float>(4u * round + r) * sign);
    static constexpr V2 kAxes[4] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}};
    return v * (28.0f * static_cast<float>(round) * sign) + kAxes[r - 4u] * m;
}

FinalFacing final_formation_facing(std::span<const V2> path, float facing_deg, float half_width, float speed,
                                   const MovementConstants& k) {
    FinalFacing f;
    const size_t n = path.size();
    if (n < 2 || facing_deg == kNoFacing) return f;
    // 0x20b100: the facing (0x20b6d0-wrapped) minus the last segment's yaw, into [0, 360); then 0x20b6d0 into (-180, 180]
    float d = wrap_deg(facing_deg) - heading_deg(path[n - 1] - path[n - 2]);
    if (d >= 360.0f) d -= 360.0f;
    else if (d < 0.0f) d += 360.0f;
    const float psi = wrap_deg(d);
    if (!(std::fabs(psi) >= k.final_formation_facing_minimum_angle)) return f;
    const float full = psi * 0.017453292f;
    f.full = full;
    float r = half_width * 0.5f * 20.0f;
    r = std::sqrt(r * 2.0f * r);
    float len = 0.0f;
    for (size_t i = 1; i < n; ++i) len += distance(path[i - 1], path[i]);
    if (len != 0.0f && speed > 0.0f) {
        const float budget = ((k.final_formation_facing_delta_coefficient * speed) / r) * (len / speed);
        const float a = std::fabs(full);
        if (budget < a) f.start = full <= 0.0f ? budget - a : a - budget;
        f.rate = ((f.full - f.start) / len) * 1.2f;
    }
    return f;
}

int32_t snap_order_center(V2 p, float pz, std::span<const SnapCenter> objects) {
    int32_t best = -1;
    float best_d = -1.0f;
    for (size_t i = 0; i < objects.size(); ++i) {
        const SnapCenter& o = objects[i];
        const float dx = p.x - o.pos.x, dy = p.y - o.pos.y, dz = pz - o.z;
        const float d = std::sqrt(dy * dy + dx * dx + dz * dz);
        if (d <= o.radius && (best_d == -1.0f || d < best_d)) {
            best = static_cast<int32_t>(i);
            best_d = d;
        }
    }
    return best;
}

}  // namespace alamo::tactical::movement
