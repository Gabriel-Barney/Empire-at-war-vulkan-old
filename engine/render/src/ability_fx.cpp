#include "alamo/render/ability_fx.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

#include "alamo/core/ascii.hpp"
#include "alamo/render/beams.hpp"

#include "alamo/objects/game_object_types.hpp"
#ifdef ALAMO_SIM_HAVE_BATTLES
#include "alamo/sim/tactical_battle.hpp"
#include "alamo/tactical/abilities/ability_system.hpp"
#include "alamo/tactical/abilities/ability_types.hpp"
#include "alamo/tactical/combat/combat_system.hpp"
#endif

namespace alamo::render {

namespace {

// SnapshotEffect::percent of a one-shot (the key is fx_seq; the percent only labels it) [port].
constexpr uint8_t kEffectAbilityShot = 236;

// A particle object type's model, else the name itself as a particle file (as the jammer / lob particles do).
std::string particle_asset(const std::string& name, const SnapshotTypeTable* table) {
    if (table) {
        const objects::TypeIndex ti = table->find(name);
        if (ti != objects::kNone && table->valid(ti) && !(*table)[ti].model.empty()) return (*table)[ti].model;
    }
    return name;
}

}  // namespace

void AbilityFxTracker::reset() {
    live_.clear();
    next_seq_ = 1;
    visual_serial = 0;
    stun_serial = 0;
}

void AbilityFxTracker::apply(const AbilityFxInput& in, SimSnapshot& snap, const SnapshotTypeTable* table) {
    for (const AbilityFxInput::OneShot& s : in.shots) {
        if (s.particle.empty() || !snap.find(s.target)) continue;
        const std::string asset = particle_asset(s.particle, table);
        // [port] a type without a lifetime (retail: lives until destroyed) is kept once per target and asset, so a repeated
        // trigger does not pile up systems.
        if (s.lifetime == 0 && std::any_of(live_.begin(), live_.end(), [&](const Live& o) {
                return o.target == s.target && o.end == std::numeric_limits<uint32_t>::max() && o.asset == asset;
            }))
            continue;
        Live l;
        l.seq = next_seq_++;
        if (next_seq_ == 0) next_seq_ = 1;
        l.target = s.target;
        l.asset = asset;
        l.bones = s.bones;
        l.start = s.frame;
        l.end = s.lifetime > 0 ? s.frame + s.lifetime : std::numeric_limits<uint32_t>::max();
        live_.push_back(std::move(l));
    }
    // Retail removes the particle object at creation + Particle_Lifetime_Frames; a one-shot also ends with its target.
    std::erase_if(live_, [&](const Live& l) { return in.frame >= l.end || !snap.find(l.target); });
    std::vector<SnapshotEffect> fx;
    fx.reserve(in.states.size() + live_.size());
    for (const AbilityFxInput::State& s : in.states) {
        if (s.particle.empty()) continue;
        // one system per (target, percent): a second applier on the same target shares it
        if (std::any_of(fx.begin(), fx.end(), [&](const SnapshotEffect& e) { return e.id == s.target && e.percent == s.percent; }))
            continue;
        SnapshotEffect e;
        e.id = s.target;
        e.asset = particle_asset(s.particle, table);
        e.percent = s.percent;
        e.frame = in.frame;
        e.bone = s.bones;
        fx.push_back(std::move(e));
    }
    for (const Live& l : live_) {
        SnapshotEffect e;
        e.id = l.target;
        e.asset = l.asset;
        e.percent = kEffectAbilityShot;
        e.frame = l.start;
        e.bone = l.bones;
        e.fx_seq = l.seq;
        fx.push_back(std::move(e));
    }
    append_snapshot_effects(snap, fx);
}

std::vector<JamBlob> heal_range_blobs(const InterpolatedFrame& frame, std::span<const uint32_t> selected, int32_t local_player) {
    std::vector<JamBlob> out;
    for (const SnapshotJammer& h : frame.jamming.heal_blobs) {
        if (h.owner != local_player || !(h.radius > 0.0f) || !std::isfinite(h.radius)) continue;
        if (!std::binary_search(selected.begin(), selected.end(), static_cast<uint32_t>(h.id))) continue;
        const auto it = std::lower_bound(frame.objects.begin(), frame.objects.end(), h.id,
                                         [](const SnapshotObject& o, objects::ObjectId v) { return o.id < v; });
        if (it == frame.objects.end() || it->id != h.id) continue;
        if ((it->flags & (kSnapDying | kSnapContained | kSnapCorpse)) != 0 || !(it->fade > 0.0f)) continue;
        JamBlob b;
        b.jammer = h.id;
        b.center = it->position;
        b.half_size = h.radius;
        b.material = h.blob_material;
        for (size_t k = 0; k < 3; ++k) b.color[k] = h.blob_color[k];
        b.color[3] = 1.0f;
        out.push_back(std::move(b));
    }
    return out;
}

#ifdef ALAMO_SIM_HAVE_BATTLES
namespace {

std::string trimmed(std::string_view v) { return std::string(ascii_trim(v)); }

// A particle type's Particle_Lifetime_Frames (0 = none); false when `name` is no object type (retail: no particle).
bool particle_lifetime(const objects::GameObjectTypes* types, const std::string& name, uint32_t& lifetime) {
    const objects::TypeIndex t = types && !name.empty() ? types->resolve_name(name) : objects::kNone;
    if (t == objects::kNone) return false;
    const int32_t life = types->get_int(t, "Particle_Lifetime_Frames");
    lifetime = life > 0 ? static_cast<uint32_t>(life) : 0u;
    return true;
}

std::string join_bones(const std::vector<std::string>& bones) {
    std::string out;
    for (const std::string& b : bones) {
        if (!out.empty()) out += ',';
        out += b;
    }
    return out;
}

}  // namespace

void apply_battle_ability_fx(const sim::TacticalBattle& battle, AbilityFxTracker& tracker, std::string_view status_bones,
                             const objects::GameObjectTypes* types, SimSnapshot& snap, const SnapshotTypeTable* table) {
    namespace ab_ns = tactical::abilities;
    const ab_ns::AbilitySystem& ab = battle.abilities();
    const tactical::combat::CombatSystem& combat = battle.combat();
    AbilityFxInput in;
    in.frame = battle.frame();
    // [port] the victim keeps no infector type: Infection_Particle of the first special that names one (vanilla: IG-88 only).
    std::string infection;
    for (uint32_t i = 0; const ab_ns::SpecialAbilityDesc* d = ab.special_desc(i); ++i)
        if (!d->infection_particle.empty()) {
            infection = d->infection_particle;
            break;
        }
    std::vector<ab_ns::AppliedSpecialView> applied;
    snap.jamming.heal_blobs.clear();
    snap.world_effects.clear();
    for (const SnapshotObject& o : snap.objects) {  // ascending id
        if ((o.flags & kSnapCorpse) != 0) continue;
        if (ab.has_object(o.id)) {
            ab.applied_specials(o.id, applied);
            for (const ab_ns::AppliedSpecialView& a : applied) {
                const ab_ns::SpecialAbilityDesc* d = ab.special_desc(a.special);
                if (!d) continue;
                // Earthquake / Telekinesis (§2.2): the target particle while the effect holds on its target.
                if ((d->cls == ab_ns::SpecialClass::Earthquake || d->cls == ab_ns::SpecialClass::Telekinesis) &&
                    !d->target_particle_effect.empty() && a.target != objects::kNoObjectId)
                    in.states.push_back({a.target, kEffectTargetState, d->target_particle_effect, join_bones(d->target_particle_bones)});
                // vis1 t08 Force_Healing range blob (§8): listed while the special runs; drawn when selected (heal_range_blobs).
                if (d->cls == ab_ns::SpecialClass::ForceHealing && !d->blob_material.empty() && d->heal_range > 0.0f) {
                    SnapshotJammer h;
                    h.id = o.id;
                    h.owner = o.owner;
                    h.radius = d->heal_range;
                    h.blob_material = d->blob_material;
                    for (size_t k = 0; k < 4; ++k) h.blob_color[k] = d->blob_color[k];
                    snap.jamming.heal_blobs.push_back(std::move(h));
                }
                // vis1 t08 Drain_Life (§5): one lightning per victim while the drain runs.
                if (d->cls == ab_ns::SpecialClass::DrainLife && !d->drain_effect.empty()) {
                    for (const ab_ns::ObjectId v : a.victims) {
                        const SnapshotObject* vo = snap.find(static_cast<objects::ObjectId>(v));
                        if (!vo || (vo->flags & kSnapCorpse) != 0) continue;
                        SnapshotBeam b;
                        b.kind = kBeamDrain;
                        b.owner = o.id;
                        b.target = vo->id;
                        b.frame = battle.frame();
                        const float z = types && vo->type < types->size() ? types->get_float(vo->type, "Ranged_Target_Z_Adjust") : 0.0f;
                        b.end_z_adjust = std::isfinite(z) ? z : 0.0f;
                        b.style.texture.clear();  // lightning only
                        b.style.lightning = d->drain_effect;
                        b.style.start_bone = d->drain_source_bone;
                        b.style.end_bone = d->drain_target_bone;
                        snap.beams.push_back(std::move(b));
                    }
                }
            }
        }
        if (!combat.has_unit(o.id)) continue;
        // Infection (§7): while the victim's INFECTION behaviour is active (attached, not suspended).
        if (!infection.empty() && combat.infection_attached(o.id) && combat.infection_suspend_count(o.id) == 0)
            in.states.push_back({o.id, kEffectInfection, infection, std::string(status_bones)});
        // vis1 t08 Base_Shield_Penetration_Particle (§6): world-placed at the dome contact while the unit straddles a shield.
        // [M] orientation: facing = the horizontal part of the dome normal (retail: the normal's angles with pitch + 90).
        if (types && o.type < types->size() && combat.base_shield_straddled(o.id) != tactical::combat::kNoObject) {
            const std::string pen = trimmed(types->get_string(o.type, "Base_Shield_Penetration_Particle"));
            tactical::Vec3 point{}, normal{};
            if (!pen.empty() && types->resolve_name(pen) != objects::kNone && combat.base_shield_contact(o.id, point, normal)) {
                SimWorldEffect w;
                w.key = shield_penetration_key(o.id);
                w.asset = particle_asset(pen, table);
                w.position = Vec3{point.x, point.y, point.z};
                const float h = std::sqrt(normal.x * normal.x + normal.y * normal.y);
                w.facing = h > 1e-6f ? Vec3{normal.x / h, normal.y / h, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
                snap.world_effects.push_back(std::move(w));
            }
        }
    }
    // One-shots: the visual events after the last serial taken (a smaller newest serial: a new system, start over).
    const std::span<const ab_ns::AbilityVisualEvent> visuals = ab.visual_events();
    if (!visuals.empty() && visuals.back().serial < tracker.visual_serial) tracker.visual_serial = 0;
    for (const ab_ns::AbilityVisualEvent& v : visuals) {
        if (v.serial <= tracker.visual_serial) continue;
        tracker.visual_serial = v.serial;
        const ab_ns::SpecialAbilityDesc* d = ab.special_desc(v.special);
        if (!d) continue;
        AbilityFxInput::OneShot s;
        s.frame = v.frame;
        s.target = v.target;
        if (v.kind == ab_ns::AbilityVisualKind::StunnedEffect) {
            s.particle = d->stunned_effect;  // §4: the status bone list
            s.bones = std::string(status_bones);
        } else {
            s.particle = d->target_particle_effect;  // §2: the first Target_Particle_Bone_Name the model has
            s.bones = join_bones(d->target_particle_bones);
        }
        // §1.2: a null or unknown particle type does nothing (vanilla HUGE_DUD).
        if (!particle_lifetime(types, s.particle, s.lifetime)) continue;
        in.shots.push_back(std::move(s));
    }
    // vis1 t08 Projectile_Stun_Spawn_Effect (§3): one per stun of a stun detonation, at the status bones (same cursor rule).
    const std::span<const tactical::combat::StunVisualEvent> stuns = combat.stun_visual_events();
    if (!stuns.empty() && stuns.back().serial < tracker.stun_serial) tracker.stun_serial = 0;
    for (const tactical::combat::StunVisualEvent& e : stuns) {
        if (e.serial <= tracker.stun_serial) continue;
        tracker.stun_serial = e.serial;
        if (!types || e.projectile >= combat.data().projectiles.size()) continue;
        const objects::TypeIndex pt = types->resolve_name(combat.data().projectiles[e.projectile].name);
        if (pt == objects::kNone) continue;
        AbilityFxInput::OneShot s;
        s.frame = e.frame;
        s.target = static_cast<objects::ObjectId>(e.victim);
        s.particle = trimmed(types->get_string(pt, "Projectile_Stun_Spawn_Effect"));
        s.bones = std::string(status_bones);
        if (!particle_lifetime(types, s.particle, s.lifetime)) continue;
        in.shots.push_back(std::move(s));
    }
    tracker.apply(in, snap, table);
}
#endif

}  // namespace alamo::render
