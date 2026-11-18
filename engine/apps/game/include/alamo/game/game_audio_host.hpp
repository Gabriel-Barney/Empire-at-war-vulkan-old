// P host / P3 hookup: drives engine/audio (AudioSystem + the game-host layer audio::GameAudio) from the game.
// Presentation only: it reads the sim between ticks through const references, never writes it, and every random
// draw is on its own FREE (unsynced) SimRng - so the sim's per-tick CRC is the same with audio on or off.
//   UI            GUI / HUD SFX hooks -> GameAudio::gui_sfx.
//   unit response the local player's select / move / attack / guard / stop order -> GameAudio::unit_response on the
//                 representative unit = the lowest id of the order's units [port; AUDIO.md 3.3 M]; played before the
//                 command is submitted (local only).
//   battle events after every battle tick, in a stable order: new battle units -> object_spawned (engine / ambient
//                 loops) and object_moving from the movement state (ascending id); deaths -> unit_death
//                 (Death_SFXEvent_Start_Die); projectile hits -> impact (Projectile_SFXEvent_Detonate); new projectiles
//                 -> weapon_fire (shooter's SFXEvent_Fire [port: the hardpoint's Fire_SFXEvent is not in the battle
//                 catalog]); any of them involving the local player -> combat (battle music, mode 7); the tactical super
//                 weapon's cues (battle_fx.hpp tsw_cues -> tsw_audio).
//   modes         front end / galactic / battle pending + load screen / tactical (enter_tactical) / leave_mode.
//   listener      set_camera from the active camera (AUDIO.md 1.8).
// Unit sounds come from the typed object view (GameObjectTypes::get_string), cached per type.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "alamo/core/sim_rng.hpp"
#include "alamo/game/battle_fx.hpp"
#include "alamo/game/game_data.hpp"
#include "alamo/objects/object_store.hpp"
#include "alamo/tactical/abilities/ability_system.hpp"  // aud1 t10: AbilityActivation

namespace alamo::audio {
class AudioDatabase;
class AudioSystem;
class VoiceBackend;
class SampleSource;
class GameAudioData;
class GameAudio;
struct UnitSounds;
}  // namespace alamo::audio
namespace alamo::sim {
class TacticalBattle;
}
namespace alamo::tactical::combat {
struct HeroClashData;
class CombatSystem;
}
namespace alamo::lua {
struct PresentationEvent;
}

namespace alamo::game {

enum class AudioBackendKind : uint8_t { Device = 0, Null = 1, Off = 2 };
enum class UnitResponse : uint8_t { Select, Move, FleetMove, Attack, Guard, Stop };
enum class MusicScene : uint8_t { None, MainMenu, Galactic, LoadScreen, Battle };

struct GameAudioStats {
    uint64_t ui = 0, responses = 0, deaths = 0, hits = 0, fires = 0, combat_notes = 0, spawned = 0, scenes = 0, speech = 0, engine_restarts = 0;
    uint64_t feedback = 0;  // P4 host: faction_sfx / negative_feedback calls
    uint64_t flag_changes = 0, attract_starts = 0;  // OI host: set_flags changes / attract demo starts
    uint64_t excluded = 0, battle_end_music = 0;    // responses skipped for retreating units / battle_end calls
    uint64_t battle_pending = 0;                    // GALACTIC_DIALOGS §1.3 battle prompt sound + music
    uint64_t engine_loop_requests = 0, removed = 0, station_upgrades = 0;  // STATION_HANGARS_SOUND §1.2 / §3
    uint64_t movie_music = 0, movie_halts = 0, movie_cues = 0, movie_restores = 0;  // movie music modes 14 / 17 (STRATEGIC_GAPS §7.4)
    uint64_t tsw_activations = 0, tsw_fires = 0, tsw_planet_deaths = 0;  // refix t06: tsw_audio cues played
    uint64_t lob_chargeups = 0, lob_charged = 0;                         // S8 t31: lobbing charge-up / charged cues
    uint64_t control_point_lines = 0;                                    // ccpgu t01: control point countdown lines
    uint64_t garrison_lines = 0, clash_win_speech = 0;                   // aud4: SFXEvent_Bunker_Garrisoned / clash win speech
    uint64_t anim_sounds = 0;                                           // aud1 t03: AnimationSFXMaps SOUND rows started
    uint64_t planet_reveals = 0, production_sfx = 0, build_music = 0;    // aud2 t01 / t03
    uint64_t fleet_jumps = 0, campaign_end_music = 0, deactivations = 0;  // aud2 t07 / t08 / t04
    uint64_t health_warnings = 0, hardpoint_lines = 0, hardpoint_explosions = 0;  // mcx4 t13 batch A
    uint64_t control_lines = 0;                                                    // mcx4 t13 batch C
    uint64_t battle_result_lines = 0;                                              // mcx4 t13 batch D
    uint64_t retreat_lines = 0;                                                    // mcx4 t13 batch E
    uint64_t special_weapon_lines = 0;                                             // mcx4 t13 batch G
    uint64_t base_shield_lines = 0;                                                // mcx4 t13 batch I
    uint64_t pop_cap_lines = 0;                                                    // mcx4 t13 batch J
    uint64_t gravity_lines = 0;                                                    // mcx4 t13 batch K
    uint64_t strike_lines = 0;                                                     // mcx4 t13 batch M
    uint64_t sighting_lines = 0;                                                   // mcx4 t13 batch N
    uint64_t land_damage_sfx = 0;                                                  // mcx4 t13 batch O
    uint64_t pad_lines = 0;                                                        // mcx4 t13 batch P
    uint64_t ability_lines = 0;                                                    // mcx4 t13 batch R
};

class GameAudioHost {
public:
    GameAudioHost();
    ~GameAudioHost();
    GameAudioHost(const GameAudioHost&) = delete;
    GameAudioHost& operator=(const GameAudioHost&) = delete;

    // Loads the audio databases through data.files; the device / null backend; `seed` seeds the free RNG.
    // Off (or no engine/audio) -> inactive (every call is a no-op). False + err when a requested backend failed.
    bool init(const GameData& data, AudioBackendKind backend, uint32_t seed, std::string* err = nullptr);
    bool active() const { return game_ != nullptr; }
    audio::AudioSystem* system() { return system_.get(); }  // movie streams (null when inactive)
    // [port] Bounds the audio event log (audio::AudioConfig::log_limit); no-op when inactive. The live game calls it with
    // kGameAudioLogLimit after init; tests leave the log unlimited.
    void set_log_limit(size_t limit);

    void ui(std::string_view event);
    void speech(std::string_view event);  // a queued speech event (story dialog scripts' DIALOG lines)
    // L1/S4 wave 1: speech events that ended since the last call (AudioSystem::on_speech_done, finished or killed by
    // Stop_All_Speech / a mode change - AUDIO.md §3.1 reports both to the story), in end order. Empty when inactive.
    std::vector<std::string> take_speech_done();
    // P4 host: the local faction's announcement by its Faction tag (GameAudio::faction_sfx, e.g.
    // SFXEvent_Sabotage_Success) and the GUI negative feedback (the special systems' local error SFX, [port]).
    void faction_sfx(std::string_view faction_tag);
    void negative_feedback();
    // owner = the unit's player (-1 unknown): units of the battle's retreating player give no response (the mode's
    // "excluded set" = the retreat coordinator's retreating units, STRATEGIC_GAPS §7.4 [M]; stats().excluded).
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Move_Into_Asteroid_Field family" [H]): `dest_fields` = the
    // space field kinds (tactical::abilities kField* bits) at a local unit's space move destination; the type's
    // move_into_field_tag line, when set, replaces the move response.
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Attack_Hardpoint", 0x44590 / 0x44cd0 [H]): `hardpoint_kind` =
    // the HardPointType of an attack order's targeted hard point (-1 none); the type's SFXEvent_Attack_Hardpoint entry
    // for it ("HARD_POINT_*, Event" lines), when set, replaces the attack response.
    void unit_response(const objects::GameObjectTypes& types, objects::TypeIndex type, uint64_t object, UnitResponse kind,
                       bool group = false, int32_t owner = -1, uint32_t dest_fields = 0, int32_t hardpoint_kind = -1);
    // Asteroid field (bit 1) first, then nebula (bit 4); "" for neither.
    static std::string_view move_into_field_tag(uint32_t dest_fields);
    // Tactical win / lose music (GameAudio::tactical_result_music, modes 9 / 10): `won` = the local player is the
    // winner; `other_faction` = the loser's faction on a win, the winner's on a loss. battle_tick calls it at the victory
    // declare; the battle-end dialogs call it again (a no-op while the mode is unchanged).
    void battle_end(bool won, std::string_view other_faction = {});
    // mcx4 t13 (0x343b90 -> 0x28f250 [H]): the declare's music call. loser = the other of attacker / defender relative
    // to the winner; play = a local player and a loser exist; won = local is the winner; other = the loser on a win, the
    // winner otherwise (an ally of the winner gets the lose music).
    struct ResultMusic {
        bool play = false, won = false;
        int32_t other = -1;
    };
    static ResultMusic result_music(int32_t local, int32_t winner, int32_t attacker, int32_t defender);
    // GALACTIC_DIALOGS §1.3: the battle prompt opened - the local faction's SFX_Event_Tactical_{Space,Land}_Battle_Pending
    // and the pending-battle music (GameAudio::battle_pending, mode 5); the load screen after Begin does not repeat them.
    // galactic_resume: back to the galactic mode music after an auto-resolve's battle-end screen (the scene stayed Galactic).
    void battle_pending(bool land);
    void galactic_resume();
    // Movie music (STRATEGIC_GAPS §7.3 / §7.4), called by MovieSlot for the movie's Movies.xml flags: movie_started with the
    // movie-music flag (Has_Credits, +0xD0) starts mode 14 music (kCreditsMusicEvent [M for the name]; one pass, then the
    // previous music resumes), with Halt_Game_Music (+0xD4) stops the game's music; movie_cue = a MusicEvent_Frame cue
    // (mode 17); movie_ended = the movie stopped (finished, skipped, replaced): its music stops now and the music it
    // replaced restarts, a halted movie's silence ends with the mode-based music [port]. Movies without those flags
    // (the logo / intro movies, the advisor loops) never reach here and leave the music alone.
    static constexpr const char* kCreditsMusicEvent = "Game_Credits_Music_Event";
    void movie_started(bool movie_music, bool halt_music);
    void movie_cue(std::string_view music_event);
    void movie_ended();
    // After every tick of a running battle (const reads). q6 t19 (OBJECT_SCRIPTS §10.3): `script_hidden` (optional,
    // GameSession::script_hidden) mutes Lua-hidden objects like fogged / stealthed ones (loops muted, never stopped).
    void battle_tick(const sim::TacticalBattle& battle, const objects::ObjectStore& store, const objects::GameObjectTypes& types,
                     int32_t local_player, const std::function<bool(objects::ObjectId)>& script_hidden = {});
    void battle_reset();
    // refix t06 (TSW_PLANET_DESTROY §3 / §4 step 5; battle_fx.hpp): the tactical super weapon's sounds, in cue order.
    // Firing: the TSW type's TSW_SFXEvent_Activate_Voice and TSW_SFXEvent_Weapon_Power_Up attached to the TSW (2D or 3D
    // by the event's data) and TSW_MusicEvent_Activation_Start as music mode 8; AttackAnim: its SFXEvent_Fire (weapon
    // fire rules); PlanetDestroyed: the target dummy's Death_SFXEvent_Start_Die at the dummy (unit_death: the dummy's own
    // sounds stop). Every owner's TSW is heard [M: the spec lists no owner test]. S8 t31 (TACTICAL_COMBAT.md "Charge-up"):
    // LobChargeup plays the unit type's Lobbing_Superweapon_SFXEvent_Chargeup, LobCharged its
    // Lobbing_Superweapon_SFXEvent_Fire, both attached to the unit (every owner's). battle_tick calls it.
    void tsw_audio(std::span<const TswCue> cues, const objects::GameObjectTypes& types);
    void sim_tick();  // once per sim tick
    // aud1 (battle_tick's calls, public for the scripted battle tests): the tactical weather (type name, 0..1 intensity:
    // GameAudio::set_weather, weather loops + the faction SFXEvent_Weather_Begin / _End) and one combat notification
    // (AUDIO.md §2.3 slot 76: battle music when the local player is involved, the base-under-attack announcement).
    // aud1 t03 (AUDIO.md §9.4): AnimationSFXMaps SOUND rows the sim's animation crossed this tick (SimAnimation::events,
    // kind Sound), in its order: each starts attached to the animating object (3D-attached when Is_3D, else 2D-attached),
    // no fog / owner test at start (fogged objects are muted). Returns the number started.
    struct AnimSound {
        uint64_t object = 0;
        std::string_view event;
        Vec3 position{};
    };
    uint32_t animation_sounds(std::span<const AnimSound> sounds);
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "GameObjectType.Spin_Away_On_Death_SFXEvent_Start_Die", DeathBehavior 0x58cf90): the
    // battle's spin-away corpses (SimAnimation::corpses with spin_away; `object` = the corpse id, `event` unused); each
    // new one plays its type's Spin_Away_On_Death_SFXEvent_Start_Die attached to it (3D when Is_3D, else 2D), any owner,
    // once per death. Returns the number started.
    struct SpinDeath {
        uint64_t object = 0;
        objects::TypeIndex type = objects::kNone;
        Vec3 position{};
    };
    uint32_t spin_away_deaths(std::span<const SpinDeath> deaths, const objects::GameObjectTypes& types);
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "GameObjectType.Selt_Destruct_SFXEvent_Start_Die"): this tick's counting-down units
    // (`event` = the type's Selt_Destruct_SFXEvent_Start_Die). A unit new to the list starts its event attached to it; a
    // unit that left the list (cancelled, blown up) has it stopped. battle_tick builds the list from the SELF_DESTRUCT
    // ability state. Returns the number started.
    struct SelfDestructCue {
        uint64_t object = 0;
        std::string_view event;
        Vec3 position{};
    };
    uint32_t self_destruct_lines(std::span<const SelfDestructCue> counting);
    // aud1 t04 (AUDIO.md §9.5): one shot's fire sound (fire_event(hardpoint_fire, shooter_fire)), 3D at the projectile's
    // spawn position. Returns the instance id (0 = none / refused).
    uint32_t weapon_fired(uint64_t shooter, std::string_view hardpoint_fire, std::string_view shooter_fire, const Vec3& position);
    // aud1 t05: each battle player's Faction name (index = player), read by battle_tick's object_events; set at battle start.
    void set_player_factions(std::vector<std::string> factions) { player_factions_ = std::move(factions); }
    // mcx4 t13 batch D: the running battle is a tactical corruption mission (0x28ae30; picks the _Tactical_Corruption_
    // Mission HUD result lines).
    void set_corruption_mission(bool on) { corruption_mission_ = on; }
    void weather(std::string_view type, float intensity, bool local_affected = true);  // local_affected: no Weatherproof
    void combat(bool local_involved, bool local_base_attacked);
    // --- aud1 t05 (object SFX: abilities, shields, hyperspace; src/game_audio_host_objects.cpp) ---
    // AUDIO.md §9.6, after every battle tick (battle_tick calls it; presentation only, const reads):
    //   ability   AbilitySystem EffectOn (the activation presentation 0x48e4d0): the special ability's SFXEvent_Activate
    //             attached to the user, SFXEvent_Target_Affected attached to the target when there is one. Read from
    //             activation_events() after the last serial played (aud1 t10: the battle clears events() inside its tick).
    //   hyperspace TacticalBattle::hyperspace_sfx(): Arrive -> the owner faction's SFXEvent_Arrive_From_Hyperspace attached
    //             to the object; Depart -> the type's SFXEvent_Retreat_Engines_Jump, else the owner faction's
    //             SFXEvent_Exit_Into_Hyperspace, 3D at the object's position (not attached).
    //   shields   the type's SFXEvent_Shields_Go_Down when the shield falls to 0, SFXEvent_Shields_Go_Back_Up when it
    //             regenerates above 0 (transitions only; vanilla sets neither tag).
    // `player_factions[p]` = player p's Faction name ("" / out of range: no faction sounds for its objects).
    void object_events(const sim::TacticalBattle& battle, const objects::ObjectStore& store, const objects::GameObjectTypes& types,
                       std::span<const std::string> player_factions);
    void object_events_reset();  // battle_reset: forget the log cursors and the shield states
    // aud1 t10, object_events' ability part (public for the tests): the activations after the last serial played (a
    // smaller newest serial: a new system, start over), each through ability_effect with the store's types and positions.
    void ability_activations(std::span<const tactical::abilities::AbilityActivation> log, const objects::ObjectStore& store,
                             const objects::GameObjectTypes& types);
    struct AbilitySfx {
        std::string activate, target_affected;  // "" = none
    };
    // The special ability that a unit-ability `ability_type` (tactical::abilities::AbilityType; the EffectOn event's
    // `type`) of object type `t` applies: the type's Unit_Ability of that Type names it (GUI_Activated_Ability_Name), the
    // <Abilities> entry of that Name gives the tags. [port] No Unit_Ability of that Type (auto-style specials): the type's
    // only <Abilities> entry, if it has exactly one. Data-free tests drive it through objects::GameObjectTypes.
    static AbilitySfx ability_sfx(const objects::GameObjectTypes& types, objects::TypeIndex t, uint8_t ability_type);
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "Unit_Ability.SFXEvent_GUI_Unit_Ability_Activated family", 0x374b90): `tag` of the
    // type's Unit_Ability record for `ability_type`: the first record when the ability is the type's ability #0, else the
    // second. "" when the type has no record of that ability.
    static std::string_view unit_ability_line(const objects::GameObjectTypes& types, objects::TypeIndex t, uint8_t ability_type,
                                              std::string_view tag);
    // The instant target / area abilities whose activation 0x3a3e20 plays the record's SFXEvent_Target_Ability
    // (types 19, 23..26, 28, 35, 36, 42 and SELF_DESTRUCT 39).
    static bool plays_target_ability_line(uint8_t ability_type);
    // mcx4 t13: `field` of the type's first <Abilities> entry whose element is `element` (e.g. Laser_Defense_Ability
    // Zap_SFXEvent), "" when none.
    static std::string_view special_line(const objects::GameObjectTypes& types, objects::TypeIndex t, std::string_view element,
                                         std::string_view field);
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds family", 0x42b560): the
    // highest of 60 / 30 / 15 / 5 s that the TSW's ETA crossed between two services (prev > t >= now), 0 for none; and
    // the local faction's speech tag for a threshold (0 = moved into range), the _Enemy_ form for an enemy TSW.
    static int32_t tsw_threshold_crossed(float prev_eta, float eta);
    static std::string_view tsw_speech_tag(int32_t threshold, bool enemy);
    // The faction toggle list tag for a SetUnitAbilityMode toggle (0x430270): on / off, the local player hostile to the
    // issuer -> the _Enemy_ variant.
    static std::string_view toggle_line_tag(bool on, bool local_hostile);
    // mcx4 (tac2 handover; MCX4_T11_T12_RE_ANSWERS §C3, GameConstants 0x2c79d0 / 0x2c7bb0, selection 0x38290): the unit of a
    // multi-selection that speaks the unit response is the best-ranked one. `rankings` is the
    // Unit_Command_Rankings_By_Category list (names split on ',', '|' and blanks); a type's rank is the index of the
    // first entry its CategoryMask text names (case-insensitive), rankings.size() when none.
    static std::vector<std::string> command_rankings(std::string_view text);
    static size_t command_rank(std::span<const std::string> rankings, std::string_view category_mask);
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "Faction.SFXEvent_HUD_Reinforcement_Point_Contested", 0x501980): a local player's
    // REINFORCEMENT_POINT whose capture progress starts from 0 with a pending controller other than its owner (an owned
    // point is first neutralised) is being contested. [M] "pending != owner" stands for "pending = the neutral player".
    static bool point_contested(bool reinforcement_point, int32_t owner, int32_t local, float prev_progress, float progress,
                                int32_t pending);
    // The HUD ability button click (0x307190, local player): the first user's record line, _Activated for an activate
    // click (a targeted ability: at targeting start), _Deactivated for a deactivate click. 2D.
    void ability_button_line(const objects::GameObjectTypes& types, objects::TypeIndex t, uint8_t ability_type, bool activate);
    // Departure tag pick: the type's SFXEvent_Retreat_Engines_Jump when set, else the faction's SFXEvent_Exit_Into_Hyperspace.
    static std::string_view departure_sfx(std::string_view type_retreat_jump, std::string_view faction_exit);
    // Shield transition: 1 = went down (up -> 0 points), 2 = back up (down -> > 0), 0 = none. `was_up` < 0 = first sight.
    static int shield_transition(int was_up, float fraction);
    // One ability presentation (public for the scripted tests): plays the tags of ability_sfx(types, t, ability_type).
    void ability_effect(const objects::GameObjectTypes& types, objects::TypeIndex t, uint8_t ability_type, uint64_t user,
                        const Vec3& user_position, uint64_t target, const Vec3& target_position);
    // One hyperspace arrival (public for the scripted tests): the owner faction's SFXEvent_Arrive_From_Hyperspace attached
    // to the arriving object. Returns the instance id (0 = none / refused).
    uint32_t hyperspace_arrival(uint64_t object, std::string_view owner_faction, const Vec3& position);
    // mcx6 t05 (docs/re/MCX6_T05_CINEMATIC_BINDINGS.md): Lua Cinematic_Hyperspace_In's arrival sound (state 0x2c timer 45):
    // hyperspace_arrival with player `owner`'s faction (set_player_factions; none: nothing plays).
    uint32_t cinematic_arrival(uint64_t object, int32_t owner, const Vec3& position);
    // Lua Play_Cinematic_Engine_Flyby (0x57ea50): the object type's SFXEvent_Ambient_Moving (type +0x1520) attached to the
    // object (0x2d5320). Returns the instance id (0 = no tag / refused).
    uint32_t cinematic_engine_flyby(const objects::GameObjectTypes& types, objects::TypeIndex type, uint64_t object,
                                    const Vec3& position);
    uint64_t ability_sfx_played() const { return ability_sfx_played_; }
    uint64_t hyperspace_sfx_played() const { return hyperspace_sfx_played_; }
    uint64_t shield_sfx_played() const { return shield_sfx_played_; }

private:
    std::map<std::pair<objects::TypeIndex, uint8_t>, AbilitySfx> ability_sfx_cache_;  // lookup only
    std::map<objects::TypeIndex, std::pair<std::string, std::string>> shield_tags_;   // type -> (down, back up)
    std::map<objects::ObjectId, int> shield_up_;                                      // ascending id
    size_t hyperspace_seen_ = 0;  // TacticalBattle::hyperspace_sfx() entries already played
    uint32_t activation_seen_ = 0;  // aud1 t10: the last AbilityActivation::serial played
    uint64_t ability_sfx_played_ = 0, hyperspace_sfx_played_ = 0, shield_sfx_played_ = 0;

public:
    // --- end aud1 t05 ---
    // --- aud1 t07 (tactical intro speech; src/game_audio_host_intro.cpp) ---
    // AUDIO.md §9.7 (0x364b20): once per battle the local player's faction speaks one SpeechEvent_Tactical_Intro_* line
    // (Faction fields), only to the local player and only for its side. Both builds.
    struct IntroSpeechFacts {
        bool land = false;               // land battle (else space)
        bool attacker = false;           // the local player is the battle's attacker (else its defender)
        bool last_location = false;      // land, galactic parent: the local player owns fewer than 2 planets (0x4b2080)
        bool raid = false;               // land: the conflict is a raid (record +0x50)
        bool defender_playable = true;   // the defender's faction Is_Playable (+0x20a8; gates the land attacker default)
    };
    // The faction's raw fields (audio::FactionAudio::fields: tag as written, trimmed text, document order).
    using FactionFieldList = std::span<const std::pair<std::string, std::string>>;
    // Step 4 of §9.7: the speech event for `f`, or "" for none. Order: the side's _Conditional_Or entries, then its
    // _Conditional_And entries ("A, B, C, Event"; Or = A or B present and C absent, And = A and B present and C absent;
    // "None" / empty names are never present; `present(type name)` = an object of that type is at the battle's planet,
    // any owner); land only: _Land_<side>_Last_Location, then _Land_Raid_<side>; then the side's default
    // (_Land_Attacker only when the defender's faction is playable, else nothing). An empty field is skipped.
    static std::string intro_speech_pick(FactionFieldList fields, const IntroSpeechFacts& f,
                                         const std::function<bool(std::string_view)>& present);
    // Steps 1-2 of §9.7: Stop = never in this battle (skirmish / multiplayer / story-linked battle: `eligible` false);
    // Wait = retry next tick (a cinematic camera, the battle frame below 10 s x 30 fps, or speech queued / playing);
    // Speak = decide now (step 3 onwards).
    enum class IntroGate : uint8_t { Wait, Speak, Stop };
    static IntroGate intro_speech_gate(bool eligible, uint32_t battle_frame, bool cinematic, bool speech_busy);
    // After every battle tick: the gate, then once `facts` (false = the local player is neither attacker nor defender:
    // nothing) and intro_speech_pick for the local faction (GameAudio::local_faction), queued like speech(). Decided once
    // per battle; a battle frame lower than the last one seen starts a new battle. Returns the queued event ("" = none).
    std::string intro_speech_tick(uint32_t battle_frame, bool eligible, bool cinematic,
                                  const std::function<bool(IntroSpeechFacts&)>& facts,
                                  const std::function<bool(std::string_view)>& present);

private:
    bool intro_decided_ = false;  // t07: this battle's intro was decided (played or not)
    uint32_t intro_frame_ = 0;    // t07: the battle frame of the last intro_speech_tick

public:
    // --- end aud1 t07 ---
    // ccpgu t01 (SKIRMISH_SETUP 4.2 announcer 0x3611f0, presentation only): the local faction's Faction tag for the control
    // point domination countdown after a battle tick, or "" for none. Retail plays it when the timer (mode +0x2b4) equals
    // 60 / 30 / 15 / 5 s x 30 frames: _Owned_ when the local player leads, _Ally_Owned_ for an ally (relation 0),
    // _Enemy_Owned_ for an enemy; no local player (a Neutral observer) hears nothing. `prev` = the timer after the previous
    // tick (TacticalBattle::control_point_timer(), -1 = not armed), `timer` = now. [port] The host sees the timer once per
    // tick: a mark is played when this tick passed it (prev > mark >= timer), and on (re)arming (timer > prev) when the arming
    // value is the mark or one frame below it (the arm and the first countdown step can fall in the same tick).
    static std::string_view control_point_line(int32_t prev, int32_t timer, int32_t leader, int32_t local_player, bool leader_is_enemy);

    // aud4 (battle_tick's presentation rules, public for the tests; both builds):
    // L1_S4_REMAINING §4.1 (b0289): a unit that actually entered a garrison -> the local faction's SFXEvent_Bunker_Garrisoned
    // [M: for the local player's own units only], else "". SFXEvent_Bunker_Vacated has no traced trigger (backlog).
    static std::string_view garrison_line(int32_t unit_owner, int32_t local_player);
    // mcx4 t13 (docs/re/MCX4_T13_RE_ANSWERS.md; battle_tick's rules, public for the tests):
    // "GameObjectType.SFXEvent_Health_Low_Warning family" (Take_Damage 0x3a9e30 [H]): the type tag a hit plays when it takes
    // the victim's health fraction from `before` to `after` across a threshold, the critical one tested first
    // (GameConstants Health_Critical_Percent_Threshold / Health_Low_Percent_Threshold): the victim type's
    // SFXEvent_Health_Critical_Warning / _Low_Warning when the local player owns the victim, else the attacker type's
    // SFXEvent_Enemy_Damaged_Health_Critical_Warning / _Low_Warning when the local player owns the attacker; "" otherwise.
    static std::string_view health_warning_tag(float before, float after, float low, float critical, bool local_victim,
                                               bool local_attacker);
    // "GameObjectType.SFXEvent_Hardpoint_Destroyed family" (0x386660 [H]): the parent type's tag when the local player's live
    // parent loses a hard point of `kind` (HardpointKind order): SFXEvent_Hardpoint_All_Weapons_Destroyed for a weapon kind
    // (5..10) when no weapon hard point is left, else SFXEvent_Hardpoint_Destroyed when none of that kind is left; "" otherwise.
    static std::string_view hardpoint_lost_tag(uint8_t kind, bool local_parent, bool parent_dying, bool last_of_kind,
                                               bool weapons_left);
    // The event for hard point kind `kind` in a per-kind list (SFXEvent_Hardpoint_Destroyed / SFXEvent_Attack_Hardpoint
    // lines "HARD_POINT_WEAPON_LASER, Event"; the 13 HARD_POINT_* names in HardpointKind order); "" when the kind has none.
    static std::string_view hardpoint_kind_event(std::span<const std::string_view> tokens, uint8_t kind);
    // "Faction.SFXEvent_Tactical_Gain_Friendly_Control family" (capture complete 0x500e30 [H]): the local faction's per-type
    // list tags a capture point owner change plays, {friendly half, enemy half} ("" = none). Friendly half when the local
    // player is friendly (relation 0, self included) to the old or the new owner: _Lose_Friendly_Control if friendly to
    // the old owner, else _Gain_Friendly_Control. Enemy half: _Lose_Enemy_Control when the old owner's faction is not
    // Is_Neutral and the local player is hostile to it; else none when the new owner's faction is neutral; else
    // _Gain_Enemy_Control when hostile to the new owner. `friendly_old` / `hostile_old` false for no owner (-1).
    // "Faction.SFXEvent_HUD_Won_Land_Battle family" (0x343810 from the victory declare 0x343b90 [H]): the local faction's
    // HUD line for a declared victory ("" = none). `local_won`: the winner is the local player; `local_hostile`: the
    // local player is hostile to the winner (an ally of the winner hears nothing). `enemy_tsw`: an enemy of the local
    // player owns a TACTICAL_SUPER_WEAPON object; `own_killer`: the local player owns a live Is_Super_Weapon_Killer object.
    // A space win against a TSW without a killer plays Space_Forced_Retreat_Due_To_Superweapon.
    static std::string_view battle_result_line(bool local_won, bool local_hostile, bool space, bool enemy_tsw, bool own_killer,
                                               bool corruption);
    // The 0x343b90 gate: victory types 0xb..0x16 that are not silent.
    static bool battle_result_gate(uint8_t victory_type, bool silent) { return victory_type >= 0x0b && victory_type <= 0x16 && !silent; }
    // "Faction.Land_Retreat_Begin_SFXEvent family" (begin: land 0x5350d0, space 0x533080 [H]): the local faction's line
    // when a retreat starts ("" = none): the retreater is local -> _Retreat_Begin_SFXEvent (after the retreating units'
    // own SFXEvent_Retreat_Start, which replaces it); the local player is hostile to the retreater -> _Enemy_Begin_.
    static std::string_view retreat_begin_tag(bool local_retreats, bool local_hostile, bool space);
    // "Faction.SFXEvent_HUD_Special_Weapon_Ready family" [H]: each faction tag is a list (one event per XML occurrence),
    // indexed by the weapon type's Special_Weapon_Target_Action_Index. special_weapon_tag gives the faction tag for a
    // ready (or fired) weapon whose owner is local / friendly (`own`) or hostile; nth_field the index-th occurrence of a
    // tag in `fields` ("" when absent or out of range).
    static std::string_view special_weapon_tag(bool fired, bool own);
    static std::string_view nth_field(std::span<const std::pair<std::string, std::string>> fields, std::string_view tag, int32_t index);
    // "Faction.SFXEvent_HUD_Base_Shield_Online family" (Set_State 0x56b540 step 2 [H]): the local faction's line for a
    // base shield going back online (2 -> 1) or offline (1 -> 2), by the owner's relation (own / friendly vs hostile).
    // The first evaluation (0 -> 1 / 0 -> 2) plays nothing.
    static std::string_view base_shield_line(bool online, bool friendly);
    // P4_SIM_SYSTEMS §2.7.3 (player tick 0x287e70, tactical): "room for one unit" (0x2b03e0) = no cap (< 0) or the
    // population used below the cap; SFXEvent_Tactical_Pop_Cap_Reached plays when it turns false.
    static bool unit_room(float used, float cap) { return cap < 0.0f || used < cap; }
    // "Faction.SFXEvent_Bombard_Available family" (PlanetaryBombardManager 0x52a110 / BombingRunManager 0x495dd0 [H]): the
    // tag of player P's faction that announces P's bombardment (`bombard`) or bombing run becoming available: P is the
    // local player -> _Available, the local player friendly to P -> _Ally_Available, hostile -> _Enemy_Available.
    static std::string_view strike_available_tag(bool bombard, bool local, bool friendly);
    // "GameObjectType.Land_Damage_SFX" (0x3ac290 [H]; stage rule TACTICAL_GAPS §6.3): the damage stage for a health
    // fraction = the last Land_Damage_Thresholds entry k (list order) with fraction <= t_k, -1 when none. When a hit
    // changes it, Land_Damage_SFX[k] plays attached to the object unless it is "NULL" [M on the compare].
    static int32_t land_damage_stage(std::span<const std::string_view> thresholds, float fraction);
    // "Faction.SFXEvent_Tactical_Object_Building_Started family" (0x42dbd0 / 0x42e890 / 0x5248b0 [H]): the lines of a build
    // pad start, completion or sale. `type_tags` = the event type's 2D line (first set entry wins), heard only by a local
    // builder / seller (`local`; a start also needs its build command's sfx flag); `faction_tag` = the owner's faction
    // line for any owner (start / complete attached to the object, sale 3D at its position); `loop_tag` = the start's
    // attached loop, stopped on the under-construction object at completion.
    enum class PadLine : uint8_t { Started, Completed, Sold };
    struct PadLines {
        std::array<std::string_view, 2> type_tags{};
        std::string_view faction_tag, loop_tag;
    };
    static PadLines pad_lines(PadLine kind, bool local);
    struct ControlLines {
        std::string_view friendly, enemy;
    };
    static ControlLines control_lines(bool friendly_old, bool friendly_new, bool hostile_old, bool hostile_new, bool old_neutral,
                                      bool new_neutral);
    // HERO_CLASH §3.8 (b0300): the clash winner's speech event, First_Hero_Win_Speech when hero 1 (First_Hero_Type) won,
    // else Second_Hero_Win_Speech; "" for an unknown entry. (Attack_SFXEvent belongs to Clash_Actions, i.e. the
    // action-driven clash that shipped data never reaches, HERO_CLASH §4.)
    // aud1 t02: the weather audio name of a tactical::combat::WeatherType value (CLEAR, RAIN, SNOW, SANDSTORM, ASH; out of
    // range -> CLEAR), the names WeatherAudio.xml and the Faction SFXEvent_Weather_Begin / _End pairs use.
    static std::string_view weather_name(uint8_t type);
    // aud1 t01 (AUDIO.md §9.1): the Music_Event_List_* / Ambient_Map_Sounds key of a map terrain type
    // (TacticalBattle::terrain_type(): 0 Temperate .. 6 Urban, 7 Space); 8 or more -> "" (retail plays no list music).
    static std::string_view terrain_name(uint32_t type);
    // aud1 t04 (AUDIO.md §9.5): the fire sound of a shot - the hardpoint type's Fire_SFXEvent when it has one, else the
    // shooter's SFXEvent_Fire (only one plays; both empty = none). Unit weapon shots pass hardpoint_fire = "".
    // aud1 t08 (AUDIO.md §9.8): a hit on `victim_owner`'s object counts as "the local base attacked" when the victim is a
    // base object (land DUMMY_GROUND_STRUCTURE / DUMMY_LAND_BASE_LEVEL_COMPONENT, space DUMMY_STAR_BASE /
    // DUMMY_ORBITAL_STRUCTURE), the attacker is hostile to the victim's owner, and the owner is the local player or an ally
    // of it whose victim type has Is_Community_Property. The rate limit is GameAudio::combat's.
    static bool base_attacked(bool victim_is_base, bool attacker_hostile, int32_t victim_owner, int32_t local_player,
                              bool owner_is_ally, bool community_property);
    static std::string_view fire_event(std::string_view hardpoint_fire, std::string_view shooter_fire);
    static std::string_view clash_win_speech(const tactical::combat::HeroClashData* data, uint32_t entry, bool first_won);
    // AUDIO.md §2.3 (b0286): this tick's "any player has its faction super weapon on the map". The first report after
    // battle_reset re-picks the mode's music (the start forces are known one tick after the Battle scene began); later
    // reports only set the flag that the next mode switch reads.
    void super_weapon_report(bool present);

    // Mode / music scene with the local faction ("" keeps the previous); `space` / `environment` for battles.
    void music(MusicScene scene, std::string_view faction = {}, bool space = true, std::string_view environment = {});

    // Positions of objects for attached instances (copied from the render snapshot after each capture).
    void set_object_positions(std::vector<std::pair<uint64_t, Vec3>> positions);
    // `cinematic` = a Lua cinematic camera is active (AUDIO.md §1.2 / §1.6: listener at the camera, gates 2 / 9).
    void set_listener(const Vec3& eye, const Vec3& look_at, bool cinematic = false);
    // OI host: the game-state flags the AudioSystem gates and gains read (AUDIO.md §1.1 gate 4, §1.6, §3), owned by the
    // host. modal = a modal GUI dialog is open (speech ducking off); gui_only = gate 4's "flag +0x94 AND a modal dialog"
    // (the flag's retail owner is open [AUDIO.md §8]; [port] the host sets it while the in-game pause menu / its option
    // dialogs are up); attract = the main-menu attract demo runs (attract_mode.hpp). Each changes only on a new value.
    void set_flags(bool modal, bool gui_only, bool attract);
    bool attract() const { return attract_; }
    bool gui_only() const { return gui_only_; }
    // OI host (P3 video item 3): the output device's buffering in seconds (period size x periods / rate; 0 for the
    // null backend / inactive audio), subtracted from a movie's audio clock (MoviePlayer::set_audio_latency).
    double output_latency_seconds() const;
    // The profile's bus levels (options load / apply / preview). aud2 t02 (AUDIO.md §4): the master level is also saved as
    // the level Lua Master_Volume_Restore ramps back to (AudioSystem::save_master_volume).
    void set_volumes(float master, float sfx, float speech, float music);

    // --- aud2 (galactic, app and mixer hookups; both builds, no-ops without engine/audio) ---
    // t01 (GALACTIC_REVEAL_CORRUPTION 1.3): the galactic pre-step revealed a planet: Audio.xml's SFXEvent_Planet_Revealed
    // value (AudioConstants::sfx_planet_revealed; FoC GUI_Planet_Mouse_Over), 2D. Nothing when the constant is empty.
    void planet_revealed();
    // t04 (AUDIO.md §6.1, §2.3): whether this application is the active one (WM_ACTIVATEAPP). Inactive: update() does
    // nothing (step 14 runs only while active) and the music manager's pause counter is raised (AudioSystem::pause_music);
    // active again: the counter drops and updates resume. Only a change does anything. The host starts active.
    void set_app_active(bool active);
    bool app_active() const { return app_active_; }
    // t03 (AUDIO.md §6.2, xml_field_tables GameObject +0x1450 / +0x1460 / +0x1470): a galactic production event of the
    // local player plays the built type's SFXEvent_Build_Started / _Build_Cancelled / _Build_Complete (2D, as the
    // event's data says: the HUD voice lines use Preset_GUI-style 2D presets); a completion also starts the type's
    // Build_Music_Completed as music mode 4 (STRATEGIC_GAPS §7.4; [port] for the local player's own production only).
    // Returns the SFX instance id (0 = no tag / refused).
    // mcx4 t13 (MCX4_T13_RE_ANSWERS "GameObjectType.SFXEvent_Tactical_Build_Started family": queue add 0x496a20, complete
    // 0x498170 / 0x402840, cancel 0x498820 [H]): the type's Build_Speech_Underway / _Completed / _Stopped speech, when set,
    // replaces the SFX line; else in a battle (`tactical`) SFXEvent_Tactical_Build_Started / _Complete / _Cancelled, else
    // the plain SFXEvent_Build_* line. production_tags gives that order (speech tag first; tactical tag "" in galactic).
    enum class Production : uint8_t { Begin, Cancel, Complete };
    static std::string_view production_tag(Production kind);
    static std::array<std::string_view, 3> production_tags(Production kind, bool tactical);
    uint32_t production(const objects::GameObjectTypes& types, objects::TypeIndex type, Production kind, bool tactical = false);
    // t07 (BATTLE_TRANSITIONS, departure 0x431c80): a fleet left a planet's orbit. Galactic_Fleet_Hyperspace (2D, GUI
    // preset) when the local player owns the fleet or sees it (`local_sees`: the host's visibility test). Returns the
    // instance id (0 = not heard / refused).
    static constexpr const char* kFleetHyperspaceEvent = "Galactic_Fleet_Hyperspace";
    uint32_t fleet_departure(bool local_owner, bool local_sees);
    // t08 (GAME_SCORING, STRATEGIC_GAPS §7.4): the campaign-end dialog opened - Audio.xml
    // Music_Event_Battle_End_Summary_Screen_<Win/Lose>, music mode 16 (mcx4 t13: retail never reads the faction's
    // Music_Event_Strategic_*; `enemy_faction` is unused).
    void campaign_end(bool won, std::string_view enemy_faction = {});
    // Lua presentation audio ops (Play_Music, Stop_All_Speech, SFX permissions, ...) of events with seq >= *next_seq
    // through GameAudio (audio::apply_presentation_audio); *next_seq advances. No-op without engine/lua.
    void apply_presentation(const std::vector<lua::PresentationEvent>& events, uint32_t* next_seq);
    void update(double dt_seconds);

    const GameAudioStats& stats() const { return stats_; }

private:
    const audio::UnitSounds& sounds(const objects::GameObjectTypes& types, objects::TypeIndex t);
    Vec3 position_of(uint64_t id) const;
    // mcx4 t13 batch A: health warnings, hard point lost lines and hard point death explosions of this tick.
    void cue_lines(const sim::TacticalBattle& battle, const objects::ObjectStore& store, const objects::GameObjectTypes& types,
                   int32_t local_player);
    float health_low_ = 0.33f, health_critical_ = 0.10f;  // GameConstants Health_Low / _Critical_Percent_Threshold
    bool corruption_mission_ = false, victory_seen_ = false;  // mcx4 t13 batch D: set_corruption_mission; declare latch
    int32_t last_retreater_ = -1;                             // mcx4 t13 batch E: retreating_player() last tick
    std::map<int64_t, uint8_t> base_shield_state_;            // mcx4 t13 batch I: generator -> 1 on / 2 off (lookup only)
    bool unit_room_ = true;                                   // mcx4 t13 batch J: last tick's unit_room [M: starts true]
    std::vector<uint8_t> strike_avail_;                       // mcx4 t13 batch M: per player bit 0 bombard, bit 1 bombing run
    std::set<int64_t> sighted_;                               // mcx4 t13 batch N: units seen by the local player (lookup only)
    std::set<int32_t> spotted_types_;                         // types whose Unit_Type_Spotted line played
    bool enemy_spotted_done_ = false;                         // SFXEvent_Enemy_Spotted played this battle
    std::map<int64_t, int32_t> land_damage_stage_;            // mcx4 t13 batch O: object -> last stage (lookup only)
    std::map<int64_t, std::string> pad_loops_;                // mcx4 t13 batch P: UC object -> its building loop event
    std::set<uint64_t> spun_;                                 // mcx4 t13 batch Q: spin-away deaths already voiced
    std::map<int64_t, std::string> self_destructs_;           // mcx4 t13 batch Q: counting-down unit -> its started event
    struct TswApproach {
        float eta = 0.0f;
        uint8_t said = 0;   // bit 0..3: 60 / 30 / 15 / 5 s announced
        bool arming = false, in_range = false;
    };
    std::map<int64_t, TswApproach> tsw_approach_;             // mcx4 t13 batch S: TSW -> approach state (lookup only)
    std::map<int64_t, float> point_progress_;                 // mcx4 t13 batch U: capture point -> last progress
    // mcx4 t13 batch B: the local player's tactical build orders last tick (order id -> type) and built counts (type ->
    // TacticalProduction::built_count), to tell a completed order from a cancelled one.
    std::map<uint32_t, int32_t> prod_orders_;
    std::map<int32_t, uint32_t> prod_built_;
    // aud1 t04: Fire_SFXEvent of a CombatData::hardpoints entry (resolved by its HardPoint type name, cached; "" none).
    const std::string& hardpoint_fire_sfx(const objects::GameObjectTypes& types, const tactical::combat::CombatSystem& cs, int32_t hardpoint);

    std::unique_ptr<audio::AudioDatabase> db_;
    std::unique_ptr<audio::GameAudioData> game_data_;
    std::unique_ptr<audio::SampleSource> samples_;
    std::unique_ptr<audio::VoiceBackend> backend_;
    std::unique_ptr<audio::AudioSystem> system_;
    std::unique_ptr<audio::GameAudio> game_;
    SimRng rng_;  // FREE: presentation only
    std::vector<std::string> speech_done_;  // ended speech events not taken yet
    std::map<objects::TypeIndex, std::unique_ptr<audio::UnitSounds>> sounds_;  // lookup only
    uint8_t base_behaviors_[4] = {0, 0, 0, 0};  // aud1 t08: behaviour ids (land 2, space 2), resolved on first use
    bool base_behaviors_ready_ = false;
    std::vector<std::string> player_factions_;  // aud1 t05: set_player_factions
    std::map<int32_t, std::string> hardpoint_fire_;  // aud1 t04: hardpoint desc index -> Fire_SFXEvent (lookup only)
    std::unordered_map<uint64_t, Vec3> positions_;                             // lookup only (never iterated)
    std::set<objects::ObjectId> spawned_;                                      // battle units already announced
    std::map<objects::ObjectId, bool> moving_;                                 // ascending id
    std::unordered_set<uint64_t> inaudible_;  // lookup only (never iterated): hidden / stealthed / fogged for the local player
    std::unordered_set<uint64_t> hidden_;     // lookup only: the stealthed / Lua-hidden part of inaudible_ (muted in cinematics too)
    std::vector<objects::TypeIndex> super_weapon_types_;  // b0286: every faction's Faction_Super_Weapon type (resolved once)
    bool super_weapon_types_ready_ = false, battle_reported_ = false;
    size_t clash_ends_seen_ = 0;  // b0300: HeroClashManager::ended() entries already voiced  // battle_reported_: a battle_tick ran since battle_reset
    size_t upgrades_seen_ = 0;                // TacticalBattle::station_upgrades() announced so far
    int32_t cp_timer_ = -1;                   // ccpgu t01: TacticalBattle::control_point_timer() at the last battle_tick
    MusicScene scene_ = MusicScene::None;
    uint32_t last_serial_ = 0;
    bool have_serial_ = false;
    int32_t retreating_player_ = -1;  // TacticalBattle::retreating_player() at the last battle_tick
    bool modal_ = false, gui_only_ = false, attract_ = false;  // OI host: flags last applied
    bool app_active_ = true;                                   // aud2 t04: set_app_active
    bool pending_played_ = false;  // battle_pending ran since the last scene change
    bool movie_music_on_ = false, movie_halted_ = false;  // a movie's mode 14 / 17 music is the current one / it halted the music
    GameAudioStats stats_;
};

}  // namespace alamo::game
