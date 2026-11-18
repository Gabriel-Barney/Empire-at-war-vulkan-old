#include "alamo/game/game_audio_host.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "alamo/core/ascii.hpp"
#include "alamo/data/values.hpp"
#include "alamo/objects/behaviors.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/sim/tactical_battle.hpp"
#include "alamo/tactical/hangar/contents.hpp"
#ifdef ALAMO_GAME_HAVE_AUDIO
#include "alamo/audio/audio_db.hpp"
#include "alamo/audio/audio_system.hpp"
#include "alamo/audio/game_audio.hpp"
#include "alamo/audio/game_audio_data.hpp"
#include "alamo/audio/miniaudio_backend.hpp"
#include "alamo/audio/sample_source.hpp"
#include "alamo/vfs/vfs.hpp"
#ifdef ALAMO_AUDIO_HAVE_LUA
#include "alamo/audio/lua_audio.hpp"
#endif
#else
namespace alamo::audio {  // complete stand-ins for the owned pointers (no engine/audio in this build)
class AudioDatabase {};
class AudioSystem {};
class VoiceBackend {};
class SampleSource {};
class GameAudioData {};
class GameAudio {};
struct UnitSounds {};
}  // namespace alamo::audio
#endif

namespace alamo::game {

std::string_view GameAudioHost::control_point_line(int32_t prev, int32_t timer, int32_t leader, int32_t local_player, bool leader_is_enemy) {
    if (timer < 0 || leader < 0 || local_player < 0) return {};
    static constexpr struct {
        int32_t frames;
        const char* owned;
        const char* ally;
        const char* enemy;
    } kMarks[] = {
        {60 * 30, "SFXEvent_HUD_Reinforcement_Point_Owned_60_Seconds", "SFXEvent_HUD_Reinforcement_Point_Ally_Owned_60_Seconds",
         "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_60_Seconds"},
        {30 * 30, "SFXEvent_HUD_Reinforcement_Point_Owned_30_Seconds", "SFXEvent_HUD_Reinforcement_Point_Ally_Owned_30_Seconds",
         "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_30_Seconds"},
        {15 * 30, "SFXEvent_HUD_Reinforcement_Point_Owned_15_Seconds", "SFXEvent_HUD_Reinforcement_Point_Ally_Owned_15_Seconds",
         "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_15_Seconds"},
        {5 * 30, "SFXEvent_HUD_Reinforcement_Point_Owned_05_Seconds", "SFXEvent_HUD_Reinforcement_Point_Ally_Owned_05_Seconds",
         "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_05_Seconds"},
    };
    const bool armed_now = prev < 0 || timer > prev;
    for (const auto& m : kMarks) {
        const bool hit = armed_now ? (timer == m.frames || timer + 1 == m.frames) : (prev > m.frames && m.frames >= timer);
        if (!hit) continue;
        if (leader == local_player) return m.owned;
        return leader_is_enemy ? m.enemy : m.ally;
    }
    return {};
}

std::string_view GameAudioHost::garrison_line(int32_t unit_owner, int32_t local_player) {
    return local_player >= 0 && unit_owner == local_player ? std::string_view("SFXEvent_Bunker_Garrisoned") : std::string_view();
}

std::string_view GameAudioHost::health_warning_tag(float before, float after, float low, float critical, bool local_victim,
                                                   bool local_attacker) {
    const bool crit = before > critical && after <= critical;
    const bool lo = !crit && before > low && after <= low;
    if (!crit && !lo) return {};
    if (local_victim) return crit ? "SFXEvent_Health_Critical_Warning" : "SFXEvent_Health_Low_Warning";
    if (local_attacker) return crit ? "SFXEvent_Enemy_Damaged_Health_Critical_Warning" : "SFXEvent_Enemy_Damaged_Health_Low_Warning";
    return {};
}

std::string_view GameAudioHost::hardpoint_lost_tag(uint8_t kind, bool local_parent, bool parent_dying, bool last_of_kind,
                                                   bool weapons_left) {
    if (!local_parent || parent_dying) return {};
    if (kind >= 5 && kind <= 10 && !weapons_left) return "SFXEvent_Hardpoint_All_Weapons_Destroyed";
    return last_of_kind ? std::string_view("SFXEvent_Hardpoint_Destroyed") : std::string_view();
}

std::string_view GameAudioHost::battle_result_line(bool local_won, bool local_hostile, bool space, bool enemy_tsw, bool own_killer,
                                                   bool corruption) {
    if (local_won) {
        if (!space) {
            if (enemy_tsw) return "SFXEvent_HUD_Won_Land_Battle_Enemy_TSW_Present";
            return corruption ? "SFXEvent_HUD_Won_Tactical_Corruption_Mission" : "SFXEvent_HUD_Won_Land_Battle";
        }
        if (enemy_tsw) return own_killer ? "SFXEvent_HUD_Won_Space_Battle_Enemy_TSW_Present" : "Space_Forced_Retreat_Due_To_Superweapon";
        return corruption ? "SFXEvent_HUD_Won_Tactical_Corruption_Mission" : "SFXEvent_HUD_Won_Space_Battle";
    }
    if (!local_hostile) return {};
    if (enemy_tsw) return space ? "SFXEvent_HUD_Lost_Space_Battle_Enemy_TSW_Present" : "SFXEvent_HUD_Lost_Land_Battle_Enemy_TSW_Present";
    if (corruption) return "SFXEvent_HUD_Lost_Tactical_Corruption_Mission";
    return space ? "SFXEvent_HUD_Lost_Space_Battle" : "SFXEvent_HUD_Lost_Land_Battle";
}

std::string_view GameAudioHost::retreat_begin_tag(bool local_retreats, bool local_hostile, bool space) {
    // ledger: consumes Faction.Land_Retreat_Begin_SFXEvent
    // ledger: consumes Faction.Space_Retreat_Begin_SFXEvent
    if (local_retreats) return space ? "Space_Retreat_Begin_SFXEvent" : "Land_Retreat_Begin_SFXEvent";
    // ledger: consumes Faction.Land_Retreat_Enemy_Begin_SFXEvent
    // ledger: consumes Faction.Space_Retreat_Enemy_Begin_SFXEvent
    if (local_hostile) return space ? "Space_Retreat_Enemy_Begin_SFXEvent" : "Land_Retreat_Enemy_Begin_SFXEvent";
    return {};
}

std::string_view GameAudioHost::move_into_field_tag(uint32_t dest_fields) {
    // ledger: consumes GameObjectType.SFXEvent_Move_Into_Asteroid_Field
    if ((dest_fields & 1u) != 0) return "SFXEvent_Move_Into_Asteroid_Field";  // tactical::abilities::kFieldAsteroid
    // ledger: consumes GameObjectType.SFXEvent_Move_Into_Nebula
    if ((dest_fields & 4u) != 0) return "SFXEvent_Move_Into_Nebula";  // kFieldNebula
    return {};
}

std::string_view GameAudioHost::special_weapon_tag(bool fired, bool own) {
    // ledger: consumes Faction.SFXEvent_HUD_Special_Weapon_Ready
    // ledger: consumes Faction.SFXEvent_HUD_Enemy_Special_Weapon_Ready
    if (!fired) return own ? "SFXEvent_HUD_Special_Weapon_Ready" : "SFXEvent_HUD_Enemy_Special_Weapon_Ready";
    // ledger: consumes Faction.SFXEvent_HUD_Special_Weapon_Firing
    // ledger: consumes Faction.SFXEvent_HUD_Enemy_Special_Weapon_Firing
    return own ? "SFXEvent_HUD_Special_Weapon_Firing" : "SFXEvent_HUD_Enemy_Special_Weapon_Firing";
}

std::string_view GameAudioHost::nth_field(std::span<const std::pair<std::string, std::string>> fields, std::string_view tag,
                                          int32_t index) {
    int32_t n = 0;
    for (const auto& [k, v] : fields) {
        if (!ascii_iequals(k, tag)) continue;
        if (n++ == index) return v;
    }
    return {};
}

std::string_view GameAudioHost::base_shield_line(bool online, bool friendly) {
    // ledger: consumes Faction.SFXEvent_HUD_Base_Shield_Online
    // ledger: consumes Faction.SFXEvent_HUD_Enemy_Base_Shield_Online
    if (online) return friendly ? "SFXEvent_HUD_Base_Shield_Online" : "SFXEvent_HUD_Enemy_Base_Shield_Online";
    // ledger: consumes Faction.SFXEvent_HUD_Base_Shield_Offline
    // ledger: consumes Faction.SFXEvent_HUD_Enemy_Base_Shield_Offline
    return friendly ? "SFXEvent_HUD_Base_Shield_Offline" : "SFXEvent_HUD_Enemy_Base_Shield_Offline";
}

GameAudioHost::ControlLines GameAudioHost::control_lines(bool friendly_old, bool friendly_new, bool hostile_old, bool hostile_new,
                                                         bool old_neutral, bool new_neutral) {
    ControlLines c;
    if (friendly_old || friendly_new)
        c.friendly = friendly_old ? "SFXEvent_Tactical_Lose_Friendly_Control" : "SFXEvent_Tactical_Gain_Friendly_Control";
    if (!old_neutral && hostile_old) c.enemy = "SFXEvent_Tactical_Lose_Enemy_Control";
    else if (!new_neutral && hostile_new) c.enemy = "SFXEvent_Tactical_Gain_Enemy_Control";
    return c;
}

std::string_view GameAudioHost::hardpoint_kind_event(std::span<const std::string_view> tokens, uint8_t kind) {
    static constexpr const char* kNames[13] = {
        "HARD_POINT_ENGINE",         "HARD_POINT_SHIELD_GENERATOR", "HARD_POINT_GRAVITY_WELL",  "HARD_POINT_FIGHTER_BAY",
        "HARD_POINT_TRACTOR_BEAM",   "HARD_POINT_WEAPON_LASER",     "HARD_POINT_WEAPON_MISSILE", "HARD_POINT_WEAPON_TORPEDO",
        "HARD_POINT_WEAPON_ION_CANNON", "HARD_POINT_WEAPON_MASS_DRIVER", "HARD_POINT_WEAPON_SPECIAL",
        "HARD_POINT_ENABLE_SPECIAL_ABILITY", "HARD_POINT_DUMMY_ART"};
    if (kind >= 13) return {};
    const auto is_kind = [](std::string_view t) { return t.size() > 11 && ascii_istarts_with(t, "HARD_POINT_"); };
    std::string_view hit;  // a later line for the same kind replaces an earlier one (one slot per kind)
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (!is_kind(tokens[i]) || !ascii_iequals(tokens[i], kNames[kind])) continue;
        hit = i + 1 < tokens.size() && !is_kind(tokens[i + 1]) ? tokens[i + 1] : std::string_view();
    }
    return hit;
}

std::string_view GameAudioHost::clash_win_speech(const tactical::combat::HeroClashData* data, uint32_t entry, bool first_won) {
    if (!data || entry >= data->entries.size()) return {};
    return first_won ? data->entries[entry].first_speech : data->entries[entry].second_speech;
}

std::string_view GameAudioHost::weather_name(uint8_t type) {
    static constexpr const char* kNames[] = {"CLEAR", "RAIN", "SNOW", "SANDSTORM", "ASH"};  // LandModeClass::tLandWeather order
    return type < std::size(kNames) ? std::string_view(kNames[type]) : std::string_view("CLEAR");
}

bool GameAudioHost::base_attacked(bool victim_is_base, bool attacker_hostile, int32_t victim_owner, int32_t local_player,
                                  bool owner_is_ally, bool community_property) {
    if (!victim_is_base || !attacker_hostile || local_player < 0 || victim_owner < 0) return false;
    return victim_owner == local_player || (owner_is_ally && community_property);
}

std::string_view GameAudioHost::fire_event(std::string_view hardpoint_fire, std::string_view shooter_fire) {
    return hardpoint_fire.empty() ? shooter_fire : hardpoint_fire;
}

std::string_view GameAudioHost::terrain_name(uint32_t type) {
    static constexpr const char* kNames[] = {"Temperate", "Arctic", "Desert", "Forest", "Swamp", "Volcanic", "Urban", "Space"};  // 0x5e8770
    return type < std::size(kNames) ? std::string_view(kNames[type]) : std::string_view();
}

Vec3 GameAudioHost::position_of(uint64_t id) const {
    const auto it = positions_.find(id);
    return it == positions_.end() ? Vec3{} : it->second;
}

#ifdef ALAMO_GAME_HAVE_AUDIO

namespace {
audio::UnitOrder order_of(UnitResponse k, bool group) {
    switch (k) {
        case UnitResponse::Select: return audio::UnitOrder::Select;
        case UnitResponse::Move: return group ? audio::UnitOrder::GroupMove : audio::UnitOrder::Move;
        case UnitResponse::FleetMove: return audio::UnitOrder::FleetMove;
        case UnitResponse::Attack: return group ? audio::UnitOrder::GroupAttack : audio::UnitOrder::Attack;
        case UnitResponse::Guard: return audio::UnitOrder::Guard;
        case UnitResponse::Stop: return audio::UnitOrder::Stop;
    }
    return audio::UnitOrder::Select;
}
Vec3 v3(const tactical::Vec3& v) { return {v.x, v.y, v.z}; }
}  // namespace

bool GameAudioHost::init(const GameData& data, AudioBackendKind kind, uint32_t seed, std::string* err) {
    rng_.seed(seed);
    if (kind == AudioBackendKind::Off) return true;
    db_ = std::make_unique<audio::AudioDatabase>();
    if (!db_->load(*data.files)) {
        if (err) *err = "SFXEventFiles.xml missing";
        db_.reset();
        return false;
    }
    game_data_ = std::make_unique<audio::GameAudioData>();
    game_data_->load(*data.files);
    // mcx4 t13: the health warning thresholds (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Health_Low_Warning family").
    health_low_ = data.db.constants().get_float("Health_Low_Percent_Threshold", health_low_);
    health_critical_ = data.db.constants().get_float("Health_Critical_Percent_Threshold", health_critical_);
    audio::GameSampleConfig sc;
    if (data.vfs) sc.sfx_megs = audio::GameSampleSource::default_sfx_megs(*data.vfs, sc.language);
    samples_ = std::make_unique<audio::GameSampleSource>(*data.files, sc);
    if (kind == AudioBackendKind::Device) {
        std::string e;
        backend_ = audio::MiniaudioBackend::create(*samples_, audio::MiniaudioConfig{}, &e);
        if (!backend_) {
            if (err) *err = "device: " + e;
            return false;
        }
    } else {
        const audio::SampleSource* s = samples_.get();
        backend_ = std::make_unique<audio::NullBackend>([s](const audio::SampleRef& r) -> std::optional<double> {
            if (!s->exists(r)) return std::nullopt;
            return 1.0;  // [port] nominal length: the null backend only needs a deterministic duration
        });
    }
    system_ = std::make_unique<audio::AudioSystem>(*db_, *backend_, rng_);
    system_->on_speech_done([this](const std::string& event, bool) { speech_done_.push_back(event); });  // L1/S4 wave 1
    game_ = std::make_unique<audio::GameAudio>(*system_, *game_data_, rng_);
    game_->set_object_query([this](uint64_t owner) -> std::optional<audio::AudioSystem::ObjectAudio> {
        const auto it = positions_.find(owner);
        if (it == positions_.end()) return std::nullopt;
        audio::AudioSystem::ObjectAudio a;
        a.position = it->second;
        a.audible = inaudible_.find(owner) == inaudible_.end();  // §3.5 item 8: fog / hidden / stealth mute, no stop
        a.hidden = hidden_.find(owner) != hidden_.end();          // AUDIO.md §1.6 (b0280): a cinematic unmutes fog only
        return a;
    });
    return true;
}

const audio::UnitSounds& GameAudioHost::sounds(const objects::GameObjectTypes& types, objects::TypeIndex t) {
    auto it = sounds_.find(t);
    if (it != sounds_.end()) return *it->second;
    auto s = std::make_unique<audio::UnitSounds>();
    if (t != objects::kNone && t < types.size()) {
        static constexpr const char* kTags[audio::kUnitOrderCount] = {
            "SFXEvent_Select",     "SFXEvent_Move",        "SFXEvent_Fleet_Move", "SFXEvent_Attack",       "SFXEvent_Guard",
            "SFXEvent_Defend",     "SFXEvent_Stop",        "SFXEvent_Deploy",     "SFXEvent_Undeploy",     "SFXEvent_Move_Opposite",
            "SFXEvent_Group_Move", "SFXEvent_Group_Attack", "SFXEvent_Assist_Move", "SFXEvent_Assist_Attack"};
        for (size_t i = 0; i < audio::kUnitOrderCount; ++i) s->response[i] = std::string(types.get_string(t, kTags[i]));
        s->unit_lost = std::string(types.get_string(t, "SFXEvent_Unit_Lost"));
        s->unit_under_attack = std::string(types.get_string(t, "SFXEvent_Unit_Under_Attack"));
        s->fire = std::string(types.get_string(t, "SFXEvent_Fire"));
        s->death_start = std::string(types.get_string(t, "Death_SFXEvent_Start_Die"));
        s->death_end = std::string(types.get_string(t, "Death_SFXEvent_End_Die"));
        s->ambient_loop = std::string(types.get_string(t, "SFXEvent_Ambient_Loop"));
        s->ambient_moving = std::string(types.get_string(t, "SFXEvent_Ambient_Moving"));
        s->engine_idle_loop = std::string(types.get_string(t, "SFXEvent_Engine_Idle_Loop"));
        s->engine_moving_loop = std::string(types.get_string(t, "SFXEvent_Engine_Moving_Loop"));
        s->engine_cinematic_focus_loop = std::string(types.get_string(t, "SFXEvent_Engine_Cinematic_Focus_Loop"));
        s->projectile_detonate = std::string(types.get_string(t, "Projectile_SFXEvent_Detonate"));
    }
    return *sounds_.emplace(t, std::move(s)).first->second;
}

void GameAudioHost::ui(std::string_view event) {
    if (game_ && !event.empty() && game_->gui_sfx(event)) ++stats_.ui;
}

void GameAudioHost::speech(std::string_view event) {
    if (game_ && !event.empty() && game_->speech(event)) ++stats_.speech;
}

void GameAudioHost::faction_sfx(std::string_view faction_tag) {
    if (!game_) return;
    ++stats_.feedback;
    game_->faction_sfx(faction_tag);
}

void GameAudioHost::battle_pending(bool land) {
    if (!game_) return;
    ++stats_.battle_pending;
    pending_played_ = true;
    game_->battle_pending(land);
}

void GameAudioHost::galactic_resume() {
    if (!game_ || scene_ != MusicScene::Galactic) return;
    pending_played_ = false;
    game_->enter_galactic();
}

void GameAudioHost::movie_started(bool movie_music, bool halt_music) {
    if (!game_) return;
    if (halt_music) {
        game_->halt_game_music();
        movie_halted_ = true;
        movie_music_on_ = false;
        ++stats_.movie_halts;
    }
    if (movie_music && !halt_music) {  // [port] a halted movie plays no music of its own
        game_->movie_music(kCreditsMusicEvent);
        if (system_ && system_->music_mode() == audio::music_mode::kMovie) {
            movie_music_on_ = true;
            ++stats_.movie_music;
        }
    }
}

void GameAudioHost::movie_cue(std::string_view music_event) {
    if (!game_ || music_event.empty()) return;
    const bool before = system_ && system_->music_mode() == audio::music_mode::kMovieCue;
    game_->movie_music_cue(music_event);
    ++stats_.movie_cues;
    if (system_ && system_->music_mode() == audio::music_mode::kMovieCue && !before) movie_music_on_ = true;
}

void GameAudioHost::movie_ended() {
    if (!game_) return;
    if (movie_music_on_ && game_->movie_music_end()) ++stats_.movie_restores;
    movie_music_on_ = false;
    if (movie_halted_) {  // Halt_Game_Music: the silence ends with the movie, nothing else started meanwhile
        movie_halted_ = false;
        if (system_ && system_->current_music().empty()) {
            system_->resume_mode_based_music(false);
            ++stats_.movie_restores;
        }
    }
}

void GameAudioHost::negative_feedback() {
    if (!game_) return;
    ++stats_.feedback;
    game_->negative_feedback();
}

void GameAudioHost::unit_response(const objects::GameObjectTypes& types, objects::TypeIndex type, uint64_t object, UnitResponse kind,
                                  bool group, int32_t owner, uint32_t dest_fields) {
    if (!game_) return;
    // The mode's "excluded set" = the retreat coordinator's retreating units (STRATEGIC_GAPS §7.4 [M]): no response.
    if (owner >= 0 && owner == retreating_player_) {
        ++stats_.excluded;
        return;
    }
    const audio::UnitOrder order = order_of(kind, group);
    if (const std::string_view tag = kind == UnitResponse::Move ? move_into_field_tag(dest_fields) : std::string_view(); !tag.empty())
        if (const std::string_view ev = types.get_string(type, tag); !ev.empty()) {
            audio::UnitSounds s = sounds(types, type);  // the field line replaces the move response (0x3738b0)
            s.response[static_cast<size_t>(order)] = std::string(ev);
            if (game_->unit_response(object, s, order, position_of(object), group)) ++stats_.responses;
            return;
        }
    if (game_->unit_response(object, sounds(types, type), order, position_of(object), group)) ++stats_.responses;
}

void GameAudioHost::battle_end(bool won, std::string_view enemy_faction) {
    if (!game_) return;
    audio::BattleResult r;
    r.won = won;
    r.enemy_faction = std::string(enemy_faction);
    game_->battle_end(r);  // mode 16 (STRATEGIC_GAPS §7.4)
    ++stats_.battle_end_music;
}

void GameAudioHost::battle_tick(const sim::TacticalBattle& battle, const objects::ObjectStore& store, const objects::GameObjectTypes& types,
                                int32_t local_player, const std::function<bool(objects::ObjectId)>& script_hidden) {
    if (!game_) return;
    retreating_player_ = battle.retreating_player();
    const tactical::combat::CombatSystem& cs = battle.combat();
    const tactical::movement::MovementSystem& mv = battle.movement();
    bool combat = false;
    auto handle_of = [&store](int64_t id) { return store.find(static_cast<objects::ObjectId>(id)); };
    auto owner_of = [&](int64_t id) {
        const objects::ObjectHandle h = handle_of(id);
        return store.exists(h) ? store.owner(h) : -1;
    };
    // Battle units new since the last tick: ambient loop + engine mode enter (object_spawned). STATION_HANGARS_SOUND §3:
    // every locomotor service reports moving (speed >= 1.1 x SpaceIdleMovementSpeed, the sim's threshold) - ambient
    // moving chatter and the audio-side engine switch; outside hyperspace states. Audible = not hidden / fogged /
    // stealthed for the local player (running loops are muted, never stopped).
    const tactical::abilities::AbilitySystem& ab = battle.abilities();
    const float threshold = battle.engine_moving_threshold();
    if (!super_weapon_types_ready_) {  // AUDIO.md §2.3 (b0286): the factions' Faction_Super_Weapon types
        super_weapon_types_ready_ = true;
        for (const audio::FactionAudio& f : game_data_->factions()) {
            const objects::TypeIndex t = types.resolve_name(f.get("Faction_Super_Weapon"));
            if (t != objects::kNone && std::find(super_weapon_types_.begin(), super_weapon_types_.end(), t) == super_weapon_types_.end())
                super_weapon_types_.push_back(t);
        }
    }
    bool super_weapon = false;
    std::vector<objects::ObjectId> live;
    for (const objects::ObjectId id : mv.unit_ids()) {
        const objects::ObjectHandle h = store.find(id);
        if (!store.alive(h)) continue;
        live.push_back(id);
        // [port] any owner: only the Empire names a Faction_Super_Weapon (Death_Star) in shipped data.
        if (std::find(super_weapon_types_.begin(), super_weapon_types_.end(), store.type(h)) != super_weapon_types_.end())
            super_weapon = true;
        if (spawned_.insert(id).second) {
            game_->object_spawned(static_cast<uint64_t>(id), sounds(types, store.type(h)), store.position(h));
            ++stats_.spawned;
        }
        const bool hidden = ab.is_stealthed(id, battle.frame()) ||
                            (script_hidden && script_hidden(id));  // q6 t19: Lua Hide(true) mutes the object's sound
        const bool fogged = battle.fog_object_hidden(local_player, id, false);
        if (hidden || fogged) inaudible_.insert(static_cast<uint64_t>(id));
        else inaudible_.erase(static_cast<uint64_t>(id));
        if (hidden) hidden_.insert(static_cast<uint64_t>(id));
        else hidden_.erase(static_cast<uint64_t>(id));
        if (battle.hyperspace().departing(id)) continue;  // hyperspace states: the locomotor leaves the loops alone
        const bool moving = mv.view(id).speed >= threshold;
        moving_[id] = moving;
        game_->object_moving(static_cast<uint64_t>(id), moving);
    }
    if (!super_weapon && battle.lead_object() != objects::kNoObjectId) {  // the spawned super weapon (§4.8 lead object)
        const objects::ObjectHandle lh = store.find(battle.lead_object());
        super_weapon = store.alive(lh) && std::find(super_weapon_types_.begin(), super_weapon_types_.end(), store.type(lh)) !=
                                              super_weapon_types_.end();
    }
    super_weapon_report(super_weapon);
    // §3.3 engine-loop requests of the tick (TacticalBattle::engine_loop_requests, hangar::EngineLoops): mode enter,
    // locomotor services, engines offline / online - start = fade-in, stop = fade-out. cu8g t01: HOLSTER_WEAPON / Saber_Throw
    // suppress (0x392410: every engine loop stopped with the request's fade, < 0 = the event's own) / unsuppress (0x393970)
    // and the draw / holster SFX attached to the hero.
    for (const tactical::hangar::EngineLoopRequest& r : battle.engine_loop_requests()) {
        using Op = tactical::hangar::EngineLoopOp;
        const auto id = static_cast<uint64_t>(r.id);
        switch (r.op) {
            case Op::Loop:
                if (r.loop == tactical::hangar::EngineLoop::Idle) game_->set_engine_idle_loop(id, r.start);
                else game_->set_engine_moving_loop(id, r.start);
                break;
            case Op::Suppress: game_->suppress_engine_audio(id, r.fade); break;
            case Op::Unsuppress: game_->clear_engine_audio_suppression(id); break;
            case Op::DrawWeapon:
            case Op::HolsterWeapon: {
                const objects::ObjectHandle h = handle_of(r.id);
                if (!store.alive(h)) break;
                const std::string_view ev =
                    types.get_string(store.type(h), r.op == Op::DrawWeapon ? "SFXEvent_Draw_Weapon" : "SFXEvent_Holster_Weapon");
                if (!ev.empty()) game_->object_sfx(id, ev, store.position(h));
                break;
            }
        }
        ++stats_.engine_loop_requests;
    }
    // HARDPOINT_DISABLED.md §9.1: engines back online (an ENGINE hardpoint repaired): idle on (GameAudio::engines_online).
    for (const objects::ObjectId id : battle.engines_restarted()) {
        game_->engines_online(static_cast<uint64_t>(id));
        ++stats_.engine_restarts;
    }
    std::vector<objects::ObjectId> died;
    for (const tactical::combat::DeathEvent& d : cs.deaths()) {
        const objects::ObjectHandle h = handle_of(d.id);
        if (!store.exists(h)) continue;
        if (game_->unit_death(static_cast<uint64_t>(d.id), sounds(types, store.type(h)).death_start, store.position(h))) ++stats_.deaths;
        moving_.erase(static_cast<objects::ObjectId>(d.id));
        died.push_back(static_cast<objects::ObjectId>(d.id));
        combat = combat || store.owner(h) == local_player || owner_of(d.killer) == local_player;
    }
    // Objects that left without dying (retreat / hyperspace out finished, silent removal): every attached instance
    // killed at once and the object forgotten; dead ones are forgotten too once gone from the battle.
    std::sort(live.begin(), live.end());
    for (auto it = spawned_.begin(); it != spawned_.end();) {
        if (std::binary_search(live.begin(), live.end(), *it)) {
            ++it;
            continue;
        }
        game_->object_removed(static_cast<uint64_t>(*it));
        if (std::find(died.begin(), died.end(), *it) == died.end()) ++stats_.removed;
        moving_.erase(*it);
        inaudible_.erase(static_cast<uint64_t>(*it));
        hidden_.erase(static_cast<uint64_t>(*it));
        it = spawned_.erase(it);
    }
    // §1.2 step 11: the 2D station-upgrade announcement for the local player (own / ally / enemy).
    for (; upgrades_seen_ < battle.station_upgrades().size(); ++upgrades_seen_) {
        const int32_t o = battle.station_upgrades()[upgrades_seen_].owner;
        const char* ev = o == local_player ? "SFXEvent_Starbase_Upgraded"
                         : (o >= 0 && local_player >= 0 && !cs.is_enemy(local_player, o)) ? "SFXEvent_Starbase_Ally_Upgraded"
                                                                                          : "SFXEvent_Starbase_Enemy_Upgraded";
        faction_sfx(ev);
        ++stats_.station_upgrades;
    }
    // L1_S4_REMAINING §4.1 (b0289): a unit that actually entered a garrison this tick -> the local faction's
    // SFXEvent_Bunker_Garrisoned [M: played for the local player's own units only].
    for (const sim::TacticalBattle::StoryGarrisonEvent& e : battle.story_garrisoned()) {
        const std::string_view line = garrison_line(owner_of(e.unit), local_player);
        if (line.empty()) continue;
        faction_sfx(line);
        ++stats_.garrison_lines;
    }
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "Faction.SFXEvent_Bunker_Vacated", garrison eject 0x444dd0): once per unit leaving a
    // garrison, when its owner is the local player: the owner's faction line, attached to the unit (3D at its position).
    if (local_player >= 0 && local_player < static_cast<int32_t>(player_factions_.size()))
        if (const audio::FactionAudio* lf = game_data_->find_faction(player_factions_[static_cast<size_t>(local_player)]))
            for (const sim::TacticalBattle::StoryGarrisonEvent& e : battle.story_vacated()) {
                if (owner_of(e.unit) != local_player) continue;
                // ledger: consumes Faction.SFXEvent_Bunker_Vacated
                const std::string_view ev = lf->get("SFXEvent_Bunker_Vacated");
                if (!ev.empty() && game_->impact(ev, position_of(static_cast<uint64_t>(e.unit)))) ++stats_.garrison_lines;
            }
    // HERO_CLASH §3.8 (b0300): a clash end plays the winner's First_ / Second_Hero_Win_Speech (speech queue).
    {
        const auto& ends = battle.hero_clashes().ended();
        const tactical::combat::HeroClashData* cd = battle.hero_clashes().data();
        if (ends.size() < clash_ends_seen_) clash_ends_seen_ = 0;  // cleared (load / new battle)
        for (; clash_ends_seen_ < ends.size(); ++clash_ends_seen_) {
            const auto& e = ends[clash_ends_seen_];
            const std::string_view line = clash_win_speech(cd, e.entry, e.first_won);
            if (line.empty()) continue;
            speech(line);
            ++stats_.clash_win_speech;
        }
    }
    // ccpgu t01 (SKIRMISH_SETUP 4.2): the control point domination countdown lines.
    {
        const int32_t timer = battle.control_point_timer(), leader = battle.control_point_leader();
        const std::string_view line =
            control_point_line(cp_timer_, timer, leader, local_player, leader >= 0 && local_player >= 0 && cs.is_enemy(local_player, leader));
        cp_timer_ = timer;
        if (!line.empty()) {
            faction_sfx(line);
            ++stats_.control_point_lines;
        }
    }
    for (const tactical::combat::HitEvent& e : cs.hit_events()) {
        if (e.projectile_desc < cs.data().projectiles.size()) {
            const objects::TypeIndex pt = types.resolve_name(cs.data().projectiles[e.projectile_desc].name);
            // tru1 t08 (TRU1_A_RE_ANSWERS B1): the expiry / commanded paths play the detonation sound only when their
            // lifetime particle exists; a ground hit always plays it.
            const bool silent = e.victim == tactical::combat::kNoObject && !e.ground && pt != objects::kNone &&
                                types.get_string(pt, "Projectile_Lifetime_Detonation_Particle").empty();
            if (pt != objects::kNone && !silent) {
                std::string_view ev = e.armor_reduced ? types.get_string(pt, "Projectile_SFXEvent_Detonate_Reduced_By_Armor") : std::string_view();
                if (ev.empty()) ev = sounds(types, pt).projectile_detonate;
                if (!ev.empty() && game_->impact(ev, v3(e.position))) ++stats_.hits;
            }
        }
        combat = combat || e.owner == local_player || (e.victim != tactical::combat::kNoObject && owner_of(e.victim) == local_player);
    }
    // aud1 t04 (AUDIO.md §9.5): a new projectile's fire sound, 3D at the projectile's spawn position (fog test applies):
    // a hardpoint shot plays the hardpoint type's Fire_SFXEvent, else the parent's SFXEvent_Fire; a unit weapon shot the
    // shooter's SFXEvent_Fire. [port] Projectile_Appearance_Delay_Frames is not modelled (the projectile appears at once),
    // so the sound plays with the shot. The hardpoint comes from this tick's ShotEvents by projectile serial.
    uint32_t max_serial = last_serial_;
    const auto shots = cs.shot_events();
    for (size_t i = 0; i < cs.projectile_count(); ++i) {
        const tactical::combat::ProjectileView v = cs.projectile(i);
        if (have_serial_ && v.serial <= last_serial_) continue;
        max_serial = std::max(max_serial, v.serial);
        const objects::ObjectHandle sh = handle_of(v.shooter);
        if (store.exists(sh)) {
            int32_t hp = -1;
            for (const tactical::combat::ShotEvent& e : shots)
                if (e.serial == v.serial && e.shooter == v.shooter) {
                    hp = e.hardpoint;
                    break;
                }
            weapon_fired(static_cast<uint64_t>(v.shooter), hardpoint_fire_sfx(types, cs, hp), sounds(types, store.type(sh)).fire, v3(v.pos));
        }
        combat = combat || v.owner == local_player;
    }
    last_serial_ = max_serial;
    have_serial_ = true;
    tsw_audio(tsw_cues(battle.special_weapon_events(), store), types);  // refix t06
    cue_lines(battle, store, types, local_player);                         // mcx4 t13 batch A
    object_events(battle, store, types, player_factions_);  // aud1 t05: abilities, hyperspace, shields (AUDIO.md §9.6)
    // aud1 t02 (L1_S4_REMAINING §3.2): the land weather's type and intensity after the tick -> the weather audio manager
    // and the faction's SFXEvent_Weather_Begin / _End lines (GameAudio::set_weather; unchanged values do nothing). Weather
    // off (no scenario, space) = CLEAR at 1.0, as retail's 0x323110(0, 1.0).
    weather(weather_name(battle.weather().active() ? static_cast<uint8_t>(battle.weather().type()) : 0),
            battle.weather().active() ? battle.weather().intensity() : 1.0f, local_player < 0 || ab.weather_affected(local_player));
    // aud1 t08 (AUDIO.md §9.8): this tick's hits on the local player's (or a community-property ally's) base objects.
    bool base_hit = false;
    if (local_player >= 0) {
        using namespace objects;
        if (!base_behaviors_ready_) {
            base_behaviors_ready_ = true;
            base_behaviors_[0] = behavior_type_id("DUMMY_GROUND_STRUCTURE");
            base_behaviors_[1] = behavior_type_id("DUMMY_LAND_BASE_LEVEL_COMPONENT");
            base_behaviors_[2] = behavior_type_id("DUMMY_STAR_BASE");
            base_behaviors_[3] = behavior_type_id("DUMMY_ORBITAL_STRUCTURE");
        }
        const bool space = battle.is_space_battle();
        const BehaviorSet set = space ? BehaviorSet::Space : BehaviorSet::Land;
        for (const tactical::combat::DamageEvent& d : cs.damage_events()) {
            if (base_hit) break;
            const ObjectHandle vh = handle_of(d.victim);
            if (!store.exists(vh) || d.source_owner < 0) continue;
            const TypeIndex vt = store.type(vh);
            const int32_t vo = store.owner(vh);
            const BehaviorTypeId a = base_behaviors_[space ? 2 : 0], b = base_behaviors_[space ? 3 : 1];
            // The object's behaviours in this mode = the generic <Behavior> list plus the mode's own list (shipped bases
            // name DUMMY_GROUND_STRUCTURE / DUMMY_STAR_BASE in the generic one).
            const auto has = [&](BehaviorTypeId id) {
                return id != kNoBehavior && (types.has_behavior(vt, set, id) || types.has_behavior(vt, BehaviorSet::Generic, id));
            };
            const bool is_base = has(a) || has(b);
            const bool ally = vo >= 0 && vo != local_player && !cs.is_enemy(local_player, vo);
            base_hit = base_attacked(is_base, cs.is_enemy(d.source_owner, vo), vo, local_player, ally,
                                     ally && types.get_bool(vt, "Is_Community_Property"));
        }
    }
    if (combat || base_hit) this->combat(combat, base_hit);
}

uint32_t GameAudioHost::weapon_fired(uint64_t shooter, std::string_view hardpoint_fire, std::string_view shooter_fire, const Vec3& position) {
    if (!game_) return 0;
    const std::string_view ev = fire_event(hardpoint_fire, shooter_fire);
    const uint32_t id = ev.empty() ? 0u : game_->weapon_fire(shooter, ev, position);
    if (id != 0) ++stats_.fires;
    return id;
}

uint32_t GameAudioHost::animation_sounds(std::span<const AnimSound> list) {
    if (!game_) return 0;
    uint32_t n = 0;
    for (const AnimSound& a : list)
        if (!a.event.empty() && game_->object_sfx(a.object, a.event, a.position) != 0) ++n;
    stats_.anim_sounds += n;
    return n;
}

void GameAudioHost::weather(std::string_view type, float intensity, bool local_affected) {
    if (game_) game_->set_weather(type, intensity, local_affected);
}

void GameAudioHost::combat(bool local_involved, bool local_base_attacked) {
    if (!game_ || (!local_involved && !local_base_attacked)) return;
    audio::CombatEvent ce;
    ce.local_player_involved = local_involved;
    ce.local_base_attacked = local_base_attacked;
    game_->combat(ce);
    ++stats_.combat_notes;
}

void GameAudioHost::cue_lines(const sim::TacticalBattle& battle, const objects::ObjectStore& store, const objects::GameObjectTypes& types,
                              int32_t local_player) {
    namespace tcb = tactical::combat;
    const tcb::CombatSystem& cs = battle.combat();
    const auto type_of = [&](int64_t id) {
        const objects::ObjectHandle h = store.find(static_cast<objects::ObjectId>(id));
        return store.exists(h) ? store.type(h) : objects::kNone;
    };
    const auto owner_of = [&](int64_t id) {
        const objects::ObjectHandle h = store.find(static_cast<objects::ObjectId>(id));
        return store.exists(h) ? store.owner(h) : -1;
    };
    // Health warnings (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Health_Low_Warning family"): one line per hit that
    // crosses a threshold. The tick's damage events are replayed in order from the health before the tick (the health now
    // plus every hull loss of the tick). [port] a killing hit plays none [L]; 2D.
    std::map<tcb::ObjectId, float> lost;  // victim -> hull lost this tick (lookup by id; presentation only)
    for (const tcb::DamageEvent& d : cs.damage_events()) lost[d.victim] += std::max(0.0f, d.hull_lost);
    std::map<tcb::ObjectId, float> running;
    for (const tcb::DamageEvent& d : cs.damage_events()) {
        if (d.hull_lost <= 0.0f || d.killed) continue;
        const float max_h = cs.max_health(d.victim);
        if (!(max_h > 0.0f)) continue;
        auto [it, fresh] = running.try_emplace(d.victim, 0.0f);
        if (fresh) it->second = cs.health(d.victim) + lost[d.victim];
        const float before = it->second, after = before - d.hull_lost;
        it->second = after;
        // The attacker: the source object, or for a projectile its shooter (this tick's hit on the victim).
        int64_t attacker = static_cast<int64_t>(d.source);
        if (d.source_is_projectile)
            for (const tcb::HitEvent& h : cs.hit_events())
                if (h.victim == d.victim && h.shooter != tcb::kNoObject) attacker = static_cast<int64_t>(h.shooter);
        const bool local_victim = local_player >= 0 && owner_of(static_cast<int64_t>(d.victim)) == local_player;
        const bool local_attacker = local_player >= 0 && d.source_owner == local_player;
        const std::string_view tag = health_warning_tag(before / max_h, after / max_h, health_low_, health_critical_, local_victim,
                                                        local_attacker);
        if (tag.empty()) continue;
        const objects::TypeIndex t = local_victim ? type_of(static_cast<int64_t>(d.victim)) : type_of(attacker);
        // ledger: consumes GameObjectType.SFXEvent_Health_Low_Warning
        // ledger: consumes GameObjectType.SFXEvent_Health_Critical_Warning
        const std::string_view ev = t != objects::kNone ? types.get_string(t, tag) : std::string_view();
        // ledger: consumes GameObjectType.SFXEvent_Enemy_Damaged_Health_Low_Warning
        // ledger: consumes GameObjectType.SFXEvent_Enemy_Damaged_Health_Critical_Warning
        if (!ev.empty() && game_->gui_sfx(ev)) ++stats_.health_warnings;
    }
    // Tactical production of the local player (skirmish build queues; MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Tactical_
    // Build_Started family"): an order new since last tick -> Begin; a gone order -> Complete when the type's built count
    // rose, else Cancel. Build_Music_Completed (galactic, mode 4) is not played in a battle [port: as before].
    if (const tactical::hangar::TacticalProduction* prod = battle.production(); prod && local_player >= 0) {
        std::map<uint32_t, int32_t> now;
        for (const tactical::hangar::BuildOrder& o : prod->queue(local_player)) now.emplace(o.id, o.type);
        for (const auto& [id, type] : now)
            if (prod_orders_.find(id) == prod_orders_.end())
                production(types, static_cast<objects::TypeIndex>(type), Production::Begin, true);
        for (const auto& [id, type] : prod_orders_) {
            if (now.find(id) != now.end()) continue;
            const uint32_t built = prod->built_count(local_player, type);
            uint32_t& seen = prod_built_[type];
            const bool done = built > seen;
            if (done) ++seen;
            const objects::TypeIndex t = static_cast<objects::TypeIndex>(type);
            if (done && t < types.size()) {  // completion: the speech / SFX line only (no build music in a battle)
                const std::array<std::string_view, 3> tags = production_tags(Production::Complete, true);
                for (size_t i = 0; i < tags.size(); ++i) {
                    const std::string_view ev = tags[i].empty() ? std::string_view() : types.get_string(t, tags[i]);
                    if (ev.empty()) continue;
                    if (i == 0 ? game_->speech(ev) : game_->gui_sfx(ev)) ++stats_.production_sfx;
                    break;
                }
            } else if (!done) {
                production(types, t, Production::Cancel, true);
            }
        }
        prod_orders_ = std::move(now);
    }
    // Battle won / lost HUD line (MCX4_T13_RE_ANSWERS "Faction.SFXEvent_HUD_Won_Land_Battle family"): once, on the tick the
    // victory is declared. The flags walk the players in index order and stop at the first enemy of the local player
    // owning a TACTICAL_SUPER_WEAPON object, so the own-killer test has only seen lower players by then [H loop, M effect].
    if (battle.victory_declared() && !victory_seen_) {
        victory_seen_ = true;
        const int32_t winner = battle.victory_declared_player();
        if (local_player >= 0 && winner >= 0 && battle_result_gate(battle.victory_declared_type(), battle.victory_declared_silent())) {
            const bool space = battle.is_space();
            const objects::BehaviorSet set = space ? objects::BehaviorSet::Space : objects::BehaviorSet::Land;
            bool enemy_tsw = false, own_killer = false;
            const int32_t players = static_cast<int32_t>(player_factions_.size());
            for (int32_t p = 0; p < players && !enemy_tsw; ++p) {
                const bool enemy = p != local_player && cs.is_enemy(local_player, p);
                if (!enemy && p != local_player) continue;
                for (const objects::ObjectId id : battle.movement().unit_ids()) {  // [port] the battle units
                    const objects::ObjectHandle h = store.find(id);
                    if (!store.alive(h) || store.owner(h) != p) continue;
                    const objects::TypeIndex t = store.type(h);
                    if (enemy && types.has_behavior(t, set, objects::beh::TacticalSuperWeapon)) {
                        enemy_tsw = true;
                        break;
                    }
                    if (!enemy && !hidden_.count(static_cast<uint64_t>(id)) && types.get_bool(t, "Is_Super_Weapon_Killer"))
                        own_killer = true;
                }
            }
            const bool local_won = winner == local_player;
            const bool hostile = !local_won && cs.is_enemy(local_player, winner);
            const std::string_view line = battle_result_line(local_won, hostile, space, enemy_tsw, own_killer, corruption_mission_);
            // ledger: consumes Faction.SFXEvent_HUD_Won_Land_Battle
            // ledger: consumes Faction.SFXEvent_HUD_Lost_Land_Battle
            if (!line.empty()) {
                faction_sfx(line);
                ++stats_.battle_result_lines;
            }
        }
    }
    // Asteroid damage (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Damaged_By_Asteroid", 0x436920): every hit, any owner,
    // the damaged object's type line attached to it (3D at its position).
    for (const objects::ObjectId id : battle.asteroid_hits()) {
        const objects::TypeIndex t = type_of(static_cast<int64_t>(id));
        if (t == objects::kNone) continue;
        // ledger: consumes GameObjectType.SFXEvent_Damaged_By_Asteroid
        const std::string_view ev = types.get_string(t, "SFXEvent_Damaged_By_Asteroid");
        if (!ev.empty() && game_->impact(ev, position_of(static_cast<uint64_t>(id)))) ++stats_.hits;
    }
    // Special weapons (MCX4_T13_RE_ANSWERS "Faction.SFXEvent_HUD_Special_Weapon_Ready family"): ready 0x492010 (only while
    // SpecialWeaponsAllowed holds for the owner; an own / friendly weapon's type SFXEvent_Special_Weapon_Ready replaces
    // the faction line) and firing 0x491cd0; the local faction's list entry [Special_Weapon_Target_Action_Index]; 2D.
    if (local_player >= 0 && local_player < static_cast<int32_t>(player_factions_.size()))
        if (const audio::FactionAudio* lf = game_data_->find_faction(player_factions_[static_cast<size_t>(local_player)]))
            for (const tactical::abilities::SpecialWeaponEvent& e : battle.special_weapon_events()) {
                const bool fired = e.kind == tactical::abilities::SwEventKind::Fired;
                if (!fired && e.kind != tactical::abilities::SwEventKind::Ready) continue;
                const int32_t o = owner_of(static_cast<int64_t>(e.id));
                const objects::TypeIndex t = type_of(static_cast<int64_t>(e.id));
                if (o < 0 || t == objects::kNone) continue;
                if (!fired && !battle.special_weapons_allowed(o)) continue;
                const bool own = o == local_player || !cs.is_enemy(local_player, o);
                std::string_view ev;
                // ledger: consumes GameObjectType.SFXEvent_Special_Weapon_Ready
                if (!fired && own) ev = types.get_string(t, "SFXEvent_Special_Weapon_Ready");
                if (ev.empty())
                    ev = nth_field(lf->fields, special_weapon_tag(fired, own), types.get_int(t, "Special_Weapon_Target_Action_Index"));
                if (!ev.empty() && game_->gui_sfx(ev)) ++stats_.special_weapon_lines;
            }
    // Tactical pop cap (P4_SIM_SYSTEMS §2.7.3, MCX4_T13_RE_ANSWERS "Faction.SFXEvent_Tactical_Unit_Cap_Reached family"):
    // once each time the local player's room for one more unit turns false; the local faction's line, 2D.
    if (local_player >= 0) {
        const bool room = unit_room(battle.population_used(local_player), battle.population_cap(local_player));
        if (unit_room_ && !room) {
            faction_sfx("SFXEvent_Tactical_Pop_Cap_Reached");  // ledger: consumes Faction.SFXEvent_Tactical_Pop_Cap_Reached
            ++stats_.pop_cap_lines;
        }
        unit_room_ = room;
    }
    // Base shields (MCX4_T13_RE_ANSWERS "Faction.SFXEvent_HUD_Base_Shield_Online family"; TRU1_A_RE_ANSWERS Set_State
    // 0x56b540 step 2): a generator's Set_State that changed its on / off state plays the local faction's line, 2D. The
    // previous state is the host's (the first report is the 0 -> 1 / 0 -> 2 evaluation: silent).
    for (const tcb::CombatSystem::BaseShieldSetState& e : cs.base_shield_set_states()) {
        uint8_t& prev = base_shield_state_[static_cast<int64_t>(e.id)];
        const uint8_t was = prev, now = e.on ? 1 : 2;
        prev = now;
        if (local_player < 0 || was == 0 || was == now) continue;
        const int32_t o = owner_of(static_cast<int64_t>(e.id));
        if (o < 0) continue;
        faction_sfx(base_shield_line(now == 1, o == local_player || !cs.is_enemy(local_player, o)));
        ++stats_.base_shield_lines;
    }
    // Retreat begin (MCX4_T13_RE_ANSWERS "Faction.Land_Retreat_Begin_SFXEvent family"): on the tick a retreat countdown
    // starts; 2D. A local retreater first tries its retreating units' SFXEvent_Retreat_Start ([port] the retreater's
    // battle units in id order stand for the coordinator's list). Cancel / not-allowed lines: no port path yet.
    if (const int32_t rp = battle.retreating_player(); rp != last_retreater_) {
        last_retreater_ = rp;
        if (rp >= 0 && local_player >= 0) {
            const bool local = rp == local_player;
            bool played = false;
            if (local)
                for (const objects::ObjectId id : battle.movement().unit_ids()) {
                    const objects::ObjectHandle h = store.find(id);
                    if (!store.alive(h) || store.owner(h) != rp) continue;
                    const std::string_view ev = types.get_string(store.type(h), "SFXEvent_Retreat_Start");
                    if (ev.empty()) continue;
                    played = game_->gui_sfx(ev) != 0;
                    break;
                }
            const std::string_view line = retreat_begin_tag(local, !local && cs.is_enemy(local_player, rp), battle.is_space());
            if (!played && !line.empty()) {
                faction_sfx(line);
                played = true;
            }
            if (played) ++stats_.retreat_lines;
        }
    }
    // Capture point owner changes (MCX4_T13_RE_ANSWERS "Faction.SFXEvent_Tactical_Gain_Friendly_Control family"): the
    // local faction's lists, first entry whose object type is the point's type; 2D. [port] the faction's Is_Neutral is
    // read from its Factions.xml entry.
    if (local_player >= 0 && local_player < static_cast<int32_t>(player_factions_.size())) {
        const audio::FactionAudio* lf = game_data_->find_faction(player_factions_[static_cast<size_t>(local_player)]);
        const auto neutral = [&](int32_t p) {
            if (p < 0 || p >= static_cast<int32_t>(player_factions_.size())) return true;  // no owner: as neutral
            const audio::FactionAudio* f = game_data_->find_faction(player_factions_[static_cast<size_t>(p)]);
            return f && data::parse_bool_or(f->get("Is_Neutral"), false);
        };
        const auto rel_friendly = [&](int32_t p) { return p >= 0 && (p == local_player || !cs.is_enemy(local_player, p)); };
        const auto rel_hostile = [&](int32_t p) { return p >= 0 && p != local_player && cs.is_enemy(local_player, p); };
        for (const sim::TacticalBattle::StoryCaptureEvent& e : battle.story_captured()) {
            if (!lf) break;
            const objects::TypeIndex t = type_of(static_cast<int64_t>(e.object));
            if (t == objects::kNone) continue;
            const ControlLines cl = control_lines(rel_friendly(e.old_owner), rel_friendly(e.new_owner), rel_hostile(e.old_owner),
                                                  rel_hostile(e.new_owner), neutral(e.old_owner), neutral(e.new_owner));
            // ledger: consumes Faction.SFXEvent_Tactical_Gain_Friendly_Control
            // ledger: consumes Faction.SFXEvent_Tactical_Lose_Friendly_Control
            for (const std::string_view tag : {cl.friendly, cl.enemy}) {
                // ledger: consumes Faction.SFXEvent_Tactical_Gain_Enemy_Control
                // ledger: consumes Faction.SFXEvent_Tactical_Lose_Enemy_Control
                if (tag.empty()) continue;
                const std::vector<std::string> ev = lf->pair_values(tag, types.raw().types().name[t]);
                if (!ev.empty() && !ev.front().empty() && game_->gui_sfx(ev.front())) ++stats_.control_lines;
            }
        }
    }
    // Hard point losses (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Hardpoint_Destroyed family", 0x386660).
    for (const tcb::HardpointEvent& e : cs.hardpoint_events()) {
        const objects::TypeIndex pt = type_of(static_cast<int64_t>(e.owner));
        if (pt == objects::kNone) continue;
        const auto kind = static_cast<uint8_t>(e.kind);
        bool weapons_left = false;
        for (uint8_t k = 5; k <= 10; ++k) weapons_left = weapons_left || cs.live_hardpoints(e.owner, static_cast<tcb::HardpointKind>(k)) > 0;
        const std::string_view tag =
            hardpoint_lost_tag(kind, local_player >= 0 && owner_of(static_cast<int64_t>(e.owner)) == local_player, e.owner_dying,
                               e.last_of_kind, weapons_left);
        // ledger: consumes GameObjectType.SFXEvent_Hardpoint_All_Weapons_Destroyed
        // ledger: consumes GameObjectType.SFXEvent_Hardpoint_Destroyed
        const std::string_view line = tag.empty() ? std::string_view()
                                      : tag == "SFXEvent_Hardpoint_Destroyed" ? hardpoint_kind_event(types.get_list(pt, tag), kind)
                                                                              : types.get_string(pt, tag);
        if (!line.empty() && game_->gui_sfx(line)) ++stats_.hardpoint_lines;
        // The explosion: the hard point type's Death_Explosion_SFXEvent, 3D at the hard point, any owner; unset -> the
        // Death_Breakoff_Prop type's Death_SFXEvent_Start_Die. The hard point type = the parent's HardPoints entry at the
        // event's index [M].
        const std::span<const std::string_view> hps = types.get_list(pt, "HardPoints");
        const objects::TypeIndex ht = e.index < hps.size() ? types.resolve_name(hps[e.index]) : objects::kNone;
        // ledger: consumes HardPoint.Death_Explosion_SFXEvent
        std::string_view boom = ht != objects::kNone ? types.get_string(ht, "Death_Explosion_SFXEvent") : std::string_view();
        if (boom.empty() && e.breakoff_type >= 0 && static_cast<size_t>(e.breakoff_type) < types.size())
            boom = sounds(types, static_cast<objects::TypeIndex>(e.breakoff_type)).death_start;
        if (boom.empty()) continue;
        tactical::Vec3 hp_pos{};
        const Vec3 at = cs.hardpoint_world(e.owner, static_cast<int32_t>(e.index), hp_pos) ? v3(hp_pos)
                                                                                           : position_of(static_cast<uint64_t>(e.owner));
        if (game_->impact(boom, at)) ++stats_.hardpoint_explosions;
    }
}

const std::string& GameAudioHost::hardpoint_fire_sfx(const objects::GameObjectTypes& types, const tactical::combat::CombatSystem& cs,
                                                     int32_t hardpoint) {
    static const std::string kNone;
    if (hardpoint < 0 || static_cast<size_t>(hardpoint) >= cs.data().hardpoints.size()) return kNone;
    auto it = hardpoint_fire_.find(hardpoint);
    if (it == hardpoint_fire_.end()) {
        const objects::TypeIndex t = types.resolve_name(cs.data().hardpoints[static_cast<size_t>(hardpoint)].name);
        it = hardpoint_fire_.emplace(hardpoint, t != objects::kNone ? std::string(types.get_string(t, "Fire_SFXEvent")) : std::string()).first;
    }
    return it->second;
}

void GameAudioHost::super_weapon_report(bool present) {
    const bool first = !battle_reported_;
    battle_reported_ = true;
    if (game_) game_->set_super_weapon_present(present, first);
}

void GameAudioHost::tsw_audio(std::span<const TswCue> cues, const objects::GameObjectTypes& types) {
    if (!game_) return;
    for (const TswCue& c : cues) {
        if (c.type == objects::kNone || c.type >= types.size()) continue;
        const uint64_t id = static_cast<uint64_t>(c.object);
        const Vec3 p = v3(c.position);
        switch (c.kind) {
            case TswCue::Kind::Firing:
                game_->object_sfx(id, types.get_string(c.type, "TSW_SFXEvent_Activate_Voice"), p);
                game_->object_sfx(id, types.get_string(c.type, "TSW_SFXEvent_Weapon_Power_Up"), p);
                game_->super_weapon_activation(types.get_string(c.type, "TSW_MusicEvent_Activation_Start"));
                ++stats_.tsw_activations;
                break;
            case TswCue::Kind::AttackAnim:
                game_->weapon_fire(id, sounds(types, c.type).fire, p);
                ++stats_.tsw_fires;
                break;
            case TswCue::Kind::PlanetDestroyed:
                game_->unit_death(id, sounds(types, c.type).death_start, p);
                ++stats_.tsw_planet_deaths;
                break;
            case TswCue::Kind::LobChargeup:  // S8 t31
                game_->object_sfx(id, types.get_string(c.type, "Lobbing_Superweapon_SFXEvent_Chargeup"), p);
                ++stats_.lob_chargeups;
                break;
            case TswCue::Kind::LobCharged:
                game_->object_sfx(id, types.get_string(c.type, "Lobbing_Superweapon_SFXEvent_Fire"), p);
                ++stats_.lob_charged;
                break;
        }
    }
}

void GameAudioHost::sim_tick() {
    if (game_) game_->sim_tick();
}

void GameAudioHost::music(MusicScene scene, std::string_view faction, bool space, std::string_view environment) {
    if (!game_) return;
    if (!faction.empty()) game_->set_local_faction(faction);
    if (scene == scene_) return;
    const MusicScene before = scene_;
    scene_ = scene;
    const bool pending = pending_played_;
    pending_played_ = false;
    ++stats_.scenes;
    if (before == MusicScene::Battle || before == MusicScene::Galactic) game_->leave_mode();
    switch (scene) {
        case MusicScene::None: game_->leave_mode(); break;
        case MusicScene::MainMenu: game_->enter_front_end(); break;
        case MusicScene::Galactic: game_->enter_galactic(); break;
        case MusicScene::LoadScreen:
            if (!pending) game_->battle_pending(!space);  // the prompt played it already
            game_->battle_load_screen(true);
            break;
        case MusicScene::Battle: {
            game_->battle_load_screen(false);
            audio::TacticalSetup ts;
            ts.land = !space;
            ts.environment = std::string(environment);
            game_->enter_tactical(ts);
            break;
        }
    }
}

void GameAudioHost::set_listener(const Vec3& eye, const Vec3& look_at, bool cinematic) {
    if (!game_) return;
    Vec3 f{look_at.x - eye.x, look_at.y - eye.y, look_at.z - eye.z};
    const float len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = len > 0.0f ? Vec3{f.x / len, f.y / len, f.z / len} : Vec3{0.0f, 1.0f, 0.0f};
    audio::CameraState c;
    c.position = eye;
    c.look_at = look_at;
    c.forward = f;
    c.up = {0.0f, 0.0f, 1.0f};
    c.cinematic = cinematic;
    game_->set_camera(c);
}

// --- OI host: audio game-state flags and the device latency ---
void GameAudioHost::set_flags(bool modal, bool gui_only, bool attract) {
    if (modal == modal_ && gui_only == gui_only_ && attract == attract_) return;
    if (attract && !attract_) ++stats_.attract_starts;
    const bool attract_changed = attract != attract_;
    modal_ = modal;
    gui_only_ = gui_only;
    attract_ = attract;
    ++stats_.flag_changes;
    if (!system_) return;
    system_->set_modal_dialog(modal);
    system_->set_gui_only(gui_only);
    system_->set_attract_mode(attract);
    // STRATEGIC_GAPS §7.4: the music lock is set when the attract loop starts and cleared by the front-end action that
    // ends it [port: the attract demo ending]; while set, unforced front-end (mode 13) music is refused.
    if (attract_changed) system_->set_music_lock(attract);
}

double GameAudioHost::output_latency_seconds() const { return backend_ ? backend_->output_latency_seconds() : 0.0; }
// --- end OI host ---

void GameAudioHost::set_volumes(float master, float sfx, float speech, float music) {
    if (!system_) return;
    system_->set_bus_level(audio::AudioBus::Master, master);
    system_->save_master_volume();  // aud2 t02: the profile's master is what Lua Master_Volume_Restore restores
    system_->set_bus_level(audio::AudioBus::Sfx, sfx);
    system_->set_bus_level(audio::AudioBus::Speech, speech);
    system_->set_bus_level(audio::AudioBus::Music, music);
}

void GameAudioHost::update(double dt) {
    if (game_ && app_active_) game_->update(dt);  // aud2 t04: step 14 runs only while the application is active
}

void GameAudioHost::planet_revealed() {
    if (!game_ || !db_) return;
    const std::string& event = db_->constants().sfx_planet_revealed;
    if (event.empty()) return;
    ++stats_.planet_reveals;
    ui(event);
}

void GameAudioHost::set_app_active(bool active) {
    if (active == app_active_) return;
    app_active_ = active;
    if (!active) ++stats_.deactivations;
    if (!system_) return;
    if (active) system_->resume_music();
    else system_->pause_music();
}

uint32_t GameAudioHost::production(const objects::GameObjectTypes& types, objects::TypeIndex type, Production kind, bool tactical) {
    if (!game_ || type == objects::kNone || type >= types.size()) return 0;
    uint32_t id = 0;
    const std::array<std::string_view, 3> tags = production_tags(kind, tactical);
    // ledger: consumes GameObjectType.SFXEvent_Tactical_Build_Started
    // ledger: consumes GameObjectType.SFXEvent_Tactical_Build_Complete
    for (size_t i = 0; i < tags.size(); ++i) {
        // ledger: consumes GameObjectType.SFXEvent_Tactical_Build_Cancelled
        const std::string_view ev = tags[i].empty() ? std::string_view() : types.get_string(type, tags[i]);
        if (ev.empty()) continue;
        id = i == 0 ? game_->speech(ev) : game_->gui_sfx(ev);  // the Build_Speech_* line is speech (queued)
        if (id) ++stats_.production_sfx;
        break;
    }
    if (kind == Production::Complete) {
        const std::string_view music = types.get_string(type, "Build_Music_Completed");
        if (!music.empty()) {
            game_->build_complete(music);  // mode 4, the previous music resumes after it
            ++stats_.build_music;
        }
    }
    return id;
}

uint32_t GameAudioHost::fleet_departure(bool local_owner, bool local_sees) {
    if (!game_ || !(local_owner || local_sees)) return 0;
    const uint32_t id = game_->gui_sfx(kFleetHyperspaceEvent);
    if (id) ++stats_.fleet_jumps;
    return id;
}

void GameAudioHost::campaign_end(bool won, std::string_view enemy_faction) {
    if (!game_) return;
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "Faction.Music_Event_Strategic_Win family" [H]): retail never reads the faction's
    // Music_Event_Strategic_Win / _Lose; the end dialogs play Audio.xml Music_Event_Battle_End_Summary_Screen_<Win/Lose>
    // in music mode 16 (0x87770 / 0x88050). The winner's faction no longer picks a row.
    (void)enemy_faction;
    game_->battle_summary_screen(won);
    ++stats_.campaign_end_music;
}

void GameAudioHost::set_log_limit(size_t limit) {
    if (system_) system_->set_log_limit(limit);
}

void GameAudioHost::apply_presentation(const std::vector<lua::PresentationEvent>& events, uint32_t* next_seq) {
#ifdef ALAMO_AUDIO_HAVE_LUA
    if (game_ && next_seq)
        audio::apply_presentation_audio(*game_, events, next_seq, [this](int32_t id) -> std::optional<Vec3> {
            const auto it = positions_.find(static_cast<uint64_t>(id));
            if (it == positions_.end()) return std::nullopt;
            return it->second;
        });
#else
    (void)events;
    (void)next_seq;
#endif
}

#else  // no engine/audio: inactive host

bool GameAudioHost::init(const GameData&, AudioBackendKind kind, uint32_t seed, std::string* err) {
    rng_.seed(seed);
    if (kind != AudioBackendKind::Off && err) *err = "built without engine/audio";
    return kind == AudioBackendKind::Off;
}
const audio::UnitSounds& GameAudioHost::sounds(const objects::GameObjectTypes&, objects::TypeIndex) {
    static const audio::UnitSounds none;
    return none;
}
void GameAudioHost::ui(std::string_view) {}
void GameAudioHost::speech(std::string_view) {}
void GameAudioHost::faction_sfx(std::string_view) {}
void GameAudioHost::negative_feedback() {}
void GameAudioHost::battle_pending(bool) { pending_played_ = true; }  // same bookkeeping as with audio
void GameAudioHost::galactic_resume() { pending_played_ = false; }
void GameAudioHost::movie_started(bool movie_music, bool halt_music) {  // same bookkeeping as with audio (no music here)
    movie_halted_ = halt_music;
    movie_music_on_ = movie_music && !halt_music;
}
void GameAudioHost::movie_cue(std::string_view) {}
void GameAudioHost::movie_ended() { movie_music_on_ = movie_halted_ = false; }
void GameAudioHost::unit_response(const objects::GameObjectTypes&, objects::TypeIndex, uint64_t, UnitResponse, bool, int32_t, uint32_t) {}
void GameAudioHost::battle_tick(const sim::TacticalBattle&, const objects::ObjectStore&, const objects::GameObjectTypes&, int32_t,
                                const std::function<bool(objects::ObjectId)>&) {}
void GameAudioHost::battle_end(bool, std::string_view) {}
void GameAudioHost::tsw_audio(std::span<const TswCue>, const objects::GameObjectTypes&) {}
void GameAudioHost::sim_tick() {}
uint32_t GameAudioHost::animation_sounds(std::span<const AnimSound>) { return 0; }
uint32_t GameAudioHost::weapon_fired(uint64_t, std::string_view, std::string_view, const Vec3&) { return 0; }
void GameAudioHost::weather(std::string_view, float, bool) {}
void GameAudioHost::combat(bool, bool) {}
void GameAudioHost::super_weapon_report(bool) { battle_reported_ = true; }
void GameAudioHost::music(MusicScene scene, std::string_view, bool, std::string_view) {
    if (scene != scene_) pending_played_ = false;
    scene_ = scene;
}
void GameAudioHost::set_listener(const Vec3&, const Vec3&, bool) {}
void GameAudioHost::set_flags(bool modal, bool gui_only, bool attract) {  // same bookkeeping as with audio
    if (modal == modal_ && gui_only == gui_only_ && attract == attract_) return;
    if (attract && !attract_) ++stats_.attract_starts;
    modal_ = modal;
    gui_only_ = gui_only;
    attract_ = attract;
    ++stats_.flag_changes;
}
double GameAudioHost::output_latency_seconds() const { return 0.0; }
void GameAudioHost::set_volumes(float, float, float, float) {}
void GameAudioHost::update(double) {}
void GameAudioHost::planet_revealed() {}
void GameAudioHost::set_app_active(bool active) {  // same bookkeeping as with audio
    if (active == app_active_) return;
    app_active_ = active;
    if (!active) ++stats_.deactivations;
}
uint32_t GameAudioHost::production(const objects::GameObjectTypes&, objects::TypeIndex, Production, bool) { return 0; }
uint32_t GameAudioHost::fleet_departure(bool, bool) { return 0; }
void GameAudioHost::campaign_end(bool, std::string_view) {}
void GameAudioHost::set_log_limit(size_t) {}
void GameAudioHost::apply_presentation(const std::vector<lua::PresentationEvent>&, uint32_t*) {}

#endif

std::array<std::string_view, 3> GameAudioHost::production_tags(Production kind, bool tactical) {
    switch (kind) {
        case Production::Begin:
            return {"Build_Speech_Underway", tactical ? "SFXEvent_Tactical_Build_Started" : "", "SFXEvent_Build_Started"};
        case Production::Cancel:
            return {"Build_Speech_Stopped", tactical ? "SFXEvent_Tactical_Build_Cancelled" : "", "SFXEvent_Build_Cancelled"};
        case Production::Complete:
            return {"Build_Speech_Completed", tactical ? "SFXEvent_Tactical_Build_Complete" : "", "SFXEvent_Build_Complete"};
    }
    return {};
}

std::string_view GameAudioHost::production_tag(Production kind) {
    switch (kind) {
        case Production::Begin: return "SFXEvent_Build_Started";
        case Production::Cancel: return "SFXEvent_Build_Cancelled";
        case Production::Complete: return "SFXEvent_Build_Complete";
    }
    return {};
}

// Shared by both builds (cu9 t09): the same bookkeeping with and without engine/audio (nothing reads it without).
void GameAudioHost::battle_reset() {
    last_serial_ = 0;
    hardpoint_fire_.clear();  // aud1 t04: hardpoint indices are per battle
    prod_orders_.clear();     // mcx4 t13: per-battle production tracking
    victory_seen_ = false;    // mcx4 t13 batch D
    last_retreater_ = -1;     // mcx4 t13 batch E
    base_shield_state_.clear();  // mcx4 t13 batch I
    unit_room_ = true;           // mcx4 t13 batch J
    prod_built_.clear();
    intro_decided_ = false;   // aud1 t07: a new battle decides its intro again
    object_events_reset();    // aud1 t05
    intro_frame_ = 0;
    have_serial_ = false;
    spawned_.clear();
    moving_.clear();
    inaudible_.clear();
    hidden_.clear();
    battle_reported_ = false;
    clash_ends_seen_ = 0;
    upgrades_seen_ = 0;
    cp_timer_ = -1;
    retreating_player_ = -1;
}

void GameAudioHost::set_object_positions(std::vector<std::pair<uint64_t, Vec3>> positions) {
    positions_.clear();
    for (const auto& [id, p] : positions) positions_[id] = p;
}

std::vector<std::string> GameAudioHost::take_speech_done() {
    std::vector<std::string> v;
    v.swap(speech_done_);
    return v;
}

GameAudioHost::GameAudioHost() = default;
GameAudioHost::~GameAudioHost() {
    game_.reset();    // GameAudio references the system, the data and the RNG
    system_.reset();  // before the backend / database / samples it references
    backend_.reset();
}

}  // namespace alamo::game
