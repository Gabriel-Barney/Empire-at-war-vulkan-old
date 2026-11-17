// trn1 t01 / t02: terrain cover and the SurfaceFX terrain modifiers (docs/re/TRN1_TERRAIN_GAMEPLAY.md §1 - §2).
//
// SurfaceFXBehavior (vtable 0x8b3428) update vf6 0x6450b0 -> per instance 0x5efe50 -> modifier step 0x5ee820:
// - the first update only creates the instances (one per SurfaceFX_Name entry); evaluation starts at the next one;
// - surface = water type when the terrain lies more than 0.5 below the water plane, else the cell material's type;
// - on a change from S to S', the S entries are taken off (every list, by the instance's source key) and S' entries
//   put on (good / high ground only when the unit is not converted to the enemy, obj +0x3a2 bit 4);
// - damage ground / lava: entering sets the object's timer (amount, next = now + int(fps x delay), delay); every later
//   update on the same surface deals the amount once next <= now (next += int(fps x delay)); leaving zeroes the
//   timer unless the new surface is one of 4, 6, 8, 12, 14, 15 (mask 0xf150).
// [port] choices (docs/PHASE4.md "trn1"): the service runs at the start of CombatSystem::tick, serially in unit order,
// for land battles only (space: no terrain, surface 0 - the update would only take entries off); instances whose
// SurfaceFX has no sim term are not kept (they would only repeat the timer zeroing at the same frame).
#include "alamo/tactical/combat/surface_fx.hpp"

#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>

#include "alamo/core/ascii.hpp"
#include "alamo/core/hash.hpp"
#include "alamo/data/xml.hpp"
#include "combat_system_internal.hpp"

namespace alamo::tactical::combat {

namespace {

constexpr uint32_t kTimerKeepMask = 0xf150u;  // surfaces 4, 6, 8, 12, 14, 15: leaving lava / damage ground keeps the timer

float number(std::string_view s) { return s.empty() ? 0.0f : static_cast<float>(std::atof(std::string(s).c_str())); }

bool is_damage_surface(uint32_t s) { return s == kSurfaceDamageGround || s == kSurfaceLava; }
bool is_slow_surface(uint32_t s) { return s == kSurfaceSlowGround || s == kSurfaceWater; }

}  // namespace

int32_t surface_type_from_name(std::string_view name) {
    static constexpr std::pair<std::string_view, int32_t> kNames[] = {
        {"DEFAULT", 0},  {"GRASS", 1},        {"DIRT", 2},          {"SNOW", 3},           {"WATER", 4},
        {"CONCRETE", 5}, {"SPACE", 18},       {"MUD", 6},           {"ICE", 7},            {"LAVA", 8},
        {"GOOD_GROUND", 12}, {"HIGH_GROUND", 13}, {"SLOW_GROUND", 14}, {"DAMAGE_GROUND", 15}, {"INFANTRY_GROUND", 16},
    };
    name = ascii_trim(name);
    for (const auto& [n, id] : kNames)
        if (ascii_iequals(n, name)) return id;
    return -1;
}

bool SurfaceFxDef::affects_sim() const {
    for (const uint32_t s : {kSurfaceWater, kSurfaceLava, kSurfaceGoodGround, kSurfaceHighGround, kSurfaceSlowGround,
                             kSurfaceDamageGround}) {
        const SurfaceSettings& v = by_surface[s];
        if (v.defense != 0.0f || v.damage != 0.0f || v.reveal_range != 0.0f || v.fire_range != 0.0f || v.speed != 0.0f ||
            v.terrain_damage > 0.0f)
            return true;
    }
    return false;
}

std::vector<SurfaceFxDef> load_surface_fx(const data::XmlNode& root) {
    std::vector<SurfaceFxDef> out;
    for (data::XmlNode fx : root.children("SurfaceFX")) {
        SurfaceFxDef d;
        d.name = std::string(ascii_trim(fx.attribute("name")));
        if (d.name.empty()) continue;
        d.key_crc = name_crc("Persistent_SurfaceFX_Unique_ID_" + ascii_upper(d.name));
        for (data::XmlNode ss : fx.children("Surface_Settings")) {
            const int32_t s = surface_type_from_name(ss.attribute("name"));
            if (s < 0) continue;
            SurfaceSettings& v = d.by_surface[static_cast<size_t>(s)];
            v.defense = number(ss.child_text("Defense_Mod"));
            v.damage = number(ss.child_text("Damage_Mod"));
            v.reveal_range = number(ss.child_text("FOW_Reveal_Range_Mod"));
            v.fire_range = number(ss.child_text("Fire_Range_Mod"));
            v.speed = number(ss.child_text("Speed_Mod"));
            v.terrain_damage = number(ss.child_text("Terrain_Damage"));
            v.terrain_damage_delay = number(ss.child_text("Terrain_Damage_Delay"));
        }
        const int32_t at = find_surface_fx(out, d.name);
        if (at >= 0) out[static_cast<size_t>(at)] = std::move(d);
        else out.push_back(std::move(d));
    }
    return out;
}

int32_t find_surface_fx(const std::vector<SurfaceFxDef>& defs, std::string_view name) {
    name = ascii_trim(name);
    for (size_t i = 0; i < defs.size(); ++i)
        if (ascii_iequals(defs[i].name, name)) return static_cast<int32_t>(i);
    return -1;
}

// ---- CombatSystem: cover (t01) ----

float CombatSystem::cover_factor(const Unit& victim) const {
    // Get_Damage_Multiplier (0x38e0c0), last term: the victim type's Surface_Type_Cover_Damage_Shield when it is not
    // 1.0, the battle is on land and the cell under the victim's position has surface type 9, 10 or 11.
    const float c = desc_of(victim).cover_damage_shield;
    if (c == 1.0f || cfg_.mode != GameMode::Land || !surface_.cell_surface) return 1.0f;
    const uint32_t s = surface_.cell_surface(victim.pos.x, victim.pos.y);
    return s >= kSurfaceCoverFirst && s <= kSurfaceCoverLast ? c : 1.0f;
}

uint32_t CombatSystem::surface_type(ObjectId id) const {
    const Unit* u = unit(id);
    if (!live(u) || cfg_.mode != GameMode::Land || !surface_.surface) return 0;
    return surface_.surface(u->pos.x, u->pos.y);
}

// ---- CombatSystem: Destroy_When_Stunned_Over_Water (t04) ----

void CombatSystem::stun_water_kill(ObjectId id, uint32_t frame) {
    // ABILITIES_HARDPOINTS §4.12 RE reading [H]: land only; the type flag; a water plane with the terrain at the object's
    // X, Y more than 0.5 below it -> Take_Damage(kind 1, 999999, no source): its gates (invulnerable, Immune_To_Damage,
    // garrisoned) apply and the death follows the normal path.
    const Unit* v = unit(id);
    if (!live(v) || cfg_.mode != GameMode::Land || !surface_.under_water || !desc_of(*v).destroy_when_stunned_over_water) return;
    if (!surface_.under_water(v->pos.x, v->pos.y)) return;
    DamageRequest r;
    r.victim = id;
    r.amount = 999999.0f;
    r.kind = kKindNormal;
    (void)take_damage(r, frame);
}

// ---- CombatSystem: SurfaceFX terrain modifiers (t02) ----

void CombatSystem::service_surface_fx(uint32_t frame) {
    if (cfg_.mode != GameMode::Land || !surface_.surface || data_->surface_fx.empty()) return;
    for (Unit& u : units_) {
        if (u.flags & kUnitDead) continue;
        const UnitDesc& d = desc_of(u);
        if (d.surface_fx.empty()) continue;
        if (!u.sfx_ready) {  // vf6 first call: the instances are made, nothing is evaluated
            u.sfx_ready = 1;
            continue;
        }
        const uint32_t s = surface_.surface(u.pos.x, u.pos.y);
        const auto n = static_cast<uint32_t>(d.surface_fx.size() < kMaxUnitSurfaceFx ? d.surface_fx.size() : kMaxUnitSurfaceFx);
        for (uint32_t i = 0; i < n; ++i) surface_fx_step(u, d.surface_fx[i], i, s, frame);
    }
}

void CombatSystem::surface_fx_step(Unit& u, uint16_t def_index, uint32_t slot, uint32_t s, uint32_t frame) {
    if (def_index >= data_->surface_fx.size() || s >= kSurfaceTypeCount) return;
    const SurfaceFxDef& def = data_->surface_fx[def_index];
    const auto prev = static_cast<uint32_t>(u.sfx_prev[slot]);
    const bool keep_timer = s < 16 && ((kTimerKeepMask >> s) & 1u) != 0;
    if (prev == s) {
        if (is_damage_surface(s) && u.terrain_damage > 0.0f && u.terrain_next <= frame) {
            u.terrain_next = static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(cfg_.fps) * u.terrain_delay)) +
                             u.terrain_next;
            DamageRequest r;
            r.victim = u.id;
            r.amount = u.terrain_damage;
            r.kind = kKindTerrain;
            (void)take_damage(r, frame);  // last use of u: take_damage may grow units_ (death clones)
        }
        return;
    }
    // Leave prev, enter s; each surface's own block, in the retail order 12, 13, 14 / 4, 15 / 8.
    const auto take_off = [&](std::initializer_list<ModList> lists) {
        for (const ModList l : lists) (void)u.mods.remove_source_in_list(l, u.id, def.key_crc);
    };
    const auto put_on = [&](ModList l, float v) {
        if (v != 0.0f) u.mods.insert(l, 0, v, kPermanent, def.key_crc, u.id);
    };
    const SurfaceSettings& in = def.by_surface[s];
    const bool converted = (u.flags & kUnitConverted) != 0;
    if (prev == kSurfaceGoodGround) take_off({ModList::Defense, ModList::Damage});
    else if (s == kSurfaceGoodGround && !converted) {
        put_on(ModList::Defense, in.defense);
        put_on(ModList::Damage, in.damage);
    }
    if (prev == kSurfaceHighGround) take_off({ModList::RevealRange, ModList::FireRange});
    else if (s == kSurfaceHighGround && !converted) {
        put_on(ModList::RevealRange, in.reveal_range);
        put_on(ModList::FireRange, in.fire_range);
    }
    if (is_slow_surface(prev)) take_off({ModList::MaxSpeed});
    else if (is_slow_surface(s)) put_on(ModList::MaxSpeed, in.speed);
    if (is_damage_surface(prev)) {
        if (!keep_timer) {
            u.terrain_damage = 0.0f;
            u.terrain_delay = 0.0f;
        }
    } else if (is_damage_surface(s) && in.terrain_damage > 0.0f) {
        u.terrain_damage = in.terrain_damage;
        u.terrain_next = static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(cfg_.fps) * in.terrain_damage_delay)) + frame;
        u.terrain_delay = in.terrain_damage_delay;
    }
    u.sfx_prev[slot] = static_cast<uint8_t>(s);
}

}  // namespace alamo::tactical::combat
