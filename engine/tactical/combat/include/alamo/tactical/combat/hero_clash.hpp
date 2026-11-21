// s78n t06 - hero clashes (docs/re/HERO_CLASH.md §1-§6; TACTICAL_COMBAT §5.4.3 item 3). Land mode only.
//   * HEROCLASH.XML (§1): one entry per <Hero_Clash>, file order, duplicate names kept (the loader 0x4814d0 does not
//     check them). Action_Driven entries and their Clash_Actions are parsed; they start only with a host that carries
//     out the action runner's effects (HeroClashHost::action_clashes; shipped Clash_Range 0.0, so retail never starts
//     them either). The runner (mcx4, MCX4_T10_RE_ANSWERS §11, MCX4_T11_T12_RE_ANSWERS §B3): each object without a
//     running action picks one by relevance-weighted draw, running actions are serviced every 2 (Attack), 6 (Move) or
//     3 (Special_Ability) frames, finished ones end; the clash ends when the remaining objects share one owner.
//   * HeroClashManager (§2, §3): the land mode's manager 0x56ce20, ticked in land tick step 4 (after weather, before the
//     common object tick). Detection 0x56c790 only when frame % 15 == 0 (no draws), then every running clash's update in
//     start order. Melee clash 0x6ac780 (start) -> approach (1) -> snap (2) -> fight (3) with one synced float draw per
//     exchange (outcome weights First / Second / draw, the previous outcome removed) and the exchange damage at 80 % of
//     hero 1's clip; it ends only when the exchange loser is dead or a hero is gone.
//   * The pairing rule 0x39a540 (§5.2) lives in CombatSystem::clash_allows (the +0x3b4 byte is kUnitHeroClash); the
//     manager hands its running pairs to the host (pairs()).
// The manager owns no object state: the host (HeroClashHost) answers queries and applies the effects (flag, movement,
// damage). Serial, deterministic (ids, never pointers).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/core/file_provider.hpp"
#include "alamo/core/sim_rng.hpp"
#include "alamo/tactical/combat/combat_system.hpp"

namespace alamo {
class ByteWriter;
class ByteReader;
class StateHasher;
}  // namespace alamo

namespace alamo::tactical::combat {

// Clash_Type (§1.1): Melee_Duel 0, Action_Driven 1, anything else -1 (never starts).
inline constexpr int32_t kClashMelee = 0;
inline constexpr int32_t kClashActionDriven = 1;
// Exchange outcomes (§3.6): 1 first hero wins, 2 second hero wins, 3 draw, 4 conversation; 0 = none yet.
inline constexpr uint8_t kExchangeNone = 0;
inline constexpr uint8_t kExchangeFirstWins = 1;
inline constexpr uint8_t kExchangeSecondWins = 2;
inline constexpr uint8_t kExchangeDraw = 3;
inline constexpr uint8_t kExchangeConversation = 4;
// [port] Exchange clip when the host has no model data (no sim animation, or a model without the clip): the shipped
// HC_WIN / HC_LOSE / HC_DRAW clips of Darth_Vader and Obi_Wan_Kenobi (one variant, 101 frames at 30 fps, §1.2).
inline constexpr uint32_t kClashFallbackClipFrames = 101;
inline constexpr float kClashFallbackClipFps = 30.0f;

// ---- action-driven clash data (§4; MCX4_T10_RE_ANSWERS §11 Attack_Action, MCX4_T11_T12_RE_ANSWERS §B3 Move_Action and
// Special_Ability_Action) ----
inline constexpr uint8_t kActionAttack = 0;
inline constexpr uint8_t kActionMove = 1;
inline constexpr uint8_t kActionSpecialAbility = 2;
// ActionRelevanceType (0x5e94a0): Constant, Distance_From_Target_Curve, Health_Percentage_Curve,
// Distance_From_Target_Curve_Times_Health_Percentage_Curve; -1 (unset or unknown) gives relevance 0.
inline constexpr int32_t kRelevanceConstant = 0;
inline constexpr int32_t kRelevanceDistance = 1;
inline constexpr int32_t kRelevanceHealth = 2;
inline constexpr int32_t kRelevanceDistanceTimesHealth = 3;
// MoveActionType (0x5ea440): Invalid 0, Move_To_Target 1, Move_Past_Target 2, Dodge_Shot_From_Target 3 (0 and 3 never start).
inline constexpr int32_t kMoveToTarget = 1;
inline constexpr int32_t kMovePastTarget = 2;
inline constexpr int32_t kMoveDodgeShot = 3;
// The variant a subindex < 0 becomes in the post-parse (slot 12): a random variant.
inline constexpr int32_t kAnyVariant = 0xFFFF;
// Frames between two services of a running action (slot 6).
inline constexpr int32_t kAttackInterval = 2;
inline constexpr int32_t kMoveInterval = 6;
inline constexpr int32_t kSpecialAbilityInterval = 3;

// LinearInterpolator (vtable 0x85c0f8, value vf2 0x2c8660; SKIRMISH_SETUP §6): points sorted by x, piecewise linear
// between them, 0 below the first / above the last point or with fewer than two points.
struct ClashCurve {
    std::vector<std::pair<float, float>> points;  // sorted by x at parse (stable)
    float value(float x) const;
};

// One <Attack_Action> / <Move_Action> / <Special_Ability_Action> of Clash_Actions (the template the clash clones).
struct ClashAction {
    uint8_t kind = kActionAttack;
    std::string name;
    std::string object_type, target_type;  // Action_Object / Action_Target (type names; HeroClashHost::type_by_name)
    int32_t relevance_type = -1;           // Action_Relevance_Type
    float relevance_constant = 0.0f;       // Action_Relevance_Constant
    ClashCurve distance_curve;             // Action_Distance_From_Target_Curve (x = actor-target 3-D distance)
    ClashCurve health_curve;               // Action_Health_Percentage_Curve (x = the actor's health fraction)
    // Attack_Action (ctor 0x71e840 defaults). Attack_Accuracy_Modifier is parsed by retail and never read (§11).
    std::string projectile_type;           // Attack_Projectile_Type (the host resolves it)
    std::string attack_anim;               // Attack_Animation_Type (a ModelAnimType name; empty = none)
    int32_t attack_subindex = kAnyVariant;  // Attack_Animation_Subindex
    float attack_speedup = 1.0f;           // Attack_Animation_Speedup
    std::string firing_bone = "ROOT";      // Attack_Firing_Bone_Name
    int32_t shots = 1;                     // Attack_Number_Of_Shots
    std::string sfx;                       // Attack_SFXEvent (audio only)
    // Move_Action (ctor 0x5e9b50 defaults).
    int32_t move_type = 0;                 // Move_Type
    float move_speed = -1.0f;              // Move_Speed (> 0: a speed override)
    std::string move_anim;                 // Move_Animation_Type
    int32_t move_subindex = kAnyVariant;   // Move_Animation_Subindex
    float move_speedup = 1.0f;             // Move_Animation_Speedup
    bool anim_override = false;            // Move_Animation_Override
    float min_distance = 0.0f;             // Move_Min_Distance
    float max_distance = 0.0f;             // Move_Max_Distance
    bool use_jetpack = false;              // Move_Use_Jetpack
    bool wait_for_jetpack = false;         // Wait_For_Finish_If_Target_Using_Jetpack
    // Special_Ability_Action.
    std::string ability_name;              // Ability_Name
    float cooldown_secs = 0.0f;            // Ability_Cooldown_Time_In_Secs
    int32_t cooldown_frames = 0;           // post-parse 0x71f5f0: round_half_away(30 x cooldown)
};

struct HeroClashEntry {
    std::string name;
    int32_t clash_type = -1;            // Clash_Type
    float clash_range = 0.0f;           // Clash_Range (3-D start distance)
    bool play_conversation = false;     // Play_Conversation_Events
    float combat_distance = 0.0f;       // Combat_Distance
    float damage_amount = 0.0f;         // Damage_Amount
    float damage_percentage = 0.0f;     // Damage_Percentage
    float first_win = 0.0f;             // First_Hero_Win_Exchange_Chance
    float second_win = 0.0f;            // Second_Hero_Win_Exchange_Chance (draw = 1 - first - second)
    float first_multiplier = 0.0f;      // First_Hero_Damage_Multiplier
    float second_multiplier = 0.0f;     // Second_Hero_Damage_Multiplier
    std::string first_type, second_type;  // First_Hero_Type / Second_Hero_Type (resolved by the host)
    std::vector<std::string> involved;    // Involved_Hero_Types
    // Win / Lose / Draw / Conversation animation type names (First_Hero_*_Anim_Type, Second_Hero_*_Anim_Type).
    std::array<std::string, 4> first_anims, second_anims;
    std::string first_speech, second_speech;  // *_Win_Speech (audio only)
    uint32_t action_count = 0;                // Clash_Actions children (every element)
    std::vector<ClashAction> actions;         // the Attack / Move / Special_Ability actions among them, file order
};

struct HeroClashData {
    std::vector<HeroClashEntry> entries;  // file order, duplicates kept
};

// Parses HEROCLASH.XML bytes (appends to `out`); false when the root is not <Hero_Clashes>. First + Second win chance
// > 1: the loader's error, both set to 0.3 (§1.1).
bool parse_hero_clash_xml(std::span<const uint8_t> bytes, HeroClashData& out, std::vector<std::string>* diagnostics = nullptr);
// Data\XML\HeroClash.xml through the provider; false when the file is missing (no clashes).
bool load_hero_clash_data(const FileProvider& files, HeroClashData& out, std::vector<std::string>* diagnostics = nullptr);

// One exchange outcome (§3.6 "Start a new exchange", the non-conversation branch): weights {1: first, 2: second,
// 3: 1 - first - second} without the previous outcome's key; one synced float draw r = Get_Float(0, total); keys
// ascending, r -= w, the first key with r <= 0 wins; fallback 3.
uint8_t clash_pick_outcome(uint8_t previous, float first_win, float second_win, SimRng& rng);
// The exchange damage (§3.6 step 4): (int)round_half_away((loser start health x Damage_Percentage + Damage_Amount) x
// mult), mult = First_Hero_Damage_Multiplier when the winner's type is First_Hero_Type, Second_... when it is
// Second_Hero_Type, else 0 (0x480ed0).
int32_t clash_exchange_damage(const HeroClashEntry& e, float loser_start_health, bool winner_is_first_type,
                              bool winner_is_second_type);

// Host side of the manager (TacticalBattle in the sim; a stub in the tests). Called from the serial land tick only.
class HeroClashHost {
public:
    virtual ~HeroClashHost() = default;
    // Live objects of exactly type `type` (team containers excluded [port]: a melee start needs the hero objects, so a
    // container candidate always fails in retail), any order.
    virtual void candidates(int32_t type, std::vector<ObjectId>& out) = 0;
    virtual bool alive(ObjectId id) = 0;  // not dying / death clone / removed (+0x3a0 0x40 / 0x80 / 0x02)
    virtual int32_t type_of(ObjectId id) = 0;
    virtual int32_t owner(ObjectId id) = 0;
    virtual bool hero_type(int32_t type) = 0;      // Is_Named_Hero or Is_Generic_Hero (0x39a600)
    virtual bool enemies(int32_t a, int32_t b) = 0;  // relation table [a][b] == 1 (0x2824d0)
    virtual Vec3 position(ObjectId id) = 0;
    virtual float yaw_deg(ObjectId id) = 0;
    virtual float health(ObjectId id) = 0;         // current hull health (+0x5c)
    virtual bool jetpacking(ObjectId id) = 0;      // an active jet-pack locomotor (0x39ae20)
    virtual ObjectId attack_target(ObjectId id) = 0;  // 0x397a40(obj, 0): formation attack target, else the target
    virtual bool walking(ObjectId id) = 0;         // locomotor vf +0x1c0(0): the walk has a path
    virtual bool in_clash(ObjectId id) = 0;        // the +0x3b4 byte
    // +0x3b4 := on. On: TARGETING suspended (slot 9) and the movement orders locked (0x3a8e50(h, 1)); off: unlocked and
    // TARGETING resumed (the release 0x6acb80, states 1 / 3 / 4).
    virtual void set_in_clash(ObjectId id, bool on) = 0;
    // Begin 0x6abde0: leave the hero's coordinators, a new coordinator with a Move_To `spot`.
    virtual void move_to(ObjectId id, Vec3 spot) = 0;
    // Snap 0x6acfb0: position := `pos`, yaw := `yaw_deg` at once (0x3a8f90 / 0x3a8710; no pathing, no collision).
    virtual void place(ObjectId id, Vec3 pos, float yaw_deg) = 0;
    virtual void face(ObjectId id, float yaw_deg) = 0;  // Fight 0x6abcd0: the snap turn toward the opponent
    // Fight: suspend IDLE (slot 7) and the locomotor (slot 1); false: resume them (release in state 3 / 4).
    virtual void hold(ObjectId id, bool on) = 0;
    // Clip of animation `anim` (a ModelAnimType name) on `id`'s model: variant count, frame count and fps of `variant`.
    // False: no model data ([port] the manager then uses the shipped clip, kClashFallbackClip*).
    virtual bool clip(ObjectId id, std::string_view anim, uint32_t variant, uint32_t& variants, uint32_t& frames, float& fps) = 0;
    // r3h t02 (§3.6): an exchange starts: `id` plays animation `anim` (a ModelAnimType name, the clip clip() described)
    // variant `variant` (already drawn), one-shot, blend 0.1, speed 1, start frame 0, no lock (Play_Animation 0x3a92f0).
    // Called for both heroes only when the manager plays the exchange. Default: nothing (no animation world).
    virtual void play(ObjectId id, std::string_view anim, uint32_t variant) { (void)id, (void)anim, (void)variant; }
    // The speech manager allows / is done with a conversation (0x3223c0 / 0x3223f0). [port] audio: the host may answer true.
    virtual bool speech_idle() = 0;
    // Take_Damage (0x3a9e30): victim, kind 1, amount, the winner as a non-projectile source, no hardpoint.
    virtual void damage(ObjectId victim, float amount, ObjectId source) = 0;

    // ---- action-driven clash (§4; MCX4_T10_RE_ANSWERS §11, MCX4_T11_T12_RE_ANSWERS §B3) ----
    // True when the host carries out the effects below: Action_Driven entries start only then. Default false: such a host
    // keeps the s78n t06 rule "parse, never start" (shipped data cannot start them anyway, Clash_Range 0.0).
    virtual bool action_clashes() { return false; }
    virtual int32_t type_by_name(std::string_view /*name*/) { return -1; }  // Action_Object / Action_Target; -1 unknown
    virtual float health_fraction(ObjectId /*id*/) { return 1.0f; }       // 0x396df0 [M: current / max health]
    // On (start 0x6ab200): 0x63f060(obj, 1, 1) - the locomotor stopped and suspended, behaviours 9 (TARGETING), 10, 11,
    // 7 (IDLE) and 0x20 suspended -, +0x3b4 := 1 on the object and its team container (in_clash() answers true), and a
    // new coordinator with a Move_To its own position. Off (the release in 0x6ab4d0): 0x63f060(obj, 0, 1), +0x3b4 := 0.
    virtual void action_hold(ObjectId /*id*/, bool /*on*/) {}
    // Attack_Action. Play_Animation 0x3a92f0 of the animation type named `anim` (a ModelAnimType name), variant `subindex`
    // (kAnyVariant: a random one), looped or one-shot, blend seconds, speed; false when the model has no such clip.
    virtual bool play_clip(ObjectId /*id*/, std::string_view /*anim*/, int32_t /*subindex*/, bool /*loop*/, float /*blend*/,
                           float /*speed*/) {
        return false;
    }
    virtual void play_default_clip(ObjectId /*id*/, float /*blend*/) {}  // the type's default clip (0x395920), its loop flag
    // 0 when the current clip is not of the type named `anim` (0x3956a0), 1 while it runs, 2 once it ended (0x395660).
    virtual int32_t clip_state(ObjectId /*id*/, std::string_view /*anim*/) { return 0; }
    // WeaponBehaviorClass fire 0x54ade0: shooter, the firing bone (index of `bone` in the model, 0 when absent), target,
    // the projectile type named `projectile`.
    virtual void fire_projectile(ObjectId /*shooter*/, std::string_view /*bone*/, ObjectId /*target*/, std::string_view /*projectile*/) {}
    virtual void flash(ObjectId /*id*/, std::string_view /*bone*/, bool /*on*/) {}  // the "<bone>_FLASH" sub-mesh (visual)
    virtual void sound(ObjectId /*id*/, std::string_view /*sfx_event*/) {}          // Attack_SFXEvent (audio)
    // Move_Action.
    virtual bool busy(ObjectId /*id*/) { return false; }             // 0x39ad40: true refuses the start
    virtual bool has_locomotor(ObjectId /*id*/) { return true; }     // behaviour 1
    virtual bool locomotor_suspended(ObjectId /*id*/) { return false; }  // locomotor vf +0x210
    virtual void suspend_locomotor(ObjectId /*id*/, bool /*on*/) {}  // vf +0x200 / +0x208
    virtual void jetpack_locomotor(ObjectId /*id*/, bool /*on*/) {}  // 0x3a9910 (on: the jet-pack locomotor; off: back)
    // A move goal: z put on the ground (0x5e9c30), then the passability test 0x50eeb0 and, for `free_cell`, the free-cell
    // test 0x538200 when the mode has one. False: that try fails (up to 5 tries).
    virtual bool move_goal(ObjectId /*id*/, Vec3& /*goal*/, bool /*free_cell*/) { return true; }
    virtual bool can_move(ObjectId /*id*/) { return true; }  // 0x397260: the actor has a movement coordinator
    // 0x3a8e20: the move clip := `anim` / subindex / speedup (an earlier override is cleared first, 0x39ad10 / 0x38f8d0).
    virtual void move_anim_override(ObjectId /*id*/, std::string_view /*anim*/, int32_t /*subindex*/, float /*speedup*/) {}
    virtual void clear_move_anim_override(ObjectId /*id*/) {}  // 0x38f8d0
    virtual void speed_override(ObjectId /*id*/, float /*speed*/) {}  // 0x3a8c90 (an earlier override cleared first)
    virtual void clear_speed_override(ObjectId /*id*/) {}            // 0x38f8b0
    virtual void order_move(ObjectId /*id*/, Vec3 /*goal*/) {}       // 0x4f8200: a move order of the actor's coordinator
    virtual void lock_orders(ObjectId /*id*/, bool /*on*/) {}        // 0x3a8e50
    // Special_Ability_Action: activate the actor's ability named `ability` on `target` (0x37b750); false: not started.
    virtual bool activate_ability(ObjectId /*id*/, std::string_view /*ability*/, ObjectId /*target*/) { return false; }
};

class HeroClashManager {
public:
    enum State : uint8_t { kApproach = 1, kSnap = 2, kFight = 3 };
    struct Clash {
        uint32_t entry = 0;
        ObjectId hero1 = kNoObject, hero2 = kNoObject;
        int32_t type1 = -1, type2 = -1;
        float health1 = 0.0f, health2 = 0.0f;  // +0x38 / +0x3c: health at the clash start
        uint8_t state = kApproach;
        Vec3 spot1, spot2;
        Vec3 snap_pos1, snap_pos2;          // snap start positions
        float snap_yaw1 = 0.0f, snap_yaw2 = 0.0f;  // snap start yaws
        float want_yaw1 = 0.0f, want_yaw2 = 0.0f;  // snap target yaws (own spot -> other spot)
        uint32_t snap_start = 0, snap_end = 0;
        uint8_t outcome = kExchangeNone;   // +0xb8
        uint8_t previous = kExchangeNone;  // +0xbc
        ObjectId ex_winner = kNoObject, ex_loser = kNoObject;
        bool damage_applied = false;       // +0x50
        bool playing = false;              // the exchange clips play (else the next frame starts a new exchange)
        uint32_t ex_start = 0;             // frame the exchange started
        uint32_t ex_frames = 0;            // hero 1's clip frame count
        float ex_fps = 0.0f;               // hero 1's clip fps
        uint32_t exchanges = 0;            // started exchanges (diagnostics; saved, hashed)
    };
    struct EndEvent {
        uint32_t entry = 0;
        ObjectId winner = kNoObject, loser = kNoObject;
        uint32_t frame = 0;
        bool by_removal = false;  // a hero left (§3.8 object-removed handler) instead of dying from the exchange
        bool first_won = false;   // the winner is hero 1 (First_Hero_Type): First_Hero_Win_Speech, else Second_ (audio)
    };

    // A running clone of a ClashAction (slot 4 clone, slot 5 start; MCX4_T10 §11, MCX4_T11_T12 §B3).
    struct RunningAction {
        uint32_t action = 0;  // index in the entry's actions
        ObjectId actor = kNoObject, target = kNoObject;  // kNoObject once removed (the action is then finished)
        int32_t last_service = 0;  // frame of the last service (0 at the start)
        int32_t interval = 0;      // slot 6
        bool finished = false;     // Attack +0x120, Move +0xe7
        // Attack: the Attack_Animation_Type / subindex were replaced by the fallback clip (type 5 "Attack", any variant).
        bool fallback_clip = false;
        int32_t shots_fired = 0;   // +0x124
        int32_t last_shot = -1;    // +0x12c: frame of the last shot
        // Move.
        bool loco_suspended = false;  // +0xe8: the locomotor was suspended at the start
        bool waiting = false;         // +0xe9: waiting for the target's jet-pack jump to end
        float last_d2 = 0.0f;         // +0xec: squared distance to the target
        int32_t restarts = 0;         // +0xf0
        // Special_Ability: frames left (+0xec).
        int32_t frames_left = 0;
    };
    // ActionDrivenHeroClashClass (vtable 0x8bd978; §4).
    struct ActionClash {
        uint32_t entry = 0;
        std::vector<ObjectId> objects;  // the involved objects (+0x30), detection order
        ObjectId winner = kNoObject;    // +0x48
        std::vector<RunningAction> running;  // +0x18, start order
        uint32_t picks = 0;             // started actions (diagnostics; saved, hashed)
    };

    // The entries and their First / Second hero types resolved by the host (-1 unknown: the entry never starts).
    void set_data(const HeroClashData* data, std::vector<std::pair<int32_t, int32_t>> types);
    const HeroClashData* data() const { return data_; }
    // The land tick step: detection when frame % 15 == 0, then every clash's update (start order: melee clashes, then
    // the action-driven ones [port]).
    void update(uint32_t frame, HeroClashHost& host, SimRng& rng);

    bool empty() const { return clashes_.empty() && action_clashes_.empty(); }
    const std::vector<Clash>& clashes() const { return clashes_; }
    const std::vector<ActionClash>& action_clashes() const { return action_clashes_; }
    uint32_t action_clashes_ended() const { return action_ended_; }
    // The running clashes' hero pairs, start order (CombatSystem::set_hero_clashes).
    std::vector<std::pair<ObjectId, ObjectId>> pairs() const;
    // True when the running clash list changed since the last call (start / end / load).
    bool take_changed() {
        const bool c = changed_;
        changed_ = false;
        return c;
    }
    std::vector<EndEvent> take_ended() { return std::exchange(ended_, {}); }
    // Every end since the last clear / load / take_ended, end order (the audio host reads new ones by count, b0300).
    const std::vector<EndEvent>& ended() const { return ended_; }
    uint32_t started_total() const { return started_; }

    void clear();
    void hash(StateHasher& h) const;
    // N1 stage 2: the running clashes (entry index + every field); data_ / types_ are the host's (set_data before read).
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);

private:
    bool candidate_ok(HeroClashHost& host, ObjectId id, int32_t other_type) const;
    void detect(uint32_t frame, HeroClashHost& host);
    bool start(uint32_t entry, ObjectId a, ObjectId b, HeroClashHost& host, uint32_t frame);
    // Returns false when the clash ended (removed from the list by the caller).
    bool update_clash(Clash& c, uint32_t frame, HeroClashHost& host, SimRng& rng);
    void begin_snap(Clash& c, uint32_t frame, HeroClashHost& host);
    void fight(Clash& c, HeroClashHost& host);
    void new_exchange(Clash& c, uint32_t frame, HeroClashHost& host, SimRng& rng);
    bool apply_damage(Clash& c, uint32_t frame, HeroClashHost& host);  // false: the clash ended
    void release(const Clash& c, ObjectId hero, HeroClashHost& host) const;
    void end(Clash& c, ObjectId winner, uint32_t frame, HeroClashHost& host, bool by_removal);
    // Action-driven clash (§4).
    bool start_action_clash(uint32_t entry, ObjectId a, ObjectId b, HeroClashHost& host);
    bool update_action_clash(ActionClash& c, int32_t frame, HeroClashHost& host, SimRng& rng);  // false: ended
    bool object_removed(ActionClash& c, ObjectId id, HeroClashHost& host, SimRng& rng);           // false: ended
    bool pick_action(ActionClash& c, ObjectId actor, HeroClashHost& host, SimRng& rng);
    float relevance(const ClashAction& a, ObjectId actor, const std::vector<ObjectId>& objects, HeroClashHost& host,
                    ObjectId& target) const;
    bool begin_action(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng);
    void service_action(RunningAction& r, const ClashAction& a, int32_t frame, HeroClashHost& host, SimRng& rng);
    void end_action(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng);
    void attack_fire(RunningAction& r, const ClashAction& a, int32_t frame, HeroClashHost& host);
    bool move_begin(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng);  // 0x5e9ed0 (type 1)
    bool move_goal(RunningAction& r, const ClashAction& a, HeroClashHost& host, SimRng& rng, Vec3& goal);
    bool move_start(RunningAction& r, const ClashAction& a, HeroClashHost& host, Vec3 goal);  // 0x5ea910

    const HeroClashData* data_ = nullptr;
    std::vector<std::pair<int32_t, int32_t>> types_;
    std::vector<Clash> clashes_;
    std::vector<ActionClash> action_clashes_;
    uint32_t action_ended_ = 0;
    std::vector<EndEvent> ended_;
    uint32_t started_ = 0;
    bool changed_ = false;
};

}  // namespace alamo::tactical::combat
