// tru1 t02: base shields (docs/re/TRU1_A_RE_ANSWERS.md A1 - A5).
//
// A BASE_SHIELD generator turns its shield on and off every 15 frames (Evaluate_State 0x56bb00 / Set_State 0x56b540).
// An AFFECTED_BY_SHIELD unit that straddles the wall of an active hostile shield is "in the shield" (service 0x502f70,
// test 0x5031c0): it takes Base_Shield_Vulnerability_Modifier x damage (Take_Damage step 6), moves at
// Base_Shield_Speed_Modifier x speed (host) and does not target or fire. Hostile projectiles that cross an active
// dome stop there and hit the generator with the dome mesh's name, which Take_Damage step 15a absorbs in full.
//
// [port] choices, each listed in docs/PHASE4.md:
// - the dome is the ellipsoid of horizontal semi-axis R and vertical semi-axis kDomeHeight x R around the generator
//   (A5 measured NB_BASESHIELDGEN.ALO), tested from both sides ([L] in the RE);
// - the first evaluation of a new generator runs at its first tick (state 0, no on-delay), not inside add_unit, so a
//   generator and its power source spawned in either order start the battle in the same state;
// - the unit bound radius is the model's horizontal half extents (else Custom_Hard extents, else the radius);
// - not modelled: the story disable bit (DISABLE_SPECIAL_STRUCTURE has no port reader), the penetration particle (a
//   render particle at base_shield_contact, vis1 t08),
//   the idle nudge (a move order: host), the SHIELD_ON / OFF animation and its variant draw, the HUD / loop SFX.
#include "combat_system_internal.hpp"

namespace alamo::tactical::combat {

namespace {

constexpr uint32_t kBaseShieldInterval = 15;  // vf5 0x56ac80: FPS / 2
constexpr float kDomeHeight = 0.3f;           // A5: top of the dome about 0.3 R above the generator [M]

}  // namespace

void CombatSystem::add_base_shield_upgrade(int32_t player, ObjectId source) {
    for (const auto& [pl, src] : shield_upgrade_)
        if (pl == player && src == source) return;
    shield_upgrade_.emplace_back(player, source);
}

void CombatSystem::remove_base_shield_upgrade(int32_t player, ObjectId source) {
    shield_upgrade_.erase(std::remove_if(shield_upgrade_.begin(), shield_upgrade_.end(),
                                         [&](const std::pair<int32_t, ObjectId>& e) {
                                             return e.first == player && e.second == source;
                                         }),
                          shield_upgrade_.end());
}

bool CombatSystem::base_shield_upgraded(int32_t player) const {
    for (const auto& e : shield_upgrade_)
        if (e.first == player) return true;
    return false;
}

bool CombatSystem::base_shield_active(ObjectId id) const {
    const Unit* s = unit(id);
    return live(s) && desc_of(*s).base_shield && s->bshield_state == 1;
}

bool CombatSystem::base_shield_active_upgraded(ObjectId id) const {
    const Unit* s = unit(id);
    return base_shield_active(id) && s->bshield_upgraded != 0;
}

ObjectId CombatSystem::base_shield_straddled(ObjectId id) const {
    const Unit* u = unit(id);
    return u ? u->bshield_in : kNoObject;
}

float CombatSystem::base_shield_speed_factor(ObjectId id) const {
    return base_shield_straddled(id) != kNoObject ? cfg_.base_shield_speed : 1.0f;
}

void CombatSystem::evaluate_base_shield(Unit& s, uint32_t frame) {
    // Evaluate_State 0x56bb00 [H]: powered (base power when required), not dying, owner faction not neutral, not
    // Always_Off. [port] the story disable bit is not modelled.
    const UnitDesc& sd = desc_of(s);
    bool on = !sd.base_shield_requires_power || has_base_power(s.owner);
    if ((s.flags & kUnitDead) || !(s.health > 0.0f)) on = false;
    if (!player(s.owner).active || sd.base_shield_always_off) on = false;  // PlayerInfo::active = faction not Is_Neutral
    const bool upgraded = base_shield_upgraded(s.owner);
    if (!on) s.bshield_off_stamp = frame;  // every service that finds it off
    // Set_State 0x56b540 [H]: nothing changes -> nothing; off -> on waits FPS x Base_Shield_Delay_Time after the last off
    // service (strict float compare); from state 0 there is no wait.
    const uint8_t next = on ? 1 : 2;
    const uint8_t up = upgraded ? 1 : 0;
    if (next == s.bshield_state && up == s.bshield_upgraded) return;
    if (next == 1 && s.bshield_state == 2 &&
        static_cast<float>(frame - s.bshield_off_stamp) < static_cast<float>(cfg_.fps) * cfg_.base_shield_delay_time)
        return;
    s.bshield_state = next;
    s.bshield_upgraded = up;
    bshield_sets_.push_back({s.id, on});  // r3h t06: SHIELD_ON / SHIELD_OFF for the host (item 6)
}

bool CombatSystem::dome_segment(const Unit& s, Vec3 a, Vec3 b, float& t) const {
    // [port] the dome mesh as an ellipsoid (R, R, kDomeHeight R) centred on the generator: scale z to a sphere of
    // radius R and take the first crossing of the surface on [0, 1], from either side.
    const float R = desc_of(s).base_shield_radius;
    if (!(R > 0.0f)) return false;
    const float k = 1.0f / kDomeHeight;
    const Vec3 o{a.x - s.pos.x, a.y - s.pos.y, (a.z - s.pos.z) * k};
    const Vec3 d{b.x - a.x, b.y - a.y, (b.z - a.z) * k};
    const float qa = dot(d, d);
    if (!(qa > 0.0f)) return false;
    const float qb = 2.0f * dot(o, d);
    const float qc = dot(o, o) - R * R;
    const float disc = qb * qb - 4.0f * qa * qc;
    if (disc < 0.0f) return false;
    const float sq = dmath::sqrt(disc);
    const float t0 = (-qb - sq) / (2.0f * qa);
    const float t1 = (-qb + sq) / (2.0f * qa);
    if (t0 >= 0.0f && t0 <= 1.0f) {
        t = t0;
        return true;
    }
    if (t1 >= 0.0f && t1 <= 1.0f) {
        t = t1;
        return true;
    }
    return false;
}

void CombatSystem::straddle_segment(const Unit& u, const Unit& s, Vec3& a, Vec3& b) const {
    // Straddles_Shield step 3 [H]: along the line between the two target points, from +e to -e around the unit's,
    // e = the larger horizontal half extent.
    const UnitDesc& ud = desc_of(u);
    float hx = ud.radius, hy = ud.radius;
    if (ud.model_x_extent >= 0.0f) {
        hx = ud.model_y_extent;  // model_x_extent is |half y| (models face -Y), model_y_extent |half x|
        hy = ud.model_x_extent;
    } else if (ud.hard_x_extent > 0.0f || ud.hard_y_extent > 0.0f) {
        hx = ud.hard_x_extent;
        hy = ud.hard_y_extent;
    }
    const Vec3 tu{u.pos.x, u.pos.y, u.pos.z + ud.ranged_target_z_adjust};
    const Vec3 ts{s.pos.x, s.pos.y, s.pos.z + desc_of(s).ranged_target_z_adjust};
    const Vec3 dir = dot(ts - tu, ts - tu) > 0.0f ? normalize(ts - tu) : Vec3{1.0f, 0.0f, 0.0f};
    const float e = hx > hy ? hx : hy;
    a = tu + dir * e;
    b = tu - dir * e;
}

bool CombatSystem::base_shield_contact(ObjectId id, Vec3& point, Vec3& normal) const {
    const Unit* u = unit(id);
    if (!u || u->bshield_in == kNoObject) return false;
    const Unit* s = unit(u->bshield_in);
    if (!s) return false;
    Vec3 a{}, b{};
    straddle_segment(*u, *s, a, b);
    float t = 0.0f;
    if (!dome_segment(*s, a, b, t)) return false;
    point = a + (b - a) * t;
    // gradient of the ellipsoid x^2 + y^2 + (z / kDomeHeight)^2 = R^2 at the point, normalised
    const Vec3 g{point.x - s->pos.x, point.y - s->pos.y, (point.z - s->pos.z) / (kDomeHeight * kDomeHeight)};
    normal = dot(g, g) > 0.0f ? normalize(g) : Vec3{0.0f, 0.0f, 1.0f};
    return true;
}

bool CombatSystem::straddles_base_shield(const Unit& u, const Unit& s) const {
    // Straddles_Shield 0x5031c0 [H]: R - r <= d <= R + r (3-D), then the dome ray test 0x502770 along the line between
    // the two target points, from +e to -e around the unit's.
    const UnitDesc& ud = desc_of(u);
    float hx = ud.radius, hy = ud.radius;
    if (ud.model_x_extent >= 0.0f) {
        hx = ud.model_y_extent;  // model_x_extent is |half y| (models face -Y), model_y_extent |half x|
        hy = ud.model_x_extent;
    } else if (ud.hard_x_extent > 0.0f || ud.hard_y_extent > 0.0f) {
        hx = ud.hard_x_extent;
        hy = ud.hard_y_extent;
    }
    const float r = dmath::sqrt(hx * hx + hy * hy);
    const float d = distance3(u.pos, s.pos);
    const float R = desc_of(s).base_shield_radius;
    if (R < d - r || d + r < R) return false;
    Vec3 a{}, b{};
    straddle_segment(u, s, a, b);  // the same segment base_shield_contact reads
    float t = 0.0f;
    return dome_segment(s, a, b, t);
}

void CombatSystem::service_base_shields(uint32_t frame) {
    // The shield list (0x140a2bdf8): live BASE_SHIELD objects without Always_Off, in registration (unit) order.
    bshields_.clear();
    bshield_sets_.clear();
    for (uint32_t i = 0; i < units_.size(); ++i) {
        Unit& s = units_[i];
        const UnitDesc& sd = desc_of(s);
        if (!sd.base_shield) continue;
        if (s.flags & kUnitDead) {
            s.bshield_state = 2;  // detach vf15 0x56ba00
            continue;
        }
        // BASE_SHIELD service every 15 frames from the attach phase draw; the first evaluation at once (port: here).
        if (s.bshield_state == 0) {
            evaluate_base_shield(s, frame);
        } else if (frame >= s.bshield_next) {
            s.bshield_next = frame + kBaseShieldInterval;
            evaluate_base_shield(s, frame);
        }
        if (!sd.base_shield_always_off) bshields_.push_back(i);
    }
    // AFFECTED_BY_SHIELD service 0x502f70, every frame, per unit [H]. Hostile shields only; an inactive one clears the
    // stored shield; a straddled active one is stored (enter / stay) and ends the walk; leaving the stored one clears it.
    for (Unit& u : units_) {
        if (!desc_of(u).affected_by_shield) continue;
        ObjectId stored = u.bshield_in;
        if (stored != kNoObject && !live(unit(stored))) stored = kNoObject;  // signal 0x28 / 0x502e30
        if (!(u.flags & kUnitDead)) {
            if (bshields_.empty()) stored = kNoObject;
            for (const uint32_t idx : bshields_) {
                const Unit& s = units_[idx];
                if (!is_enemy(s.owner, u.owner)) continue;
                if (s.bshield_state != 1) {
                    stored = kNoObject;
                    continue;
                }
                if (straddles_base_shield(u, s)) {
                    stored = s.id;
                    break;
                }
                if (stored == s.id) stored = kNoObject;
            }
        } else {
            stored = kNoObject;
        }
        u.bshield_in = stored;
        if (stored != kNoObject) {
            u.flags |= kUnitInBaseShield;
        } else {
            u.flags &= ~kUnitInBaseShield;
        }
    }
}

void CombatSystem::dome_sweep(const Projectile& p, const ProjectileDesc& pd, Vec3 delta, SweepHit& best) const {
    // A5 [H]: hostile active shields only; the query mask (7 with Projectile_Does_Shield_Damage, else 5) against the
    // dome's mask (2 on, 4 on and upgraded) - a shot without shield damage crosses a shield that is not upgraded.
    // Read-only (phase E workers): bshields_ was rebuilt in the serial service.
    for (const uint32_t idx : bshields_) {
        const Unit& s = units_[idx];
        if (s.bshield_state != 1 || (s.flags & kUnitDead)) continue;
        if (!is_enemy(p.owner, s.owner)) continue;
        if (!pd.does_shield_damage && !s.bshield_upgraded) continue;
        float t = 0.0f;
        if (!dome_segment(s, p.pos, p.pos + delta, t)) continue;
        if (best.id == kNoObject || t < best.t || (t == best.t && s.id < best.id)) {
            best = SweepHit{s.id, t};
            best.mesh_hp = kHardpointBaseShieldDome;
        }
    }
}

}  // namespace alamo::tactical::combat
