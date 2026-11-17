#include "alamo/render/ability_fx.hpp"

#include <algorithm>
#include <limits>
#include <span>

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

#ifdef ALAMO_SIM_HAVE_BATTLES
namespace {

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
    for (const SnapshotObject& o : snap.objects) {  // ascending id
        if ((o.flags & kSnapCorpse) != 0) continue;
        if (ab.has_object(o.id)) {
            // Earthquake / Telekinesis (ABILITY_PARTICLES §2.2): the target particle while the effect holds on its target.
            ab.applied_specials(o.id, applied);
            for (const ab_ns::AppliedSpecialView& a : applied) {
                const ab_ns::SpecialAbilityDesc* d = ab.special_desc(a.special);
                if (!d || d->target_particle_effect.empty() || a.target == objects::kNoObjectId) continue;
                if (d->cls != ab_ns::SpecialClass::Earthquake && d->cls != ab_ns::SpecialClass::Telekinesis) continue;
                in.states.push_back({a.target, kEffectTargetState, d->target_particle_effect, join_bones(d->target_particle_bones)});
            }
        }
        // Infection (§7): while the victim's INFECTION behaviour is active (attached, not suspended).
        if (!infection.empty() && combat.has_unit(o.id) && combat.infection_attached(o.id) && combat.infection_suspend_count(o.id) == 0)
            in.states.push_back({o.id, kEffectInfection, infection, std::string(status_bones)});
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
        const objects::TypeIndex t = types && !s.particle.empty() ? types->resolve_name(s.particle) : objects::kNone;
        if (t == objects::kNone) continue;
        const int32_t life = types->get_int(t, "Particle_Lifetime_Frames");
        s.lifetime = life > 0 ? static_cast<uint32_t>(life) : 0u;
        in.shots.push_back(std::move(s));
    }
    tracker.apply(in, snap, table);
}
#endif

}  // namespace alamo::render
