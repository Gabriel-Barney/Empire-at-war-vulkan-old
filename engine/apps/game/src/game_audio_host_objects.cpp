// aud1 t05: ability, shield and hyperspace object SFX (GameAudioHost; rules: docs/re/AUDIO.md §9.6).
#include "alamo/game/game_audio_host.hpp"

#include "alamo/core/ascii.hpp"
#include "alamo/sim/tactical_battle.hpp"
#include "alamo/tactical/abilities/ability_types.hpp"
#ifdef ALAMO_GAME_HAVE_AUDIO
#include "alamo/audio/game_audio.hpp"
#include "alamo/audio/game_audio_data.hpp"
#endif

namespace alamo::game {

namespace {

// The trimmed text of the last direct child `tag` of sub-object field `f` ("" if none).
std::string_view child_text(const objects::ObjectTypeDatabase& db, objects::FieldIndex f, std::string_view tag) {
    const objects::FieldColumns& fc = db.fields();
    if (f >= fc.size()) return {};
    const objects::FieldIndex end = db.types().field_end[fc.type[f]];
    std::string_view out;
    for (objects::FieldIndex g = f + 1; g < end && fc.depth[g] > fc.depth[f]; ++g)
        if (fc.parent[g] == f && ascii_iequals(db.tag_name(g), tag)) out = fc.text[g];
    return out;
}

std::string_view name_attr(const objects::ObjectTypeDatabase& db, objects::FieldIndex f) {
    const objects::FieldColumns& fc = db.fields();
    if (f >= fc.size()) return {};
    for (uint32_t a = 0; a < fc.attr_count[f]; ++a) {
        const objects::XmlAttr& at = db.attributes()[fc.attr_begin[f] + a];
        if (ascii_iequals(at.name, "Name")) return ascii_trim(at.value);
    }
    return {};
}

}  // namespace

GameAudioHost::AbilitySfx GameAudioHost::ability_sfx(const objects::GameObjectTypes& types, objects::TypeIndex t,
                                                     uint8_t ability_type) {
    AbilitySfx out;
    if (t >= types.size()) return out;
    const objects::ObjectTypeDatabase& db = types.raw();
    // The Unit_Ability of this Type forwards to the special by GUI_Activated_Ability_Name (ABILITIES_HARDPOINTS 2.5).
    bool have_record = false;
    std::string_view special;
    if (ability_type != 0)
        for (const objects::FieldIndex f : types.get_sub_objects(t, "Unit_Abilities_Data")) {
            if (tactical::abilities::ability_type_from_name(ascii_trim(child_text(db, f, "Type"))) != ability_type) continue;
            have_record = true;
            special = ascii_trim(child_text(db, f, "GUI_Activated_Ability_Name"));
            break;
        }
    const std::span<const objects::FieldIndex> entries = types.get_sub_objects(t, "Abilities");
    objects::FieldIndex pick = objects::kNone;
    if (!special.empty()) {
        for (const objects::FieldIndex f : entries)
            if (ascii_iequals(name_attr(db, f), special)) {
                pick = f;
                break;
            }
    } else if (!have_record && entries.size() == 1) {
        pick = entries[0];  // [port] an auto-style special without a forwarding Unit_Ability: the type's only entry
    }
    if (pick == objects::kNone) return out;
    out.activate = std::string(ascii_trim(child_text(db, pick, "SFXEvent_Activate")));
    out.target_affected = std::string(ascii_trim(child_text(db, pick, "SFXEvent_Target_Affected")));
    return out;
}

std::string_view GameAudioHost::unit_ability_line(const objects::GameObjectTypes& types, objects::TypeIndex t, uint8_t ability_type,
                                                  std::string_view tag) {
    if (t >= types.size() || ability_type == 0) return {};
    const objects::ObjectTypeDatabase& db = types.raw();
    const std::span<const objects::FieldIndex> recs = types.get_sub_objects(t, "Unit_Abilities_Data");
    for (size_t i = 0; i < recs.size(); ++i) {
        if (tactical::abilities::ability_type_from_name(ascii_trim(child_text(db, recs[i], "Type"))) != ability_type) continue;
        const size_t pick = i == 0 ? 0 : 1;  // 0x374b90(type, 0 or 1): ability #0 -> record 0, any other -> record 1
        return pick < recs.size() ? ascii_trim(child_text(db, recs[pick], tag)) : std::string_view();
    }
    return {};
}

bool GameAudioHost::plays_target_ability_line(uint8_t a) {
    return a == 19 || (a >= 23 && a <= 26) || a == 28 || a == 35 || a == 36 || a == 42 || a == 39;
}

int32_t GameAudioHost::tsw_threshold_crossed(float prev_eta, float eta) {
    for (const int32_t t : {60, 30, 15, 5})
        if (prev_eta > static_cast<float>(t) && eta <= static_cast<float>(t)) return t;
    return 0;
}

std::string_view GameAudioHost::tsw_speech_tag(int32_t threshold, bool enemy) {
    switch (threshold) {
        case 60:
            if (enemy) return "SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_60_Seconds
            return "SpeechEvent_Super_Weapon_Moving_Range_60_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds
        case 30:
            if (enemy) return "SpeechEvent_Super_Weapon_Enemy_Moving_Range_30_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_30_Seconds
            return "SpeechEvent_Super_Weapon_Moving_Range_30_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Moving_Range_30_Seconds
        case 15:
            if (enemy) return "SpeechEvent_Super_Weapon_Enemy_Moving_Range_15_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_15_Seconds
            return "SpeechEvent_Super_Weapon_Moving_Range_15_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Moving_Range_15_Seconds
        case 5:
            if (enemy) return "SpeechEvent_Super_Weapon_Enemy_Moving_Range_05_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Enemy_Moving_Range_05_Seconds
            return "SpeechEvent_Super_Weapon_Moving_Range_05_Seconds";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Moving_Range_05_Seconds
        default:
            if (enemy) return "SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Enemy_Moved_Into_Range
            return "SpeechEvent_Super_Weapon_Moved_Into_Range";  // ledger: consumes Faction.SpeechEvent_Super_Weapon_Moved_Into_Range
    }
}

std::string_view GameAudioHost::toggle_line_tag(bool on, bool local_hostile) {
    // ledger: consumes Faction.SFXEvent_GUI_Toggle_Non_Hero_Ability_On
    // ledger: consumes Faction.SFXEvent_GUI_Toggle_Non_Hero_Ability_Off
    if (on) return local_hostile ? "SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On" : "SFXEvent_GUI_Toggle_Non_Hero_Ability_On";
    // ledger: consumes Faction.SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_On
    // ledger: consumes Faction.SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off
    return local_hostile ? "SFXEvent_GUI_Enemy_Toggle_Non_Hero_Ability_Off" : "SFXEvent_GUI_Toggle_Non_Hero_Ability_Off";
}

std::string_view GameAudioHost::departure_sfx(std::string_view type_retreat_jump, std::string_view faction_exit) {
    return !type_retreat_jump.empty() ? type_retreat_jump : faction_exit;
}

int GameAudioHost::shield_transition(int was_up, float fraction) {
    const bool up = fraction > 0.0f;
    if (was_up < 0) return 0;  // first sight: no transition
    if (was_up != 0 && !up) return 1;
    if (was_up == 0 && up) return 2;
    return 0;
}

void GameAudioHost::object_events_reset() {
    hyperspace_seen_ = 0;
    activation_seen_ = 0;
    shield_up_.clear();
}

#ifdef ALAMO_GAME_HAVE_AUDIO

void GameAudioHost::ability_effect(const objects::GameObjectTypes& types, objects::TypeIndex t, uint8_t ability_type, uint64_t user,
                                   const Vec3& user_position, uint64_t target, const Vec3& target_position) {
    if (!game_) return;
    const auto key = std::make_pair(t, ability_type);
    auto it = ability_sfx_cache_.find(key);
    if (it == ability_sfx_cache_.end()) it = ability_sfx_cache_.emplace(key, ability_sfx(types, t, ability_type)).first;
    // [port] An Is_2D event plays 2D attached to the object (GameAudio::object_sfx) where retail plays it 2D unattached
    // (0x2d5290): the same sound, only stopped with the object.
    if (!it->second.activate.empty() && game_->object_sfx(user, it->second.activate, user_position) != 0) ++ability_sfx_played_;
    if (target != 0 && !it->second.target_affected.empty() &&
        game_->object_sfx(target, it->second.target_affected, target_position) != 0)
        ++ability_sfx_played_;
}

uint32_t GameAudioHost::hyperspace_arrival(uint64_t object, std::string_view owner_faction, const Vec3& position) {
    if (!game_ || owner_faction.empty()) return 0;
    const audio::FactionAudio* f = game_data_->find_faction(owner_faction);
    const std::string_view ev = f ? f->get("SFXEvent_Arrive_From_Hyperspace") : std::string_view{};
    const uint32_t id = ev.empty() ? 0u : game_->object_sfx(object, ev, position);
    if (id != 0) ++hyperspace_sfx_played_;
    return id;
}

uint32_t GameAudioHost::cinematic_arrival(uint64_t object, int32_t owner, const Vec3& position) {
    if (owner < 0 || static_cast<size_t>(owner) >= player_factions_.size()) return 0;
    return hyperspace_arrival(object, player_factions_[static_cast<size_t>(owner)], position);
}

uint32_t GameAudioHost::cinematic_engine_flyby(const objects::GameObjectTypes& types, objects::TypeIndex type, uint64_t object,
                                               const Vec3& position) {
    if (!game_ || type >= types.size()) return 0;
    const std::string_view ev = ascii_trim(types.get_string(type, "SFXEvent_Ambient_Moving"));
    return ev.empty() ? 0u : game_->object_sfx(object, ev, position);
}

void GameAudioHost::ability_activations(std::span<const tactical::abilities::AbilityActivation> log, const objects::ObjectStore& store,
                                        const objects::GameObjectTypes& types) {
    if (!log.empty() && log.back().serial < activation_seen_) activation_seen_ = 0;
    for (const tactical::abilities::AbilityActivation& e : log) {
        if (e.serial <= activation_seen_) continue;
        activation_seen_ = e.serial;
        const objects::ObjectHandle h = store.find(static_cast<objects::ObjectId>(e.id));
        if (!store.alive(h)) continue;
        uint64_t target = 0;
        Vec3 tp{};
        if (e.target != tactical::abilities::kNoObject) {
            const objects::ObjectHandle th = store.find(static_cast<objects::ObjectId>(e.target));
            if (store.alive(th)) {
                target = static_cast<uint64_t>(e.target);
                tp = store.position(th);
            }
        }
        ability_effect(types, store.type(h), e.type, static_cast<uint64_t>(e.id), store.position(h), target, tp);
        // mcx4 t13 (MCX4_T13_RE_ANSWERS "Unit_Ability.SFXEvent_Target_Ability", 0x3a3e20): attached to the unit, any owner.
        if (plays_target_ability_line(e.type))
            // ledger: consumes Unit_Ability.SFXEvent_Target_Ability
            if (const std::string_view ev = unit_ability_line(types, store.type(h), e.type, "SFXEvent_Target_Ability"); !ev.empty())
                if (game_->object_sfx(static_cast<uint64_t>(e.id), ev, store.position(h)) != 0) ++stats_.ability_lines;
    }
}

void GameAudioHost::ability_button_line(const objects::GameObjectTypes& types, objects::TypeIndex t, uint8_t ability_type, bool activate) {
    if (!game_) return;
    // ledger: consumes Unit_Ability.SFXEvent_GUI_Unit_Ability_Activated
    // ledger: consumes Unit_Ability.SFXEvent_GUI_Unit_Ability_Deactivated
    const std::string_view ev =
        unit_ability_line(types, t, ability_type, activate ? "SFXEvent_GUI_Unit_Ability_Activated" : "SFXEvent_GUI_Unit_Ability_Deactivated");
    if (!ev.empty() && game_->gui_sfx(ev) != 0) ++stats_.ability_lines;
}

void GameAudioHost::object_events(const sim::TacticalBattle& battle, const objects::ObjectStore& store,
                                  const objects::GameObjectTypes& types, std::span<const std::string> player_factions) {
    if (!game_) return;
    auto faction_tag = [&](int32_t owner, std::string_view tag) -> std::string_view {
        if (owner < 0 || static_cast<size_t>(owner) >= player_factions.size() || player_factions[static_cast<size_t>(owner)].empty())
            return {};
        const audio::FactionAudio* f = game_data_->find_faction(player_factions[static_cast<size_t>(owner)]);
        return f ? f->get(tag) : std::string_view{};
    };
    // Abilities: EffectOn = the activation presentation (light, particle, sound), event order (aud1 t10: the kept copy).
    ability_activations(battle.abilities().activation_events(), store, types);
    // Hyperspace: the battle's append-only log from our cursor (a shorter log = a new battle or a load). An Arrive of a
    // reinforcement held in hyperspace (LINK_TACTICAL Param9 = 2) is logged only after the script's Resume_Hyperspace_In
    // clears the hold (TacticalBattle::arrival_held; L1_S4_REMAINING §3.4, 0x543570):
    // binding-consumer: Resume_Hyperspace_In (docs/re/MCX6_T05_CINEMATIC_BINDINGS.md §6)
    const std::span<const sim::TacticalBattle::HyperspaceSfx> log = battle.hyperspace_sfx();
    if (log.size() < hyperspace_seen_) hyperspace_seen_ = 0;
    for (; hyperspace_seen_ < log.size(); ++hyperspace_seen_) {
        const sim::TacticalBattle::HyperspaceSfx& r = log[hyperspace_seen_];
        if (r.kind == sim::TacticalBattle::HyperspaceSfx::Kind::Arrive) {
            const std::string_view fac = r.owner >= 0 && static_cast<size_t>(r.owner) < player_factions.size()
                                             ? std::string_view(player_factions[static_cast<size_t>(r.owner)]) : std::string_view();
            hyperspace_arrival(static_cast<uint64_t>(r.id), fac, r.position);
        } else {
            const std::string_view type_ev =
                r.type < types.size() ? ascii_trim(types.get_string(r.type, "SFXEvent_Retreat_Engines_Jump")) : std::string_view{};
            const std::string_view ev = departure_sfx(type_ev, type_ev.empty() ? faction_tag(r.owner, "SFXEvent_Exit_Into_Hyperspace")
                                                                                : std::string_view{});
            if (!ev.empty() && game_->world_sfx(ev, r.position) != 0) ++hyperspace_sfx_played_;
        }
    }
    // Shields (0x549810): down on the fall to 0 points, back up on the first regen above 0; only types with a tag.
    const tactical::combat::CombatSystem& cs = battle.combat();
    for (size_t i = 0; i < cs.unit_count(); ++i) {
        const tactical::combat::ObjectId cid = cs.unit_id_at(i);
        const objects::ObjectHandle h = store.find(static_cast<objects::ObjectId>(cid));
        if (!store.alive(h)) continue;
        const objects::TypeIndex t = store.type(h);
        auto st = shield_tags_.find(t);
        if (st == shield_tags_.end())
            st = shield_tags_
                     .emplace(t, std::make_pair(std::string(ascii_trim(types.get_string(t, "SFXEvent_Shields_Go_Down"))),
                                                std::string(ascii_trim(types.get_string(t, "SFXEvent_Shields_Go_Back_Up")))))
                     .first;
        if (st->second.first.empty() && st->second.second.empty()) continue;
        const float fraction = cs.shield_fraction(cid);
        const objects::ObjectId id = static_cast<objects::ObjectId>(cid);
        auto su = shield_up_.find(id);
        const int was = su == shield_up_.end() ? -1 : su->second;
        const int tr = shield_transition(was, fraction);
        shield_up_[id] = fraction > 0.0f ? 1 : 0;
        const std::string& ev = tr == 1 ? st->second.first : tr == 2 ? st->second.second : std::string();
        if (!ev.empty() && game_->object_sfx(static_cast<uint64_t>(id), ev, store.position(h)) != 0) ++shield_sfx_played_;
    }
}

#else  // no engine/audio: inactive host

void GameAudioHost::ability_effect(const objects::GameObjectTypes&, objects::TypeIndex, uint8_t, uint64_t, const Vec3&, uint64_t,
                                   const Vec3&) {}
void GameAudioHost::ability_activations(std::span<const tactical::abilities::AbilityActivation>, const objects::ObjectStore&,
                                        const objects::GameObjectTypes&) {}
void GameAudioHost::ability_button_line(const objects::GameObjectTypes&, objects::TypeIndex, uint8_t, bool) {}
void GameAudioHost::object_events(const sim::TacticalBattle&, const objects::ObjectStore&, const objects::GameObjectTypes&,
                                  std::span<const std::string>) {}
uint32_t GameAudioHost::hyperspace_arrival(uint64_t, std::string_view, const Vec3&) { return 0; }
uint32_t GameAudioHost::cinematic_arrival(uint64_t, int32_t, const Vec3&) { return 0; }
uint32_t GameAudioHost::cinematic_engine_flyby(const objects::GameObjectTypes&, objects::TypeIndex, uint64_t, const Vec3&) { return 0; }

#endif

}  // namespace alamo::game
