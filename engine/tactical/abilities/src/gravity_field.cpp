// TRU1 t03 - GRAVITY_CONTROL_FIELD (behaviour 0x3a): a powered generator slows the enemy units around it whose type sets
// Is_Affected_By_Gravity_Control_Field. Behaviour: docs/re/TRU1_GRAVITY_FIELD.md.
//
// Port shape: the generator's state lives on its ability object (gravity_due / gravity_on / gravity_list), the victim's
// flag on the victim's (in_gravity_field). The slow is one list-5 (MaxSpeed) entry in the victim's own ModifierSet whose
// source is the generator (id + type-name CRC), so the movement host's existing speed factor applies it and a release
// removes exactly that entry. No RNG here (the first-due draw is made in add_unit).
#include <algorithm>
#include <cstdint>

#include "alamo/tactical/abilities/ability_system.hpp"

namespace alamo::tactical::abilities {

using combat::ModList;

namespace {
constexpr uint32_t kGravityInterval = 60;  // the class's service interval (OBJECT_RUNTIME §9.1)
}  // namespace

bool AbilitySystem::in_gravity_field(ObjectId id) const {
    const Obj* o = obj(id);
    return o && o->in_gravity_field != 0;
}

bool AbilitySystem::gravity_field_on(ObjectId generator) const {
    const Obj* o = obj(generator);
    return o && o->gravity_on != 0;
}

std::span<const ObjectId> AbilitySystem::gravity_field_units(ObjectId generator) const {
    const Obj* o = obj(generator);
    return o ? std::span<const ObjectId>(o->gravity_list) : std::span<const ObjectId>();
}

void AbilitySystem::gravity_service(Obj& g, uint32_t frame) {
    // Every 60 frames: the owner needs a player (else nothing changes); with base power the field is applied, without it
    // every slowed unit is released. A change of the powered state raises the HUD / story event.
    if (g.dead || !desc(g).gravity_field || frame < g.gravity_due) return;
    g.gravity_due = frame + kGravityInterval;
    if (cfg_.mode != GameMode::Land || g.owner < 0) return;  // land modes only (mode type 1)
    const bool powered = combat_->has_base_power(g.owner);
    if (powered) {
        gravity_apply(g, frame);
    } else if (!g.gravity_list.empty()) {
        gravity_release_all(g, frame);
    }
    const uint8_t on = powered ? 1 : 0;
    if (g.gravity_on != on) {
        AbilityEvent e;
        e.kind = AbilityEventKind::GravityFieldPower;
        e.id = g.id;
        e.value = on;
        e.frame = frame;
        events_.push_back(e);
    }
    g.gravity_on = on;
}

void AbilitySystem::gravity_apply(Obj& g, uint32_t frame) {
    // Players in index order. An enemy of the owner: each of its units in range that is affected and not yet in any field
    // gets the slow and joins the list. Any other player (the owner included): each of its units in range that carries
    // this generator's slow loses it. A unit that leaves the range keeps the slow (only power loss or death ends it).
    const AbilityObjectDesc& gd = desc(g);
    const float slow = -gd.gravity_field_slow_fraction;
    if (slow == 0.0f) return;
    const Vec3 at = position(g);
    const float range = gd.gravity_field_range;
    const auto in_range = [&](const Obj& v) {
        // The field query: an XY circle (z ignored) around the generator [port: the unit's centre, not its box].
        const Vec3 p = position(v);
        const float dx = p.x - at.x, dy = p.y - at.y;
        return dx * dx + dy * dy <= range * range;
    };
    const uint32_t players = combat_->player_count();
    for (uint32_t pu = 0; pu < players; ++pu) {
        const auto p = static_cast<int32_t>(pu);
        const bool hostile = p != g.owner && enemy(p, g.owner);
        for (Obj& v : objs_) {
            if (v.owner != p || v.dead || !v.combat_unit || v.id == g.id) continue;
            if (!desc(v).affected_by_gravity_field || desc(v).garrison_style) continue;
            if (hostile ? v.in_gravity_field != 0 : v.in_gravity_field == 0) continue;
            if (!in_range(v)) continue;
            combat::ModifierSet* m = combat_->unit_modifiers(v.id);
            if (!m) continue;
            if (hostile) {
                m->set_by_source(ModList::MaxSpeed, 0, slow, combat::kPermanent, gd.name_crc, g.id);
                v.in_gravity_field = 1;
                g.gravity_list.push_back(v.id);
                AbilityEvent e;
                e.kind = AbilityEventKind::GravitySlow;
                e.id = g.id;
                e.target = v.id;
                e.value = 1;
                e.frame = frame;
                events_.push_back(e);
            } else if (m->remove_source_in_list(ModList::MaxSpeed, g.id, gd.name_crc) > 0) {
                gravity_release(g, v, frame);
            }
        }
    }
}

void AbilitySystem::gravity_release(Obj& g, Obj& victim, uint32_t frame) {
    // The flag, the list entry and the effect (the modifier entry is removed by the caller).
    victim.in_gravity_field = 0;
    const auto it = std::find(g.gravity_list.begin(), g.gravity_list.end(), victim.id);
    if (it != g.gravity_list.end()) g.gravity_list.erase(it);
    AbilityEvent e;
    e.kind = AbilityEventKind::GravitySlow;
    e.id = g.id;
    e.target = victim.id;
    e.value = 0;
    e.frame = frame;
    events_.push_back(e);
}

void AbilitySystem::gravity_release_all(Obj& g, uint32_t frame) {
    // Power lost or the generator gone: every listed unit, in list order, loses this generator's slow and its flag.
    const std::vector<ObjectId> list = std::move(g.gravity_list);
    g.gravity_list.clear();
    const uint32_t crc = desc(g).name_crc;
    for (const ObjectId id : list) {
        Obj* v = obj(id);
        if (!v) continue;
        v->in_gravity_field = 0;
        if (combat::ModifierSet* m = v->combat_unit ? combat_->unit_modifiers(id) : nullptr)
            m->remove_source_in_list(ModList::MaxSpeed, g.id, crc);
        AbilityEvent e;
        e.kind = AbilityEventKind::GravitySlow;
        e.id = g.id;
        e.target = id;
        e.value = 0;
        e.frame = frame;
        events_.push_back(e);
    }
}

void AbilitySystem::gravity_unit_dying(Obj& o, uint32_t frame) {
    // A dying generator releases its units [M: which behaviour vfunc runs at the generator's death]; a dying victim
    // leaves its generator's list (vf16: "Removing destroyed ... from the gravity control field list"). [port] A change
    // of the generator's owner releases nothing by itself: the next service re-sorts the units in range (now-friendly ones
    // are released, out-of-range ones keep the slow until power loss or death). From remove() (no frame) the release
    // events carry frame 0.
    if (!o.gravity_list.empty()) gravity_release_all(o, frame);
    if (o.in_gravity_field == 0) return;
    o.in_gravity_field = 0;
    for (Obj& g : objs_) {
        const auto it = std::find(g.gravity_list.begin(), g.gravity_list.end(), o.id);
        if (it == g.gravity_list.end()) continue;
        g.gravity_list.erase(it);
        break;
    }
}

}  // namespace alamo::tactical::abilities
