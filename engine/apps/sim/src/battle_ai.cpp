// A1 stage 2 hookup: BattleAi (battle_ai.hpp) - the tactical AI host of one S10 TacticalBattle.
#include "alamo/sim/battle_ai.hpp"

#ifdef ALAMO_SIM_HAVE_TACTICAL_AI
#include <algorithm>
#include <atomic>
#include <cassert>
#include <bit>
#include <cmath>
#include <map>

#include "alamo/ai/ai_types.hpp"
#include "alamo/ai/ai_goals.hpp"  // bkx1 t01: goal_quota_players
#include "alamo/ai/tactical/tactical_perception.hpp"
#include "alamo/core/ascii.hpp"
#include "alamo/core/dmath.hpp"
#include "alamo/core/profile.hpp"
#include "alamo/core/sim_crc.hpp"
#include "alamo/data/game_database.hpp"  // a2h t05: load_tactical_ai_data
#include "alamo/galactic/campaign.hpp"
#include "alamo/jobs/job_system.hpp"
#include "alamo/lua/object_bridge.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/sim/battle_object_bridge.hpp"  // ref2 t04
#include "alamo/sim/sim_host.hpp"
#include "alamo/sim/tactical_battle.hpp"
#include "alamo/tactical/movement/land_path.hpp"    // mcx6 t06: line_passable (attack position terrain check)
#include "alamo/tactical/movement/unit_params.hpp"  // refixf t06: space drop footprint
#include "save_io.hpp"

namespace alamo::sim {

namespace at = alamo::ai::tactical;
namespace cmd = alamo::commands;
namespace mv = alamo::tactical::movement;
namespace tc = alamo::tactical::combat;

namespace {

constexpr objects::BehaviorTypeId kUnitAi = 0x12, kTeam = 0x16, kHideWhenFogged = 0x19, kIndigenous = 0x20,
                                  kBuildObjects = 0x29, kLobbing = 0x37, kCashPoint = 0x3C, kCapturePoint = 0x3D,
                                  kDamageTracking = 0x3E;
// Free-store admission behaviours (A1_READINGS §6; behaviours.cpp numbering = the original's)
constexpr objects::BehaviorTypeId kTacticalSuperWeapon = 0x2C, kSpecialWeapon = 0x2F, kDummyGroundBase = 0x5B,
                                  kDummyStarBaseBeh = 0x5C, kDummyGroundStructureBeh = 0x5F, kDummyOrbitalStructure = 0x60;
constexpr uint32_t kStandinInterval = 60;  // the S10 stand-in cadence (BattleConfig::order_interval default)

uint32_t le32(std::span<const uint8_t> b, size_t o) {
    return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) | (static_cast<uint32_t>(b[o + 2]) << 16) |
           (static_cast<uint32_t>(b[o + 3]) << 24);
}

}  // namespace

AiLaneDefaults& ai_lane_defaults() {
    static AiLaneDefaults d;
    return d;
}

namespace {
std::atomic<uint64_t> g_lane_live_reads{0};       // ref5 t06 [diagnostic]
std::atomic<const char*> g_lane_live_first{nullptr};
}  // namespace

uint64_t ai_lane_live_reads(const char** first) {
    if (first) *first = g_lane_live_first.load();
    return g_lane_live_reads.load();
}

void BattleAi::lane_live_read(const char* what) const {
    if (!lane_thinking_) return;
    const char* none = nullptr;
    g_lane_live_first.compare_exchange_strong(none, what);
    g_lane_live_reads.fetch_add(1);
    assert(!"AI lane code read the live battle (BattleAi::lane_live_read)");
}

std::string default_tactical_ai_type(int32_t player, const galactic::GalacticSim& g) {
    if (player < 0 || static_cast<size_t>(player) >= g.players().size()) return {};
    const galactic::PlayerState& ps = g.players()[static_cast<size_t>(player)];
    if (ps.human || ps.faction >= g.rules().factions.size()) return {};
    const galactic::FactionRules& f = g.rules().factions[ps.faction];
    if (f.is_neutral || !f.is_playable) return {};
    if (f.basic_ai.empty() || ai::ai_iequal(f.basic_ai, "None")) return {};
    return f.basic_ai;
}

void load_tactical_ai_data(TacticalAiSetup& s, const FileProvider& files, const data::GameConstants* gc,
                           const std::function<std::vector<std::string>(std::string_view)>& list) {
    if (list) {
        s.space_plans = at::tactical_plan_files(at::TacticalMode::Space, list);
        s.land_plans = at::tactical_plan_files(at::TacticalMode::Land, list);
    }
    s.difficulties = at::load_tactical_ai_difficulties(files);
    if (gc) s.constants.load(*gc);
}

BattleAi::BattleAi(TacticalBattle& battle, TacticalMode& mode, SimHost& host, const TacticalAiSetup& setup)
    : b_(&battle), mode_(&mode), host_(&host) {
    const galactic::GalacticSim& g = *b_->galaxy_;
    at::TacticalAiConfig cfg;
    cfg.mode = b_->space_ ? at::TacticalMode::Space : at::TacticalMode::Land;
    cfg.difficulties = setup.difficulties;
    cfg.attacker = b_->req_.info.attacker;
    cfg.defender = b_->req_.info.defender;
    // t48: the port's skirmish battles also run under a galaxy, but are not campaign battles. t50: that holds for land skirmish
    // too now that it has its skirmish base (TacticalBattle::bt_skirmish_base): the shipped skirmish equations and plans run.
    cfg.campaign = !b_->cfg_.skirmish;
    cfg.constants = setup.constants;
    cfg.log_decisions = setup.log_decisions;
    cfg.game_seed = host.session_seed();  // A2 tactical: perception '#' reseed (AI_GALACTIC_3 §7.3)
    cfg.lua_gc_pf = host.parallel_for();  // OPT2 t04: scheduled Lua collections on the host's job system
    cfg.background = jobs::make_background_start(host.jobs());  // OPT2 t02: background path recomputes
    cfg.path_latency = setup.path_latency;
    cfg.stagger = setup.stagger;  // OPT2 t01
    // bkx1 t01 (b0123, BKX1_CADENCE_RE §2 [H]): the goal quota's N - H over the global PlayerList, as galactic
    {
        std::vector<ai::QuotaPlayer> qp;
        for (const galactic::PlayerState& ps : g.players()) qp.push_back({ps.faction, ps.human});
        const auto& factions = g.rules().factions;
        cfg.quota_players = ai::goal_quota_players(qp, factions.size(), [&factions](size_t f) { return std::string_view(factions[f].name); });
    }
    cfg.pf = host.parallel_for();  // OPT2 t03: goal candidates' perception values; OPT2 t10: slot-10 solves
    cfg.pp_streams = setup.pp_streams;  // OPT2 t10
    cfg.plan_retest = setup.plan_retest;  // skai t15
    cfg.pp_latency = setup.pp_latency;
    cfg.ai_lane = setup.ai_lane;  // sp3
    cfg.k_ai = setup.k_ai == 0 ? 1 : setup.k_ai;
    // A2 tactical (§3.2). trl2 t02: one source with the battle's land count (TacticalBattle::population_used reads the
    // catalog's GameConstants value).
    allow_reinforcement_fraction_ = b_->catalog_ ? b_->catalog_->allow_reinforcement_fraction() : setup.constants.allow_reinforcement_percentage;
    use_reinforcement_points_ = setup.constants.use_reinforcement_points;           // a1g t04 follow-up (land drops)
    // ref2 t05: the threat grid's box is the fog box (AI_TACTICAL_PERCEPTION §4.1): one source, TacticalBattle::map_extents
    const tactical::Aabb bb = b_->map_extents(mode, TacticalBattle::ExtentsUse::Perception);
    cfg.map = {bb.min.x, bb.min.y, bb.max.x, bb.max.y};
#ifdef ALAMO_AI_TACTICAL_HAVE_TERRAIN
    if (!b_->space_) zones_ = at::hint_zones_from_map(b_->map_);
    if (b_->space_) at::space_region_bounds(b_->map_, cfg.region_map);  // A1_READINGS §8: the camera box
#endif
    zone_locator_.build(zones_);
    cfg.zones = zones_;
    cfg.plan_files = b_->space_ ? setup.space_plans : setup.land_plans;
    const uint32_t players = host.players();
    ai_side_.assign(players, 0);
    for (uint32_t p = 0; p < players; ++p) {
        lua::LuaPlayerSetup ls;
        if (p < g.players().size()) {
            const galactic::PlayerState& ps = g.players()[p];
            if (ps.faction < g.rules().factions.size()) {
                ls.faction = g.rules().factions[ps.faction].name;
                ls.neutral = g.rules().factions[ps.faction].is_neutral;
            }
            ls.human = ps.human;
            ls.ai = ps.ai;
            ls.credits = g.credits(static_cast<int32_t>(p));
            const auto pi = static_cast<int32_t>(p);
            // skm2 t02: a skirmish map battle's slots 2.. take part too.
            const bool participant = pi == cfg.attacker || pi == cfg.defender || (b_->skirmish_map() && b_->skirmish_slot_of(pi) >= 0);
            const std::string type = !participant ? std::string() : setup.ai_type ? setup.ai_type(pi, g) : default_tactical_ai_type(pi, g);
            if (!type.empty()) {
                // skm2 t02 (SKIRMISH_SETUP §1: the slot's difficulty +0x158 -> the player's +0x3bc): the player's own entry,
                // else the setup's.
                const int32_t d = p < setup.player_difficulty.size() && setup.player_difficulty[p] >= 0 ? setup.player_difficulty[p] : setup.difficulty;
                cfg.players.push_back({pi, type, d});
                ai_side_[p] = 1;
            }
        }
        cfg.lua_players.push_back(ls);
    }
    {
        // skai t12: the PlayerList of the goal quota = the players plus one per faction without a player (the galactic
        // AI's count, galactic_ai.cpp quota_players; a faction defined twice under one name counts once [port]).
        const auto& factions = g.rules().factions;
        auto n = static_cast<int32_t>(players);
        for (size_t f = 0; f < factions.size(); ++f) {
            bool seen = false;
            for (size_t k = 0; k < f && !seen; ++k) seen = ascii_iequals(factions[k].name, factions[f].name);
            for (uint32_t p = 0; p < players && p < g.players().size(); ++p) seen = seen || g.players()[p].faction == f;
            n += seen ? 0 : 1;
        }
        cfg.player_list_size = n;
    }
    world_ = std::make_unique<at::TacticalAiWorld>(mode.primary(), &mode.types(), setup.files, host.random(), *setup.defs,
                                                   *setup.equations, *this, std::move(cfg));
    lua::ObjectBridge& br = world_->object_bridge();
    TacticalBattle* bt = b_;  // abilities / ability_frame: make_object_bridge below (ref3 t06)
    br.is_enemy = [this, bt](int32_t x, int32_t y) {  // sp3 slice 4e: the view with the lane on
        if (!lane_view()) return bt->combat().is_enemy(x, y);
        return x >= 0 && x < 256 && view_hostile(static_cast<uint8_t>(x), y);
    };
    // refixf t03: Find_All_Objects_Of_Type / Find_Nearest match TEAM containers on their company / team type.
    const objects::ObjectStore* rep_store = &mode.primary();
    br.representative_type = [this, bt, rep_store](objects::ObjectHandle o) {
        if (lane_view()) {  // sp3 slice 4e: the AI's handles are the snapshot store's
            const objects::ObjectStore& vs = world_->view_store();
            if (!vs.exists(o)) return objects::kNone;
            const objects::ObjectId id = vs.id(o);
            for (const LaneView::Team& t : view_.teams)
                if (t.id == id) return t.rep_type;
            return objects::kNone;
        }
        return rep_store->exists(o) ? bt->representative_type(rep_store->id(o)) : objects::kNone;
    };
    // mcx8 t12: Get_Game_Scoring_Type answers a TEAM container's company type (the lane: the captured view).
    br.team_company_type = [this, bt, rep_store](objects::ObjectHandle o) {
        if (lane_view()) {
            const objects::ObjectStore& vs = world_->view_store();
            if (!vs.exists(o)) return objects::kNone;
            const objects::ObjectId id = vs.id(o);
            for (const LaneView::Team& t : view_.teams)
                if (t.id == id) return t.company_type;
            return objects::kNone;
        }
        return rep_store->exists(o) ? bt->team_company_type(rep_store->id(o)) : objects::kNone;
    };
    // q6 t28: Find_Hint / Get_Hint see the map objects' .ted hint names.
    br.map_hint = [bt, rep_store](objects::ObjectHandle o) {
        return rep_store->exists(o) ? bt->map_hint(rep_store->id(o)) : std::string_view();
    };
    // s78i t02: the Reinforce_Unit stand-in faces the owner's entry-facing record (TACTICAL_COMBAT §3.1.1 "other spawns").
    br.reinforce_facing = [bt](int32_t player) { return bt->player_drop_facing(player); };
    br.order_finished = [this, bt](objects::ObjectId id, uint32_t) {
        if (lane_view()) {  // sp3 slice 4e
            const LaneView::Unit* u = view_unit(id);
            return !u || !u->mv || !u->has_goal;
        }
        return !bt->movement_->has_unit(id) || !bt->movement_->view(id).has_goal;
    };
    // OI follow-up t03 (A1_READINGS §28.9): this bridge's shares of the movement lock - Lock_Current_Orders, and the
    // locomotor's forced-move term while a Divert is the unit's current move (goal unchanged, not yet arrived).
    // sp3 slice 4e: with the lane on, from the facts published with the AI's batch (the bridge belongs to the AI lane).
    bt->set_script_movement_lock(this, [this, bt, &br](objects::ObjectId id) {
        const auto goal = [bt](objects::ObjectId u, Vec3& g) {
            if (!bt->movement_->has_unit(u)) return false;
            const auto v = bt->movement_->view(u);
            if (!v.has_goal || v.arrived) return false;
            g = Vec3{v.goal.x, v.goal.y, 0.0f};
            return true;
        };
        if (world_->ai_lane()) return lua::script_movement_locked(std::span<const lua::ScriptLockFact>(lane_locks_), id, goal);
        return lua::script_movement_locked(br, id, goal);
    });
    // STY1 t01: Prevent_All_Fire / Suspend_Locomotor / Set_Cannot_Be_Killed / Override_Max_Speed of the AI VMs' objects.
    // [port] With the AI lane on the bridge belongs to the lane thread and its facts are not published for these (the lock
    // facts are saved state; a new fact would change the lane save format): the setters are then stored only.
    bt->set_script_unit_control(this, [this, &br](objects::ObjectId id) {
        TacticalBattle::ScriptUnitControl c;
        if (world_->ai_lane()) return c;
        if (const lua::ScriptObjectState* s = br.find_state(id)) {
            c.no_fire = s->prevent_all_fire;
            c.loco_suspended = s->locomotor_suspended;
            c.cannot_die = s->cannot_be_killed;
            c.max_speed = s->max_speed_override;
            c.limbo = s->in_limbo;
            c.capture_disabled = s->capture_disabled;
        }
        return c;
    });

    // ref2 t04: the battle lifecycle of the AI VMs (Spawn_Unit, companies, Take_Damage, Change_Owner, Despawn) through the
    // battle, the same factory as the story VMs' bridge; before, only Change_Owner went through the battle (OI host) and the
    // rest wrote the bare store. [port] With the AI lane on the VMs' handles are the snapshot store's and their scripts run
    // off the critical lane, so every lifecycle call is refused there (no shipped AI script makes one).
    TacticalMode* owner_mode = &mode;
    make_object_bridge(*bt, [owner_mode] { return owner_mode; }, br, [this] { return !world_->ai_lane(); });
    br.garrison_spawn_changed = [bt, owner_mode](objects::ObjectHandle o, bool on) {  // Lua Set_Garrison_Spawn
        if (owner_mode->primary().alive(o)) bt->set_garrison_spawn(owner_mode->primary().id(o), on);
    };
    br.opportunity_fire_changed = [bt, owner_mode](objects::ObjectHandle o, bool on) {  // Lua Prevent_Opportunity_Fire
        if (owner_mode->primary().alive(o)) bt->script_opportunity_fire(owner_mode->primary().id(o), on);
    };
    // REMAINING_GAPS §4.2: movement threat mode reads the AI threat grid (point danger 0x653ff0) for the order's owner.
    const at::TacticalAiWorld* threat_world = world_.get();
    bt->movement_->set_threat_fn([threat_world](int32_t owner, mv::V2 p) { return threat_world->point_danger(owner, p.x, p.y); });
    // --- L1 bindings (script_remaining.hpp): Divert / Is_On_Diversion of the AI scripts (L1_S4_REMAINING §2.3) ---
    objects::ObjectStore* ast = &mode.primary();
    remaining_.move_to = [this, ast](objects::ObjectHandle u, Vec3 p) {
        if (!ast->alive(u)) return false;
        const objects::ObjectId id = ast->id(u);
        if (!move_unit(id, p.x, p.y)) return false;
        if (!b_->movement_locked(id)) b_->fighter_order(std::span<const objects::ObjectId>(&id, 1), 0, Vec3{p.x, p.y, 0.0f});  // S7 t22
        return true;
    };
    remaining_.in_hero_clash = [bt, ast](objects::ObjectHandle u) {  // s78n t06
        if (!ast->alive(u) || !bt->combat_->has_unit(ast->id(u))) return false;
        return (bt->combat_->flags(ast->id(u)) & tactical::combat::kUnitHeroClash) != 0;
    };
    remaining_.hero_clash_locked = [bt, ast](objects::ObjectHandle u) { return ast->alive(u) && bt->hero_clash_locked(ast->id(u)); };
    remaining_.release_hero_clash_lock = [bt, ast](objects::ObjectHandle u) {  // s78n t06 review: a TEAM Unlock
        return ast->alive(u) && bt->release_hero_clash_lock(ast->id(u));
    };
    remaining_.move_goal = [this, bt, ast](objects::ObjectHandle u, Vec3& goal) {
        if (lane_view()) {  // sp3 slice 4e
            const objects::ObjectStore& vs = world_->view_store();
            const LaneView::Unit* v = vs.alive(u) ? view_unit(vs.id(u)) : nullptr;
            if (!v || !v->mv || !v->has_goal) return false;
            goal = Vec3{v->gx, v->gy, 0.0f};
            return true;
        }
        if (!ast->alive(u) || !bt->movement_->has_unit(ast->id(u))) return false;
        const mv::UnitView v = bt->movement_->view(ast->id(u));
        if (!v.has_goal) return false;
        goal = Vec3{v.goal.x, v.goal.y, 0.0f};
        return true;
    };
    // --- Int-final: the battle's L1 systems answer the AI VMs too (as the story VMs' l1_hooks in campaign_battles.cpp):
    //     land strikes (Bombing_Run / Fire_Orbital_Bombardment, BombingBlockStatus, availability, PlayerWrapper switches),
    //     damage statistics, projectile queries, space fields, weather, base shields. Ids: the battle mode's store. ---
    {
        TacticalMode* md = &mode;
        SimHost* hs = &host;
        auto bid = [ast](objects::ObjectHandle o) { return ast->exists(o) ? ast->id(o) : objects::kNoObjectId; };
        lua::RemainingScriptHooks& r = remaining_;
        r.rate_of_damage = [bt, bid](objects::ObjectHandle u) { return bt->rate_of_damage(bid(u)); };
        r.time_till_dead = [bt, bid](objects::ObjectHandle u) { return bt->time_till_dead(bid(u)); };
        r.final_blow_player = [bt, bid](objects::ObjectHandle o) { return bt->final_blow_player(bid(o)); };
        r.projectile_types = [bt, bid](objects::ObjectHandle u) { return bt->projectile_types(bid(u)); };
        r.current_projectile = [bt, bid](objects::ObjectHandle u) { return bt->current_projectile(bid(u)); };
        r.should_switch_weapons = [bt, bid](objects::ObjectHandle u, objects::ObjectHandle t) {  // aitac t06
            return bt->should_switch_weapons(bid(u), t ? bid(t) : objects::kNoObjectId);
        };
        r.energy = [bt, bid](objects::ObjectHandle u) {
            const objects::ObjectId id = bid(u);
            return bt->combat().has_unit(id) ? bt->combat().energy(id) : -1.0f;
        };
        r.orbital_bombardment = [bt, md, hs](int32_t player, Vec3 p) { return bt->start_bombardment(*md, player, p, hs->random().synced()); };
        r.bombing_run = [bt, md](int32_t player, Vec3 p) { return bt->start_bombing_run(*md, player, p); };
        r.bombing_run_available = [bt](int32_t p) { return bt->bombing_run_available(p); };
        r.bombardment_available = [bt](int32_t p) { return bt->bombardment_available(p); };
        r.bombing_run_finished = [bt](int32_t p) { return bt->bombing_run_finished(p); };
        r.bombing_run_result = [bt](int32_t p) { return bt->bombing_run_result(p); };
        r.set_bombing_run_disabled = [bt](int32_t p, bool on) { bt->set_bombing_run_disabled(p, on); };
        r.set_bombardment_disabled = [bt](int32_t p, bool on) { bt->set_bombardment_disabled(p, on); };
        r.remove_bombardment = [bt](int32_t p, bool on) { bt->remove_bombardment(p, on); };
        r.force_weather = [bt, hs]() { return bt->force_weather(hs->random().synced()); };
        r.opposite_sides_of_shield = [bt](Vec3 a, Vec3 b, int32_t) { return bt->opposite_sides_of_shield(a, b, -1, true); };
        r.opposite_sides_of_shield4 = [bt](Vec3 a, Vec3 b, int32_t player, bool friendly) {
            return bt->opposite_sides_of_shield(a, b, player, friendly);
        };
        r.point_in_field = [bt](Vec3 p, uint32_t mask) { return (bt->field_kinds_at(p.x, p.y) & mask) != 0; };
        r.nearest_field = [bt, ast](Vec3 p, uint32_t mask) {
            const objects::ObjectId id = bt->nearest_field(p.x, p.y, mask);
            return id == objects::kNoObjectId ? objects::ObjectHandle{} : ast->find(id);
        };
        r.object_in_field = [bt, bid](objects::ObjectHandle o, uint32_t kind) { return bt->object_in_field(bid(o), kind); };
        r.build = [bt, md, bid](objects::ObjectHandle pad, objects::TypeIndex t, bool check) { return bt->pad_build(*md, bid(pad), t, check); };
        r.build_pad_contents = [bt, ast, bid](objects::ObjectHandle pad) {
            const objects::ObjectId id = bt->pad_contents(bid(pad));
            return id == objects::kNoObjectId ? objects::ObjectHandle{} : ast->find(id);
        };
        r.lock_build_pad = [bt, bid](objects::ObjectHandle pad, bool lock) { bt->pad_lock(bid(pad), lock); };
    }
    world_->set_global_values(host.global_values());  // the session-wide GlobalValue table (L1_S4_REMAINING §1.5)
    // --- end Int-final ---
    // --- battle host (L1 final list): the battle's L1 systems behind lua::TacticalScriptHooks for the AI VMs too (the
    //     story VMs' l1_hooks in campaign_battles.cpp): special weapons, TSW, engines, ground height, teleport, hyperspace
    //     out (Hyperspace_Away / Start_Cinematic_Space_Retreat), retreat increments, script spawns adopted by the battle. ---
    {
        TacticalMode* md = &mode;
        auto bid = [ast](objects::ObjectHandle o) { return ast->exists(o) ? ast->id(o) : objects::kNoObjectId; };
        lua::TacticalScriptHooks& k = tactical_;
        k.weapon_readiness = [bt, bid](objects::ObjectHandle w) { return bt->sw_readiness(bid(w)); };
        k.begin_targeting = [bt, bid](objects::ObjectHandle w) { return bt->sw_begin_targeting(bid(w)); };
        k.fire_at_object = [bt, md, bid](objects::ObjectHandle w, objects::ObjectHandle t) { return bt->sw_fire_at_object(*md, bid(w), bid(t)); };
        k.fire_lobbing_at = [bt, md, bid](objects::ObjectHandle w, Vec3 p) { return bt->sw_fire_lobbing_at(*md, bid(w), p); };
        k.special_weapons_allowed = [bt](int32_t player) { return bt->special_weapons_allowed(player); };
        k.tsw_state = [bt, bid](objects::ObjectHandle t) { return bt->tsw_state(bid(t)); };
        k.tsw_fire = [bt, bid](objects::ObjectHandle t) { return bt->tsw_fire(bid(t)); };
        k.engines_online = [bt, bid](objects::ObjectHandle u) { return bt->engines_online(bid(u)); };
        k.ground_height = [bt](float x, float y) { return bt->ground_height(x, y); };
        k.teleport = [bt, md, bid](objects::ObjectHandle u, Vec3 p, Vec3 f) { bt->teleport(*md, bid(u), p, f); };
        // aitac t03: Face_Immediate of an AI VM through the battle (movement facing, presentation), as the story VMs';
        // before, the binding set the bare store facing
        k.face_immediate = [bt, md, bid](objects::ObjectHandle u, Vec3 f) { bt->face_immediate(*md, bid(u), f); };
        k.movement_first_lock = [bt, md, bid](objects::ObjectHandle u) { bt->script_first_lock(*md, bid(u)); };  // cu8l t02
        k.hyperspace_out_link = [bt, bid](objects::ObjectHandle u, int32_t delay, bool keep) { return bt->hyperspace_out(bid(u), delay, keep); };
        k.retreat_increment_frames = [bt](int32_t player) { return bt->retreat_increment_frames(player); };
        k.set_new_environment = [bt](int32_t index) { bt->set_environment(index); };  // cu8g t03
        k.set_cinematic_environment = [bt](bool on) { bt->set_cinematic_environment(on); };  // ui2 t08
        k.spawn = [bt, md](const objects::SpawnParams& p) { return bt->script_spawn_effect(*md, p); };  // ref2 t04: the adopt step
#ifdef ALAMO_LUA_HAVE_FOG
        // refixf t03: Find_Nearest(obj, filter, player, bool) skips objects fogged for the player (0x35f470, force = 1).
        fog_hooks_.object_hidden = [bt, bid](objects::ObjectHandle o, int32_t player, bool force) {
            return bt->fog_object_hidden(player, bid(o), force);
        };
        // S7 t34: the AI VMs' FogOfWar.* (Reveal_All in BurnUnits' fire sale, Reveal, Temporary_Reveal) run on the
        // battle's fog, as the story VMs' (CampaignBattles::story_begin); before, `fog` stayed null and every call was a
        // "Command only valid in a tactical game" SCRIPT ERROR. Null when the battle runs without fog (BattleConfig::fog).
        // [port] the AI VMs' FOWCells handles are not in the save (none of the shipped AI scripts keeps one).
        bt->bind_script_fog(fog_hooks_);  // ref3 t06
        fog_hooks_.object = [bt, bid](objects::ObjectHandle o, tactical::fog::FogObject& out) { return bt->fog_object(bid(o), out); };
        k.fog = &fog_hooks_;
#endif
        br.tactical = &tactical_;
    }
    // --- end battle host ---
    br.remaining = &remaining_;
    // --- end L1 bindings ---
    // --- fog of war (owner: fog agent): FOW refog (a cell of a player's grid regrew to 0) -> threat-grid refog at
    // the FOW cell centre (FOG_OF_WAR §4.3, AI_TACTICAL_PERCEPTION §4.7). Cleared in the destructor. ---
    at::TacticalAiWorld* aw = world_.get();
    b_->set_fog_refog([aw](int32_t player, uint32_t, float x, float y) { aw->refog(player, x, y); });
    // S7 t31 (AI_TACTICAL §7.3.3 items 1-2): signal 0xb from every host (AI orders, human commands, story orders, Activate_Ability
    // of any VM) reaches this world's ability / movement blocks; this world's Activate_Ability raises it through the battle.
    at::TacticalAiWorld* ow = world_.get();
    order_listener_ = b_->add_order_replaced_listener([ow](objects::ObjectId id) { ow->order_replaced(static_cast<int32_t>(id)); });
    br.raise_order_replaced = [bt](objects::ObjectId id) { bt->raise_order_replaced(id); };
    // S7 t32 (AI_TACTICAL §7.3.3 part 6): Activate_Ability of ids 50 67 71 73 74 (and the port's JET_PACK point) on a
    // Dummy_Barrage_Target, as the story VMs (CampaignBattles::story_begin); before, the AI VMs sent the event instead.
    {
        TacticalMode* md = &mode;
        SimHost* hs = &host;
        br.spawn_ability_target = [bt, md, hs](Vec3 p, int32_t owner) { return bt->spawn_dummy_target(*md, owner, p, hs->random().synced()); };
    }
    if (world_->ai_lane()) wrap_lane_hooks();  // sp3 slice 4e (part 2)
    wire_facts();  // ref5 t07
}

// ---- sp3 slice 4e (part 2): the AI VMs' L1 hooks on the AI lane -----------------------------------------------------

namespace {
enum HostOp : uint8_t {
    kHostDivert = 64, kHostBuild, kHostLockPad, kHostBombard, kHostBombingRun, kHostBombingDisabled, kHostBombardDisabled,
    kHostRemoveBombard, kHostForceWeather, kHostSwBegin, kHostSwFire, kHostSwLob, kHostTswFire, kHostTeleport, kHostFirstLock,
    kHostHyperspaceOut, kHostSetEnvironment, kHostReleaseClash, kHostFogRevealAll, kHostFogTempReveal, kHostFogDisableRendering,
    kHostSetCinematicEnvironment
};
}

void BattleAi::wrap_lane_hooks() {
    live_remaining_ = remaining_;
    live_tactical_ = tactical_;
    lua::RemainingScriptHooks& r = remaining_;
    lua::TacticalScriptHooks& k = tactical_;
    // The AI VMs' handles are the snapshot store's: ids for the records and the queued writes.
    auto vid = [this](objects::ObjectHandle h) {
        const objects::ObjectStore& vs = world_->view_store();
        return vs.exists(h) ? vs.id(h) : objects::kNoObjectId;
    };
    auto queue = [this](uint8_t op, int32_t owner, std::vector<objects::ObjectId> ids, Vec3 p = {}, int32_t value = -1, bool on = false,
                        objects::ObjectId target = objects::kNoObjectId, Vec3 q = {}) {
        lua::LaneWrite w;
        w.op = static_cast<lua::LaneWriteOp>(op);
        w.owner = owner;
        w.ids = std::move(ids);
        w.p = p;
        w.q = q;
        w.value = value;
        w.on = on ? 1 : 0;
        w.target = target;
        queue_lane_write(w);
    };
    auto owner_of = [this](objects::ObjectId id) {
        const objects::ObjectStore& vs = world_->view_store();
        const objects::ObjectHandle h = vs.find(id);
        return vs.exists(h) ? vs.owner(h) : -1;
    };
    // ---- reads (ref5 t07: the fact tables l1_ ... answer live or from the capture, by lane_view()) ----
    {
        const TacticalBattle* bt = b_;
        l1_.set_rule([this](const objects::ObjectId& id) { return l1_fact(id); });
        final_blow_.set_rule([bt](const objects::ObjectId& id) { return bt->final_blow_player(id); });
        sw_allowed_.set_rule([bt](const int32_t& p) { return static_cast<uint8_t>(bt->special_weapons_allowed(p) ? 1 : 0); });
        bomb_avail_.set_rule([bt](const int32_t& p) { return static_cast<uint8_t>(bt->bombing_run_available(p) ? 1 : 0); });
        bombard_avail_.set_rule([bt](const int32_t& p) { return static_cast<uint8_t>(bt->bombardment_available(p) ? 1 : 0); });
        bomb_finished_.set_rule([bt](const int32_t& p) { return static_cast<uint8_t>(bt->bombing_run_finished(p) ? 1 : 0); });
        bomb_result_.set_rule([bt](const int32_t& p) { return bt->bombing_run_result(p); });
        retreat_inc_.set_rule([bt](const int32_t& p) { return bt->retreat_increment_frames(p); });
    }
    r.rate_of_damage = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).rate; };
    r.time_till_dead = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).ttd; };
    r.final_blow_player = [this, vid](objects::ObjectHandle o) { return final_blow_.get(lane_view(), vid(o)); };
    r.projectile_types = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).proj; };
    r.current_projectile = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).cur_proj; };
    // aitac t06 [port]: with the AI lane the answer is false (the combat state it reads is not in the lane view)
    r.should_switch_weapons = [this, f = r.should_switch_weapons](objects::ObjectHandle u, objects::ObjectHandle t) {
        return lane_view() ? int32_t{0} : f(u, t);
    };
    r.energy = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).energy; };
    r.in_hero_clash = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).clash != 0; };
    r.hero_clash_locked = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).clash_locked != 0; };
    r.bombing_run_available = [this](int32_t p) { return bomb_avail_.get(lane_view(), p) != 0; };
    r.bombardment_available = [this](int32_t p) { return bombard_avail_.get(lane_view(), p) != 0; };
    r.bombing_run_finished = [this](int32_t p) { return bomb_finished_.get(lane_view(), p) != 0; };
    r.bombing_run_result = [this](int32_t p) { return bomb_result_.get(lane_view(), p); };
    // point_in_field / nearest_field read the space fields, fixed at map setup; nearest_field's handle: the snapshot's
    r.nearest_field = [this, f = r.nearest_field](Vec3 p, uint32_t mask) {
        if (!lane_view()) return f(p, mask);
        const objects::ObjectId id = b_->nearest_field(p.x, p.y, mask);
        return id == objects::kNoObjectId ? objects::ObjectHandle{} : world_->view_store().find(id);
    };
    r.object_in_field = [this, vid, f = r.object_in_field](objects::ObjectHandle o, uint32_t kind) {
        if (!lane_view()) return f(o, kind);  // the battle's answer for every kind (l1_ holds the space fields only)
        const int i = kind == 1u ? 0 : kind == 2u ? 1 : kind == 4u ? 2 : kind == 8u ? 3 : -1;
        return i >= 0 ? static_cast<int32_t>(l1_.get(true, vid(o)).field[i]) : -1;
    };
    r.build_pad_contents = [this, vid, f = r.build_pad_contents](objects::ObjectHandle pad) {
        if (!lane_view()) return f(pad);
        const objects::ObjectId id = vid(pad);
        for (const LaneView::Pad& p : view_.pads)
            if (p.pad == id) return p.contents == objects::kNoObjectId ? objects::ObjectHandle{} : world_->view_store().find(p.contents);
        return objects::ObjectHandle{};
    };
    k.weapon_readiness = [this, vid, f = k.weapon_readiness](objects::ObjectHandle w) {
        return lane_view() && view_.sw ? view_.sw->readiness(vid(w), view_.frame) : f(w);
    };
    k.tsw_state = [this, vid, f = k.tsw_state](objects::ObjectHandle t) { return lane_view() && view_.sw ? view_.sw->tsw_state(vid(t)) : f(t); };
    k.special_weapons_allowed = [this](int32_t p) { return sw_allowed_.get(lane_view(), p) != 0; };
    k.engines_online = [this, vid](objects::ObjectHandle u) { return l1_.get(lane_view(), vid(u)).engines; };
    k.retreat_increment_frames = [this](int32_t p) { return retreat_inc_.get(lane_view(), p); };
    // ---- writes: queued (host ops), the same-call answer predicted from the view ----
    r.move_to = [this, vid, queue, owner_of, f = r.move_to](objects::ObjectHandle u, Vec3 p) {
        if (!lane_view()) return f(u, p);
        const objects::ObjectId id = vid(u);
        const LaneView::Unit* v = view_unit(id);
        queue(kHostDivert, owner_of(id), {id}, p);
        return v && v->live && v->movable;
    };
    r.build = [this, vid, queue, owner_of, f = r.build](objects::ObjectHandle pad, objects::TypeIndex t, bool check) {
        if (!lane_view()) return f(pad, t, check);
        const objects::ObjectId id = vid(pad);
        queue(kHostBuild, owner_of(id), {id}, {}, static_cast<int32_t>(t), check);
        for (const LaneView::Pad& p : view_.pads)
            if (p.pad == id) return p.contents == objects::kNoObjectId ? 1 : 0;
        return 0;
    };
    r.lock_build_pad = [this, vid, queue, owner_of, f = r.lock_build_pad](objects::ObjectHandle pad, bool lock) {
        if (!lane_view()) return f(pad, lock);
        queue(kHostLockPad, owner_of(vid(pad)), {vid(pad)}, {}, -1, lock);
    };
    r.orbital_bombardment = [this, queue, f = r.orbital_bombardment](int32_t player, Vec3 p) {
        if (!lane_view()) return f(player, p);
        queue(kHostBombard, player, {}, p);
        return bombard_avail_.get(true, player) != 0;
    };
    r.bombing_run = [this, queue, f = r.bombing_run](int32_t player, Vec3 p) {
        if (!lane_view()) return f(player, p);
        queue(kHostBombingRun, player, {}, p);
        return bomb_avail_.get(true, player) != 0;
    };
    r.set_bombing_run_disabled = [this, queue, f = r.set_bombing_run_disabled](int32_t p, bool on) {
        if (!lane_view()) return f(p, on);
        queue(kHostBombingDisabled, p, {}, {}, -1, on);
    };
    r.set_bombardment_disabled = [this, queue, f = r.set_bombardment_disabled](int32_t p, bool on) {
        if (!lane_view()) return f(p, on);
        queue(kHostBombardDisabled, p, {}, {}, -1, on);
    };
    r.remove_bombardment = [this, queue, f = r.remove_bombardment](int32_t p, bool on) {
        if (!lane_view()) return f(p, on);
        queue(kHostRemoveBombard, p, {}, {}, -1, on);
    };
    r.force_weather = [this, queue, f = r.force_weather]() {
        if (!lane_view()) return f();
        queue(kHostForceWeather, -1, {});
        return true;
    };
    r.release_hero_clash_lock = [this, vid, queue, owner_of, f = r.release_hero_clash_lock](objects::ObjectHandle u) {
        if (!lane_view()) return f(u);
        const objects::ObjectId id = vid(u);
        queue(kHostReleaseClash, owner_of(id), {id});
        return l1_.get(true, id).clash_locked != 0;
    };
    k.begin_targeting = [this, vid, queue, owner_of, f = k.begin_targeting](objects::ObjectHandle w) {
        if (!lane_view() || !view_.sw) return f(w);
        const objects::ObjectId id = vid(w);
        queue(kHostSwBegin, owner_of(id), {id});
        return view_.sw->begin_targeting(id, view_.frame);  // the copy's dry run (it also answers the next reads)
    };
    k.fire_at_object = [this, vid, queue, owner_of, f = k.fire_at_object](objects::ObjectHandle w, objects::ObjectHandle t) {
        if (!lane_view() || !view_.sw) return f(w, t);
        const objects::ObjectId id = vid(w), tid = vid(t);
        queue(kHostSwFire, owner_of(id), {id}, {}, -1, false, tid);
        return !b_->finished_ && tid != objects::kNoObjectId && tid != id && view_.sw->readiness(id, view_.frame) >= 1.0f;
    };
    k.fire_lobbing_at = [this, vid, queue, owner_of, f = k.fire_lobbing_at](objects::ObjectHandle w, Vec3 p) {
        if (!lane_view() || !view_.sw) return f(w, p);
        const objects::ObjectId id = vid(w);
        queue(kHostSwLob, owner_of(id), {id}, p);
        return !b_->finished_ && (view_.sw->kinds(id) & tactical::abilities::kSwLobbing) != 0 && owner_of(id) >= 0;
    };
    k.tsw_fire = [this, vid, queue, owner_of, f = k.tsw_fire](objects::ObjectHandle t) {
        if (!lane_view() || !view_.sw) return f(t);
        const objects::ObjectId id = vid(t);
        queue(kHostTswFire, owner_of(id), {id});
        return view_.sw->tsw_fire(id, view_.tsw_planet_ok, view_.frame);  // dry run on the copy
    };
    k.teleport = [this, vid, queue, owner_of, f = k.teleport](objects::ObjectHandle u, Vec3 p, Vec3 facing) {
        if (!lane_view()) return f(u, p, facing);
        queue(kHostTeleport, owner_of(vid(u)), {vid(u)}, p, -1, false, objects::kNoObjectId, facing);
    };
    k.movement_first_lock = [this, vid, queue, owner_of, f = k.movement_first_lock](objects::ObjectHandle u) {
        if (!lane_view()) return f(u);
        queue(kHostFirstLock, owner_of(vid(u)), {vid(u)});
    };
    k.hyperspace_out_link = [this, vid, queue, owner_of, f = k.hyperspace_out_link](objects::ObjectHandle u, int32_t delay, bool keep) {
        if (!lane_view()) return f(u, delay, keep);
        const objects::ObjectId id = vid(u);
        queue(kHostHyperspaceOut, owner_of(id), {id}, {}, delay, keep);
        const LaneView::Unit* v = view_unit(id);
        return v && v->live;
    };
    k.set_new_environment = [this, queue, f = k.set_new_environment](int32_t index) {
        if (!lane_view()) return f(index);
        queue(kHostSetEnvironment, -1, {}, {}, index);
    };
    k.set_cinematic_environment = [this, queue, f = k.set_cinematic_environment](bool on) {  // ui2 t08
        if (!lane_view()) return f(on);
        queue(kHostSetCinematicEnvironment, -1, {}, {}, 0, on);
    };
#ifdef ALAMO_LUA_HAVE_FOG
    // slice 4e (part 3): the fogged test from the capture; Reveal_All / Temporary_Reveal / Disable_Rendering queued
    fog_hooks_.object_hidden = [this, vid, f = fog_hooks_.object_hidden](objects::ObjectHandle o, int32_t player, bool force) {
        if (!lane_view()) return f ? f(o, player, force) : false;
        const auto pk = std::find(view_.fog_players.begin(), view_.fog_players.end(), player);
        if (pk == view_.fog_players.end()) return false;
        const objects::ObjectId id = vid(o);
        const auto& v = view_.fog_hidden;
        const auto it = std::lower_bound(v.begin(), v.end(), id, [](const std::pair<objects::ObjectId, uint32_t>& a, objects::ObjectId x) { return a.first < x; });
        if (!force) return false;  // [port] not captured: the bindings ask with force = 1 only (Find_Nearest)
        const auto bit = static_cast<uint32_t>(pk - view_.fog_players.begin());
        return it != v.end() && it->first == id && ((it->second >> bit) & 1u) != 0;
    };
    fog_hooks_.lane_write = [this, vid](uint8_t op, int32_t player, objects::ObjectHandle o, float value) {
        if (!lane_view()) return;  // (the binding acts on the fog itself without a lane_write; never reached)
        const uint8_t host = op == 1 ? kHostFogRevealAll : op == 2 ? kHostFogTempReveal : kHostFogDisableRendering;
        lua::LaneWrite w;
        w.op = static_cast<lua::LaneWriteOp>(host);
        w.owner = player;
        if (op == 2) w.ids = {vid(o)};
        w.seconds = value;
        queue_lane_write(w);
    };
#endif
    // A spawned object is used at once (its handle): not available off the critical lane (no shipped AI script spawns).
    k.spawn = [this, f = k.spawn](const objects::SpawnParams& p) {
        if (!lane_view()) return f(p);
        ++lane_refused_spawns_;
        return objects::ObjectHandle{};
    };
}

// ref5 t07: the l1_ rule - one battle unit or container's L1 answers (const battle reads: the capture runs it in parallel).
BattleAi::L1Fact BattleAi::l1_fact(objects::ObjectId id) const {
    const TacticalBattle* bt = b_;
    L1Fact o;
    o.rate = bt->rate_of_damage(id);
    o.ttd = bt->time_till_dead(id);
    o.cur_proj = bt->current_projectile(id);
    o.engines = bt->engines_online(id);
    o.energy = bt->combat().has_unit(id) ? bt->combat().energy(id) : -1.0f;
    o.clash = bt->combat().has_unit(id) && (bt->combat().flags(id) & tactical::combat::kUnitHeroClash) != 0 ? 1 : 0;
    o.clash_locked = bt->hero_clash_locked(id) ? 1 : 0;
    if (bt->space_)
        for (int k = 0; k < 4; ++k) o.field[k] = static_cast<int8_t>(bt->object_in_field(id, 1u << k));
    o.proj = bt->projectile_types(id);
    return o;
}

void BattleAi::capture_l1() {
    ALAMO_PROFILE_SCOPE("tac.cap.l1");  // wall clock only
    LaneView& v = view_;
    std::vector<objects::ObjectId> ids;
    for (const TacticalBattle::Unit& u : b_->units_) ids.push_back(u.id);
    for (const TacticalBattle::Container& c : b_->containers_) ids.push_back(c.id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    // slice 4f: the l1_ rule per object in parallel (const battle reads, per-index outputs); ref5 t07: as fact tables
    l1_.capture(std::move(ids), host_->parallel_for(), 16);
    {
        std::vector<objects::ObjectId> blows;
        for (const auto& [id, player] : b_->l1_final_blow_) blows.push_back(id);
        final_blow_.capture(std::move(blows));
    }
    {
        std::vector<int32_t> all(static_cast<size_t>(host_->players()));
        for (size_t p = 0; p < all.size(); ++p) all[p] = static_cast<int32_t>(p);
        sw_allowed_.capture(all);
        bomb_avail_.capture(all);
        bombard_avail_.capture(all);
        bomb_finished_.capture(all);
        bomb_result_.capture(all);
        retreat_inc_.capture(all);
    }
    v.sw = b_->l1_sw_;
    const TacticalBattle* bt = b_;
    ALAMO_PROFILE_SCOPE("tac.cap.fog");  // wall clock only
    // slice 4e (part 3): the AI players' fogged test of every object (force 1)
    v.fog_players.clear();
    v.fog_hidden.clear();
    if (b_->fog()) {
        for (size_t p = 0; p < ai_side_.size(); ++p)
            if (ai_side_[p]) v.fog_players.push_back(static_cast<int32_t>(p));
        const objects::ObjectStore& st = mode_->primary();
        std::vector<objects::ObjectId> fids;
        for (const objects::ObjectHandle h : st.all_objects())
            if (st.alive(h)) fids.push_back(st.id(h));
        std::vector<uint32_t> bits(fids.size(), 0);  // slice 4f: in parallel, per-index outputs
        host_->parallel_for()(static_cast<uint32_t>(fids.size()), 64, [&](uint32_t begin, uint32_t end) {
            for (uint32_t i = begin; i < end; ++i)
                for (size_t k = 0; k < v.fog_players.size(); ++k)
                    if (bt->fog_object_hidden(v.fog_players[k], fids[i], true)) bits[i] |= 1u << k;  // the bindings: force only
        });
        for (size_t i = 0; i < fids.size(); ++i)
            if (bits[i]) v.fog_hidden.emplace_back(fids[i], bits[i]);
        std::sort(v.fog_hidden.begin(), v.fog_hidden.end());
    }
    // slice 4f: the perception's point test (FogOfWar::point_fogged with force 1) on copies of the AI players' grids
    ALAMO_PROFILE_SCOPE("tac.cap.fog_grids");  // wall clock only
    const tactical::fog::FogOfWar* fog = b_->fog();
    v.fog_live = fog && fog->started() && fog->fow_active();
    v.fog_grids.resize(v.fog_live ? v.fog_players.size() : 0);  // copied into the kept buffers
    for (size_t k = 0; k < v.fog_grids.size(); ++k) {
        const tactical::fog::FogGrid* g = fog->grid(v.fog_players[k]);
        if (g) v.fog_grids[k] = *g;
        else v.fog_grids[k] = tactical::fog::FogGrid{};
    }
    // TacticalBattle::tsw_fire's planet gate (0x42a720)
    v.tsw_planet_ok = false;
    const auto planet = b_->req_.info.planet;
    if (!b_->finished_ && !b_->l1_planet_destroyed_ && planet < b_->galaxy_->planets().size() && !b_->galaxy_->planets()[planet].destroyed) {
        const uint32_t gt = b_->galaxy_->planets()[planet].type;
        if (gt < b_->galaxy_->rules().types.size()) {
            const objects::TypeIndex t = b_->catalog_->types().resolve_name(b_->galaxy_->rules().types[gt].name);
            v.tsw_planet_ok = t != objects::kNone && b_->catalog_->types().get(t, objects::tags::Planet_Is_Destroyable_By_TSW);
        }
    }
}

// The hook calls the AI VM made, at the due tick, on the live battle (handles from the live store).
void BattleAi::apply_host_write(const lua::LaneWrite& w) {
    const objects::ObjectStore& st = mode_->primary();
    const objects::ObjectId id = w.ids.empty() ? objects::kNoObjectId : w.ids.front();
    const objects::ObjectHandle h = id == objects::kNoObjectId ? objects::ObjectHandle{} : st.find(id);
    const lua::RemainingScriptHooks& r = live_remaining_;
    const lua::TacticalScriptHooks& k = live_tactical_;
    switch (static_cast<uint8_t>(w.op)) {
        case kHostDivert: if (r.move_to) r.move_to(h, w.p); break;
        case kHostBuild: if (r.build) r.build(h, static_cast<objects::TypeIndex>(w.value), w.on != 0); break;
        case kHostLockPad: if (r.lock_build_pad) r.lock_build_pad(h, w.on != 0); break;
        case kHostBombard: if (r.orbital_bombardment) r.orbital_bombardment(w.owner, w.p); break;
        case kHostBombingRun: if (r.bombing_run) r.bombing_run(w.owner, w.p); break;
        case kHostBombingDisabled: if (r.set_bombing_run_disabled) r.set_bombing_run_disabled(w.owner, w.on != 0); break;
        case kHostBombardDisabled: if (r.set_bombardment_disabled) r.set_bombardment_disabled(w.owner, w.on != 0); break;
        case kHostRemoveBombard: if (r.remove_bombardment) r.remove_bombardment(w.owner, w.on != 0); break;
        case kHostForceWeather: if (r.force_weather) r.force_weather(); break;
        case kHostReleaseClash: if (r.release_hero_clash_lock) r.release_hero_clash_lock(h); break;
        case kHostSwBegin: if (k.begin_targeting) k.begin_targeting(h); break;
        case kHostSwFire:
            if (k.fire_at_object) k.fire_at_object(h, w.target == objects::kNoObjectId ? objects::ObjectHandle{} : st.find(w.target));
            break;
        case kHostSwLob: if (k.fire_lobbing_at) k.fire_lobbing_at(h, w.p); break;
        case kHostTswFire: if (k.tsw_fire) k.tsw_fire(h); break;
        case kHostTeleport: if (k.teleport) k.teleport(h, w.p, w.q); break;
        case kHostFirstLock: if (k.movement_first_lock) k.movement_first_lock(h); break;
        case kHostHyperspaceOut: if (k.hyperspace_out_link) k.hyperspace_out_link(h, w.value, w.on != 0); break;
        case kHostSetEnvironment: if (k.set_new_environment) k.set_new_environment(w.value); break;
        case kHostSetCinematicEnvironment: if (k.set_cinematic_environment) k.set_cinematic_environment(w.on != 0); break;
        case kHostFogRevealAll:
            if (tactical::fog::FogOfWar* fog = b_->fog_rw()) fog->reveal_all(w.owner);
            break;
        case kHostFogTempReveal:
            if (tactical::fog::FogOfWar* fog = b_->fog_rw()) {
                tactical::fog::FogObject o;
                if (b_->fog_object(id, o)) fog->temporary_reveal(w.owner, o, w.seconds);
            }
            break;
        case kHostFogDisableRendering:
            if (tactical::fog::FogOfWar* fog = b_->fog_rw()) fog->disable_rendering(w.seconds != 0.0f);
            break;
        default: break;
    }
}

BattleAi::~BattleAi() {
    b_->remove_order_replaced_listener(order_listener_);
    if (b_->movement_) b_->movement_->set_threat_fn({});  // the battle outlives its AI
    b_->set_script_movement_lock(this, {});
    b_->set_script_unit_control(this, {});
    b_->set_fog_refog({});  // fog: the battle outlives its AI (CampaignBattles resets ai_ first)
}

namespace {
// S9 hard points (AI_TACTICAL_PERCEPTION sec. 4) of combat unit `id` for the perception: kind, weapon range, health; power =
// the battle unit's AI_Combat_Power (`unit_power`, 0 without a battle unit) shared by its weapon hard points [port M].
void combat_hard_points(const tc::CombatSystem& cs, int32_t id, float unit_power, std::vector<at::PerceptionHardPoint>& hps) {
    hps.clear();
    if (!cs.has_unit(id)) return;
    const uint32_t n = cs.hardpoint_count(id);
    const tc::UnitDesc& ud = cs.data().units[cs.desc_index(id)];
    uint32_t weapons = 0;
    for (uint32_t k = 0; k < n; ++k) weapons += tc::is_weapon(cs.hardpoint_kind(id, k)) ? 1u : 0u;
    const float power = weapons > 0 ? unit_power / static_cast<float>(weapons) : 0.0f;
    for (uint32_t k = 0; k < n; ++k) {
        at::PerceptionHardPoint hp;
        hp.type = static_cast<int32_t>(cs.hardpoint_kind(id, k));
        const uint32_t hd = k < ud.hardpoints.size() ? ud.hardpoints[k] : 0xFFFFFFFFu;
        hp.weapon_range = hd < cs.data().hardpoints.size() ? cs.data().hardpoints[hd].fire_range : 0.0f;
        hp.power = power;
        const float mx = cs.hardpoint_max(id, k);
        hp.health = mx > 0.0f ? cs.hardpoint_health(id, k) / mx : 1.0f;
        hp.destroyable = true;
        hp.destroyed = cs.hardpoint_destroyed(id, k);
        hps.push_back(hp);
    }
}
}  // namespace

bool BattleAi::start(std::vector<std::string>* diag) {
    if (!world_->load_plans(diag) && diag) diag->push_back("tactical AI: no plan loaded");
    if (!world_->ai_lane()) {
        const bool ok = world_->start(diag);
        set_build_time_multipliers();
        return ok;
    }
    // sp3 slice 4c: the start scripts (critical lane, before the first AI slot) read a view captured now, and what they
    // queue is collected at once (no job is pending yet: the first update would otherwise drop it).
    capture_lane_view();
    set_thinking(true);  // ref5 t06: the start scripts read only the view
    const bool ok = world_->start(diag);
    set_thinking(false);
    set_build_time_multipliers();
    queue_global_writes();  // slice 4e (part 3)
    for (cmd::Command& c : lane_job_.batch) lane_batch_.push_back(std::move(c));
    lane_job_.batch.clear();
    lane_locks_ = world_->object_bridge().script_lock_facts();  // slice 4e
    return ok;
}

// skm1 t04 (SKIRMISH_SETUP 0x496f00): the tactical build queue's AI difficulty factor, for the non-human players the world
// created an AI for (TacticalAiWorld::build_time_multiplier); everyone else 1.0.
void BattleAi::set_build_time_multipliers() {
    const auto& gps = b_->galaxy_->players();
    for (uint32_t p = 0; p < host_->players(); ++p) {
        const auto pi = static_cast<int32_t>(p);
        b_->set_ai_build_time_multiplier(pi, p < gps.size() && !gps[p].human && controls(pi) ? world_->build_time_multiplier(pi) : 1.0f);
    }
}

bool BattleAi::controls(int32_t player) const {
    return player >= 0 && static_cast<size_t>(player) < ai_side_.size() && ai_side_[static_cast<size_t>(player)] && world_->is_ai_player(player);
}

void BattleAi::update() {
    if (b_->finished_) return;
    follow_service();
    if (world_->ai_lane()) {  // sp3 t02
        lane_collect();
        capture_lane_view();
        world_->set_lane_global_overlay(global_overlay());  // slice 4e (part 3)
        world_->publish(static_cast<int64_t>(host_->frame()), b_->frame_);
        lane_job_.pending = true;
        lane_job_.frame = host_->frame();
        lane_job_.batch.clear();
        set_thinking(true);  // ref5 t06: from here to the end of think() no host answer reads the battle
        world_->think();  // inline: the AI lane's job (t03: on the AI thread)
        set_thinking(false);
        lane_job_.locks = world_->object_bridge().script_lock_facts();  // slice 4e: published at collect
        queue_global_writes();
        StateHasher h;
        world_->hash(h);
        lane_job_.digest = h.digest();
    } else {
        world_->update(static_cast<int64_t>(host_->frame()), b_->frame_);
    }
    standin_orders();
}

// sp3 t02: the TacticalAiHost answers of this AI slot (critical lane; the overrides read the battle while `valid` is false).
void BattleAi::capture_lane_view() {
    ALAMO_PROFILE_SCOPE("tac.ai.capture");  // sp3: wall clock only
    profile::Steps steps;
    view_.valid = false;
    ensure_type_facts();
    const auto players = static_cast<int32_t>(host_->players());
    const auto np = static_cast<size_t>(players);
    view_.players = players;
    view_.enemy.assign(np * np, 0);
    // Pads (build_pads / build_pad_check run on the AI lane from these and the snapshot store): pad states, relations of
    // the galactic players, their factions, the live battle units.
    view_.frame = b_->frame_;
    view_.pads.clear();
    for (const TacticalBattle::PadState& p : b_->pads_)
        view_.pads.push_back({p.pad, p.built != objects::kNoObjectId ? p.built : p.uc, p.cooldown_end});
    const auto& gps = b_->galaxy_->players();
    view_.allied_n = static_cast<int32_t>(gps.size());
    view_.allied.assign(gps.size() * gps.size(), 0);
    view_.faction.assign(gps.size(), std::string());
    for (int32_t p = 0; p < view_.allied_n; ++p) {
        for (int32_t q = 0; q < view_.allied_n; ++q)
            view_.allied[static_cast<size_t>(p * view_.allied_n + q)] = b_->bt_allied(p, q) ? 1 : 0;
        if (gps[static_cast<size_t>(p)].faction < b_->galaxy_->rules().factions.size())
            view_.faction[static_cast<size_t>(p)] = b_->galaxy_->rules().factions[gps[static_cast<size_t>(p)].faction].name;
    }
    view_.live_units.clear();
    for (const TacticalBattle::Unit& u : b_->units_)
        if (u.alive && !u.retreated) view_.live_units.push_back(u.id);
    std::sort(view_.live_units.begin(), view_.live_units.end());
    ALAMO_PROFILE_STEP(steps, "tac.cap.pads_scan");
    // ref5 t07: the per-player fact tables, from their rules (every host player)
    {
        std::vector<int32_t> all(np);
        for (int32_t p = 0; p < players; ++p) all[static_cast<size_t>(p)] = p;
        facts_.pool.capture(all);
        facts_.reinforce_ok.capture(all);
        facts_.pop_cap.capture(all);
        facts_.pop_used.capture(all);
        facts_.drop_frame.capture(all);
    }
    for (int32_t p = 0; p < players; ++p) {
        const auto pi = static_cast<size_t>(p);
        for (int32_t q = 0; q < players; ++q) view_.enemy[pi * np + static_cast<size_t>(q)] = b_->combat_->is_enemy(p, q) ? 1 : 0;
    }
    ALAMO_PROFILE_STEP(steps, "tac.cap.players");
    {  // movement locks: the live battle units and containers (an object not listed reads unlocked)
        std::vector<int32_t> ids;
        for (const TacticalBattle::Unit& u : b_->units_)
            if (u.alive && !u.retreated) ids.push_back(u.id);
        for (const TacticalBattle::Container& c : b_->containers_)
            if (!c.removed) ids.push_back(c.id);
        facts_.locked.capture(std::move(ids));
    }
    ALAMO_PROFILE_STEP(steps, "tac.cap.locked");
    {  // timed landings still unloading (owner, key); the AI lane waits for every drop's record anyway
        std::vector<std::pair<int32_t, int32_t>> keys;
        for (const TacticalBattle::Landing& l : b_->landings_) {
            const TacticalBattle::Participant& p = b_->parts_[l.participant];
            if (l.timed && l.state != 3) keys.emplace_back(p.owner, static_cast<int32_t>(p.galactic_id));
        }
        facts_.in_flight.capture(std::move(keys));
    }
    // Weather sight factors by Weather_Category (the categories of the types, resolved once).
    resolve_weather_categories();
    view_.weather_sight.assign(weather_categories_.size(), 1.0f);
    if (!b_->space_)
        for (size_t c = 0; c < weather_categories_.size(); ++c) view_.weather_sight[c] = b_->weather().sight_modifier(weather_categories_[c]);
    ALAMO_PROFILE_STEP(steps, "tac.cap.weather");
    view_.damage = b_->damage_tracking_;
    facts_.damage_copy = &view_.damage;  // ref5 t07
    ALAMO_PROFILE_STEP(steps, "tac.cap.damage");
    {
        const objects::ObjectStore& st = mode_->primary();
        view_.units.resize(b_->units_.size());
        const TacticalBattle* bt = b_;
        // slice 4f: per unit in parallel (const reads, per-index outputs)
        host_->parallel_for()(static_cast<uint32_t>(b_->units_.size()), 32, [&](uint32_t begin, uint32_t end) {
            for (uint32_t i = begin; i < end; ++i) {
                const TacticalBattle::Unit& u = bt->units_[i];
                LaneView::Unit& v = view_.units[i];
                v = {};
                v.id = u.id;
                v.owner = st.owner(u.h);
                v.container = u.container;
                v.live = static_cast<uint8_t>(u.alive && !u.retreated ? 1 : 0);
                v.movable = static_cast<uint8_t>(u.movable ? 1 : 0);
                if (bt->movement_ && bt->movement_->has_unit(u.id)) {  // slice 4e
                    const mv::UnitView m = bt->movement_->view(u.id);
                    v.mv = 1;
                    v.has_goal = m.has_goal ? 1 : 0;
                    v.arrived = m.arrived ? 1 : 0;
                    v.gx = m.goal.x;
                    v.gy = m.goal.y;
                    if (!bt->space_) v.mask = bt->movement_->mask_of(u.id);  // ref5 t06: the attack-position line test
                }
            }
        });
        view_.teams.resize(b_->containers_.size());
        for (size_t i = 0; i < b_->containers_.size(); ++i) {
            const TacticalBattle::Container& c = b_->containers_[i];
            view_.teams[i] = {c.id, static_cast<uint8_t>(c.removed ? 1 : 0), static_cast<uint8_t>(c.team && !c.removed && c.alive > 0 ? 1 : 0),
                              c.team && c.type != 0xFFFFFFFFu ? static_cast<objects::TypeIndex>(c.type) : objects::kNone,
                              c.team && c.squad_type != 0xFFFFFFFFu ? static_cast<objects::TypeIndex>(c.squad_type) : objects::kNone};
        }
        view_.retreat_ok.assign(np, 0);
        for (int32_t p = 0; p < players; ++p) view_.retreat_ok[static_cast<size_t>(p)] = b_->retreat_block(p) == 0 ? 1 : 0;
        view_.production = b_->production_enabled();
    }
    if (world_->ai_lane()) capture_l1();  // slice 4e (part 2)
    ALAMO_PROFILE_STEP(steps, "tac.cap.units");
    capture_lane_abilities();
    ALAMO_PROFILE_STEP(steps, "tac.cap.abilities");
    capture_lane_rest();
    ALAMO_PROFILE_STEP(steps, "tac.cap.rest");
    view_.valid = true;
    facts_.captured();  // ref5 t07
}

// sp3 slice 4b: one record per object of the ability system or the combat system (ascending id, merged), the answers of
// the bindings' queries at the ability frame. The bridge reads them from here on.
void BattleAi::capture_lane_abilities() {
    lua::capture_lane_abilities(b_->abilities(), b_->frame(), lane_abilities_, host_->parallel_for());
    lua::ObjectBridge& br = world_->object_bridge();
    br.lane_abilities = &lane_abilities_;
    br.lane_write = [this](const lua::LaneWrite& w) { queue_lane_write(w); };
}

void BattleAi::queue_lane_write(const lua::LaneWrite& w) {
    cmd::Command c;
    // the batch's player (drain order; a decodable slot): the write's owner, else 0
    c.header.player = static_cast<uint16_t>(w.owner >= 0 && w.owner < static_cast<int32_t>(kMaxPlayers) ? w.owner : 0);  // uint8_t sim ids
    c.header.source = cmd::CommandSource::AI;
    c.header.sequence = lane_seq_++;
    c.header.issue_tick = host_->frame();
    c.header.scheduled_tick = host_->frame() + world_->k_ai();
    c.payload = cmd::ev::port_extension(lua::kLaneWriteSubtype, lua::encode_lane_write(w));
    lane_job_.batch.push_back(std::move(c));  // visible when the job is collected
}

namespace {
constexpr uint16_t kLaneGlobalSubtype = 0x4147;  // "AG": an AI GlobalValue.Set (name, value)
void put_global(ByteWriter& w, const lua::AiGlobalValue& v) {
    w.u8(static_cast<uint8_t>(v.kind)).f64(v.number).str(v.text).u32(static_cast<uint32_t>(v.keys.size()));
    for (size_t i = 0; i < v.keys.size(); ++i) {
        put_global(w, v.keys[i]);
        put_global(w, i < v.values.size() ? v.values[i] : lua::AiGlobalValue{});
    }
}
bool get_global(ByteReader& r, lua::AiGlobalValue& v, int depth = 0) {
    const uint8_t kind = r.u8();
    if (kind > lua::AiGlobalValue::Table || depth > 64) return false;
    v.kind = static_cast<lua::AiGlobalValue::Kind>(kind);
    v.number = r.f64();
    v.text = r.str();
    uint32_t n = 0;
    if (!r.count(n, 26)) return false;
    v.keys.resize(n);
    v.values.resize(n);
    for (uint32_t i = 0; i < n; ++i)
        if (!get_global(r, v.keys[i], depth + 1) || !get_global(r, v.values[i], depth + 1)) return false;
    return r.ok();
}
bool decode_global(const cmd::EventData& ev, std::string& name, lua::AiGlobalValue& v) {
    ByteReader r(ev.bytes_view("data"));
    name = r.str();
    return get_global(r, v) && r.ok() && r.done();
}
}  // namespace

void BattleAi::queue_global_writes() {
    for (auto& [name, v] : world_->take_lane_global_writes()) {
        ByteWriter w;
        w.str(name);
        put_global(w, v);
        cmd::Command c;
        c.header.player = 0;  // no player of its own (a decodable slot)
        c.header.source = cmd::CommandSource::AI;
        c.header.sequence = lane_seq_++;
        c.header.issue_tick = host_->frame();
        c.header.scheduled_tick = host_->frame() + world_->k_ai();
        c.payload = cmd::ev::port_extension(kLaneGlobalSubtype, w.bytes());
        lane_job_.batch.push_back(std::move(c));
    }
}

std::vector<ai::tactical::TacticalAiWorld::GlobalWrite> BattleAi::global_overlay() const {
    std::vector<ai::tactical::TacticalAiWorld::GlobalWrite> out;
    for (const cmd::Command& c : lane_batch_) {
        if (c.payload.type() != cmd::EventType::PortExtension || c.payload.u("subtype") != kLaneGlobalSubtype) continue;
        ai::tactical::TacticalAiWorld::GlobalWrite g;
        if (decode_global(c.payload, g.first, g.second)) out.push_back(std::move(g));
    }
    return out;
}

bool BattleAi::lane_write_command(const cmd::EventData& ev) {
    return ev.type() == cmd::EventType::PortExtension && ev.u("subtype") == lua::kLaneWriteSubtype;
}

// The calls the binding makes on the critical lane (bind_objects.cpp AbilityAccess), at the battle frame of the apply.
void BattleAi::apply_lane_write(const cmd::Command& c) {
    lua::LaneWrite w;
    if (b_->finished_ || !lua::decode_lane_write(c.payload.bytes_view("data"), w)) return;
    if (static_cast<uint8_t>(w.op) >= static_cast<uint8_t>(lua::LaneWriteOp::HostFirst)) return apply_host_write(w);  // slice 4e
    const int32_t n = lua::apply_lane_write(*b_->abilities_, b_->frame(), w, world_->object_bridge());
    if (n >= 0 && n != w.value) ++lane_mismatch_;
}

void BattleAi::lane_collect() {
    if (!lane_job_.pending) return;
    lane_job_.pending = false;
    for (cmd::Command& c : lane_job_.batch) lane_batch_.push_back(std::move(c));
    lane_job_.batch.clear();
    lane_digest_ = lane_job_.digest;
    lane_locks_ = std::move(lane_job_.locks);  // slice 4e
    lane_job_.locks.clear();
}

void BattleAi::end() { world_->end_battle(); }

bool BattleAi::set_difficulty(int32_t player, int32_t difficulty) {
    if (!controls(player) || !world_->set_difficulty(player, difficulty)) return false;
    set_build_time_multipliers();
    return true;
}

std::string BattleAi::summary() const {
    return "tactical AI: " + std::to_string(world_->orders_issued()) + " orders, " + std::to_string(world_->orders_rejected()) +
           " rejected, " + std::to_string(world_->decision_log().size()) + " decisions, " + std::to_string(world_->lua_errors().size()) +
           " Lua errors; executed" + [this] {
               std::string s;
               for (const auto& [k, v] : executed_) s += " " + k + " " + std::to_string(v.first + v.second);
               if (stale_) s += " (" + std::to_string(stale_) + " no-ops on dead targets)";
               return s;
           }();
}

// ---- N1 stage 2 save / load (owner: n2-host): chunk "battle.ai" ---------------------------------------------------
namespace {
// 2: UnitAI re-projection stamps; 3: a1h t02 object-goal follow records; 4: s78n t25.4 ObjectFollow::guard
// mcx6 t06: 7 (lane off) / 8 (lane on) = 4 / 6 + the UnitAI attack-positioning stamps (unit_ai_second_) after the follow
// records; 4 / 5 / 6 still load (the second stamp then reads the first's value [port]).
constexpr uint32_t kBattleAiSaveVersion4 = 4;
constexpr uint32_t kBattleAiLaneSaveVersion5 = 5;  // sp3 t04: version 4 + the AI lane's queued orders (lane on only)
constexpr uint32_t kBattleAiLaneSaveVersion6 = 6;  // sp3 slice 4e: version 5 + the movement-lock facts (published, job)
constexpr uint32_t kBattleAiSaveVersion = 7;
constexpr uint32_t kBattleAiLaneSaveVersion = 8;
bool get_locks(ByteReader& r, std::vector<lua::ScriptLockFact>& v) {
    uint32_t n = 0;
    if (!r.count(n, 18)) return false;
    v.resize(n);
    for (lua::ScriptLockFact& f : v) {
        f.id = r.i32();
        f.locked = r.u8();
        f.diverted = r.u8();
        f.goal = Vec3{r.f32(), 0.0f, 0.0f};
        f.goal.y = r.f32();
        f.goal.z = r.f32();
    }
    return r.ok();
}
template <class A, class F>
void follow_fields(A& a, F& f) {
    a(f.units, f.target, f.gx, f.gy, f.r, f.threat, f.next, f.guard);
}
}

// REF4 t02: one field list for the save (save_io::Out) and the CRC (save_io::Hash). Before, hash() kept its own list and
// left out ai_side_, opp_prev_, unit_ai_stamps_ and unit_ai_second_ (opportunity filtering, attack positioning).
template <class A>
void BattleAi::visit_saved(A& a) const {
    a(ai_side_);
    a.save_only(stale_, executed_);  // statistics (summary())
    a(opp_prev_, unit_ai_stamps_);
    a.seq(follows_, [](A& x, const ObjectFollow& f) { follow_fields(x, f); });
    a(unit_ai_second_);  // version 7 / 8
    if (!world_->ai_lane()) return;
    // sp3: the host waited for the running job (lane_sync); a finished job not yet collected is saved as it is
    const auto command = [](A& x, const cmd::Command& c) { x.blob(cmd::encode_command(c)); };
    const auto lock = [](A& x, const lua::ScriptLockFact& f) { x(f.id, f.locked, f.diverted, f.goal); };
    a.save_only(lane_digest_);  // hash() takes it in the world's place
    a(lane_seq_);
    a.seq(lane_batch_, command);
    a.save_only(lane_job_.pending, lane_job_.frame, lane_job_.digest);
    a.save_only_seq(lane_job_.batch, command);
    a.seq(lane_locks_, lock);  // version 6
    a.save_only_seq(lane_job_.locks, lock);
}

void BattleAi::write(ByteWriter& w) const {
    w.u32(world_->ai_lane() ? kBattleAiLaneSaveVersion : kBattleAiSaveVersion);
    put_section(w, [this](ByteWriter& sub) { write_system(*world_, sub); });
    save_io::Out o(w);
    visit_saved(o);
}

void BattleAi::hash(StateHasher& h) const {
    if (world_->ai_lane()) h.u64(lane_digest_);  // sp3 t02: the last collected job's world digest
    else world_->hash(h);
    save_io::Hash a(h);
    visit_saved(a);
}

bool BattleAi::read(ByteReader& r) {
    // sp3 t04: version 5 (lane block) only into an AI-lane world; version 4 into either (an AI-lane world then starts with
    // an empty batch)
    const uint32_t version = r.u32();
    const bool lane_version = version == kBattleAiLaneSaveVersion5 || version == kBattleAiLaneSaveVersion6 || version == kBattleAiLaneSaveVersion;
    const bool has_second = version == kBattleAiSaveVersion || version == kBattleAiLaneSaveVersion;  // mcx6 t06
    if (!r.ok() || !(version == kBattleAiSaveVersion || version == kBattleAiSaveVersion4 || (lane_version && world_->ai_lane())))
        return false;
    const std::span<const uint8_t> world = r.raw(r.u32());
    std::vector<uint8_t> side;
    uint32_t stale = 0;
    std::map<std::string, std::pair<uint32_t, uint32_t>> executed;
    std::vector<std::pair<uint64_t, int32_t>> opp;
    std::map<objects::ObjectId, int32_t> stamps;
    save_io::In in(r);
    in(side, stale, executed, opp, stamps);
    std::vector<ObjectFollow> follows;
    in.seq(follows, [](save_io::In& a, ObjectFollow& f) { follow_fields(a, f); });
    std::map<objects::ObjectId, int32_t> seconds;
    if (has_second) in(seconds);
    uint32_t lane_seq = 0;
    uint64_t lane_digest = 0;
    std::vector<cmd::Command> lane_batch;
    LaneJob job;
    std::vector<lua::ScriptLockFact> lane_locks;
    if (lane_version) {
        const auto get_batch = [&r](std::vector<cmd::Command>& out) {
            uint32_t n = 0;
            if (!r.count(n, 4)) return false;
            for (uint32_t i = 0; i < n && r.ok(); ++i) {
                const std::span<const uint8_t> bytes = r.raw(r.u32());
                size_t pos = 0;
                cmd::Command c;
                if (!r.ok() || cmd::decode_command(bytes, pos, c) != cmd::DecodeError::None || pos != bytes.size()) return false;
                out.push_back(std::move(c));
            }
            return r.ok();
        };
        lane_digest = r.u64();
        lane_seq = r.u32();
        if (!get_batch(lane_batch)) return false;
        job.pending = r.boolean();
        job.frame = r.u64();
        job.digest = r.u64();
        if (!get_batch(job.batch)) return false;
        const bool has_locks = version == kBattleAiLaneSaveVersion6 || version == kBattleAiLaneSaveVersion;
        if (has_locks && (!get_locks(r, lane_locks) || !get_locks(r, job.locks))) return false;
    }
    if (!r.ok() || !r.done() || side != ai_side_) return false;  // the AI players are rebuilt by start()
    const std::map<objects::ObjectId, int32_t> stamps_before = unit_ai_stamps_, seconds_before = unit_ai_second_;
    unit_ai_stamps_ = std::move(stamps);  // before relink(): its fill() must find every listed object stamped (no draws)
    unit_ai_second_ = std::move(seconds);
    const auto restore_stamps = [&] {
        unit_ai_stamps_ = stamps_before;
        unit_ai_second_ = seconds_before;
    };
    ByteReader wr(world);
    if (!read_system(*world_, wr) || !wr.ok() || !wr.done()) {
        restore_stamps();
        return false;
    }
    // battle.ai is the last battle chunk (CampaignBattles registers it after the battle parts): every other battle
    // system is restored, so the world takes its types / host callbacks now (tactical_ai.hpp relink).
    if (!world_->relink()) {
        restore_stamps();
        return false;
    }
    stale_ = stale;
    follows_ = std::move(follows);
    executed_ = std::move(executed);
    opp_prev_ = std::move(opp);
    lane_seq_ = lane_seq;
    lane_batch_ = std::move(lane_batch);
    lane_digest_ = lane_digest;
    lane_job_ = std::move(job);
    // slice 4e: a version-5 save (no facts) takes the loaded bridge's (its world was saved after the job)
    lane_locks_ = version == kBattleAiLaneSaveVersion6 || version == kBattleAiLaneSaveVersion ? std::move(lane_locks)
                                                                                                : world_->object_bridge().script_lock_facts();
    view_.valid = false;  // sp3 t02: captured again at the next AI slot
    view_.grid_valid = false;  // ref5 t06: the grid copy too (a loaded grid may have another state at the same version)
    world_->object_bridge().lane_abilities = nullptr;  // sp3 slice 4b: likewise the ability records
    lane_unloaded_.clear();  // sp3 slice 4d: consumed within the tick of its apply
    return true;
}

// ---- perception snapshot ------------------------------------------------------------------------------------------

void BattleAi::ensure_type_facts() {
    const objects::GameObjectTypes& types = mode_->types();
    if (type_facts_.size() == types.size() + 1) return;
    type_facts_.assign(types.size() + 1, TypeFacts{});
    for (size_t t = 0; t <= types.size(); ++t) {
        const auto ti = static_cast<objects::TypeIndex>(t);  // == types.size(): the out-of-range entry (field defaults)
        TypeFacts& f = type_facts_[t];
        f.has_land_evaluator = types.get(ti, objects::tags::Has_Land_Evaluator);
        f.has_space_evaluator = types.get(ti, objects::tags::Has_Space_Evaluator);
        f.last_state_visible_under_fow = types.get(ti, objects::tags::Last_State_Visible_Under_FOW);
        f.initial_state_visible_under_fow = types.get(ti, objects::tags::Initial_State_Visible_Under_FOW);
        f.create_team = types.get(ti, objects::tags::Create_Team);
        f.is_dummy = types.get(ti, objects::tags::Is_Dummy);
        f.stay_in_transport = types.get(ti, objects::tags::Stay_In_Transport_During_Ground_Battle);
        f.valid_target = types.get(ti, objects::tags::Collidable_By_Projectile_Living) && types.get(ti, objects::tags::Is_Valid_Target);
        f.base_level = types.get(ti, objects::tags::Base_Level);
        f.additional_population_capacity = types.get(ti, objects::tags::Additional_Population_Capacity);
        f.reinforcement_enabling_radius = types.get(ti, objects::tags::Reinforcement_Enabling_Radius);
        f.reinforcement_prevention_radius = types.get(ti, objects::tags::Reinforcement_Prevention_Radius);
        if (t == types.size()) break;  // the behaviour bits and member flags have no out-of-range form
        f.influences_capture = types.get(ti, objects::tags::Influences_Capture_Point);
        const auto has = [&](objects::BehaviorTypeId b) { return types.has_behavior(ti, objects::BehaviorSet::Union, b); };
        f.squadron_member = types.squadron_member(ti);
        f.company_member = types.company_member(ti);
        f.indigenous = has(kIndigenous);
        f.dummy_ground_structure = has(kDummyGroundStructureBeh);
        f.dummy_ground_base = has(kDummyGroundBase);
        f.dummy_orbital_structure = has(kDummyOrbitalStructure);
        f.dummy_star_base = has(kDummyStarBaseBeh);
    }
}

void BattleAi::fill(at::PerceptionWorld& w, std::vector<at::TacticalAiObject>& out) {
    ensure_type_facts();  // sp3 t01: before the parallel build below reads it
    const objects::ObjectStore& st = mode_->primary();
    const objects::GameObjectTypes& types = mode_->types();
    const galactic::GalacticSim& g = *b_->galaxy_;
    const tc::CombatSystem& c = *b_->combat_;
    const uint32_t players = host_->players();
    if (w.types.empty()) {
        // Static parts (types indexed by objects::TypeIndex, players, relations, map, hard points).
        w.mode = b_->space_ ? at::TacticalMode::Space : at::TacticalMode::Land;
        w.campaign = !b_->cfg_.skirmish;  // as TacticalAiConfig::campaign above
        const tactical::Aabb& bb = mode_->bounds();
        w.map = {bb.min.x, bb.min.y, bb.max.x, bb.max.y};
        // S10 land outcome: the campaign start markers behind IsFriendly / IsEnemyStartLocation (AI_TACTICAL_PERCEPTION
        // §3: Defending Forces / Attacker Entry Position by the "is defender" side). Before, the list stayed empty, every
        // start-location leaf failed and with it Land_Area_Needs_Scouting: the AI never scouted for fogged enemies.
        w.markers.clear();
        if (w.campaign) {
            for (const auto& [defend, pos] : b_->bt_start_markers()) {
                at::StartMarker m;
                m.kind = defend ? at::StartMarker::Kind::DefendingForces : at::StartMarker::Kind::AttackerEntry;
                m.position = {pos.x, pos.y, pos.z};
                w.markers.push_back(m);
            }
        } else {
            // t48: skirmish reads Player_%d_Position_Marker of Self. [port] the port's skirmish deploys both sides at the battle
            // anchors, not at those markers, so each side's start position is its anchor (defender 0, attacker 1).
            // skm2 t02: every side of a skirmish map with more than two slots (side k >= 2 = slot k).
            for (uint8_t side = 0; side < b_->side_count(); ++side) {
                at::StartMarker m;
                m.kind = at::StartMarker::Kind::PlayerPosition;
                m.player_id = b_->side_player(side);
                m.position = {b_->anchor_[side].x, b_->anchor_[side].y, b_->anchor_[side].z};
                w.markers.push_back(m);
            }
        }
        // A2k t13 / t06: a type without a converted CategoryMask field resolves its names with the types' data-loaded
        // GameObjectCategoryType converter (mod categories), as the tactical AI's perception does.
        const auto mask = at::category_mask_from(types.enums());
        // battle host: type flag +0x29 = listed in some type's Squadron_Units (0x377500), sorted for lookup.
        std::vector<uint32_t> squadron_members;
        for (size_t t = 0; t < types.size(); ++t)
            for (const std::string_view n : types.get(static_cast<objects::TypeIndex>(t), objects::tags::Squadron_Units))
                if (const objects::TypeIndex m = types.resolve_name(n); m != objects::kNone && m < types.size())
                    squadron_members.push_back(static_cast<uint32_t>(m));
        std::sort(squadron_members.begin(), squadron_members.end());
        squadron_members.erase(std::unique(squadron_members.begin(), squadron_members.end()), squadron_members.end());
        w.types.resize(types.size());
        for (size_t t = 0; t < types.size(); ++t) {
            const auto ti = static_cast<objects::TypeIndex>(t);
            at::PerceptionType& pt = w.types[t];
            pt.name = t < types.raw().types().name.size() ? std::string(types.name(ti)) : std::string();
            pt.category = types.get(ti, objects::tags::CategoryMask);
            if (pt.category == 0)
                for (const std::string_view n : types.get_list(ti, objects::tags::CategoryMask)) pt.category |= mask(n);
            pt.combat_power = types.get(ti, objects::tags::AI_Combat_Power);
            // A1_READINGS §29 [H]: in a tactical mode 0x370e60 scales a company / squadron type whose first member has
            // Create_Team by clamp(Max_Squad_Size / member count, 0, 1) (the load-time power is the members' sum).
            {
                auto trimmed = [](std::string_view v) {
                    while (!v.empty() && (v.front() == ' ' || v.front() == '	')) v.remove_prefix(1);
                    while (!v.empty() && (v.back() == ' ' || v.back() == '	')) v.remove_suffix(1);
                    return v;
                };
                std::span<const std::string_view> list = types.get(ti, objects::tags::Company_Units);
                if (list.empty()) list = types.get(ti, objects::tags::Squadron_Units);
                const objects::TypeIndex first = list.empty() ? objects::kNone : types.resolve_name(trimmed(list.front()));
                if (first != objects::kNone && first < types.size() && types.get(first, objects::tags::Create_Team)) {
                    float r = static_cast<float>(types.get(ti, objects::tags::Max_Squad_Size)) / static_cast<float>(list.size());
                    r = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
                    pt.combat_power = pt.combat_power * r;
                }
            }
            pt.is_hero = types.get(ti, objects::tags::Is_Named_Hero);
            pt.bombing_run_prevention_radius = types.get(ti, objects::tags::Bombing_Run_Prevention_Radius);  // float field (bkc1 t07)
            pt.community_property = types.get(ti, objects::tags::Is_Community_Property);
            // --- A2 tactical: PotentialPlan inputs (tactical_pp.hpp) ---
            pt.tech_level = types.get(ti, objects::tags::Tech_Level);
            pt.population = type_population(static_cast<int32_t>(t));
            pt.build_cost = static_cast<float>(types.get(ti, objects::tags::Build_Cost_Credits));  // int field
            pt.build_time_seconds = types.get(ti, objects::tags::Build_Time_Seconds);  // int field +0x890 (A2g t06: AllowedTime base)
            for (const std::string_view f : types.get(ti, objects::tags::Affiliation)) pt.affiliation.emplace_back(f);
            // flags +0x67 Create_Team / +0x29 squadron member / +0x21 Cinematic_Object_Only, behaviours 0x5C DUMMY_STAR_BASE /
            // 0x5B DUMMY_GROUND_BASE (AI_GALACTIC_3 §6 table) [battle host: replaces the Is_Dummy stand-in]
            static const objects::BehaviorTypeId kStarBase = objects::behavior_type_id("DUMMY_STAR_BASE");
            static const objects::BehaviorTypeId kGroundBase = objects::behavior_type_id("DUMMY_GROUND_BASE");
            pt.pp_excluded = types.get(ti, objects::tags::Create_Team) || types.get(ti, objects::tags::Cinematic_Object_Only) ||
                             std::binary_search(squadron_members.begin(), squadron_members.end(), static_cast<uint32_t>(t)) ||
                             (kStarBase != objects::kNoBehavior && types.has_behavior(ti, objects::BehaviorSet::Union, kStarBase)) ||
                             (kGroundBase != objects::kNoBehavior && types.has_behavior(ti, objects::BehaviorSet::Union, kGroundBase));
            static const objects::BehaviorTypeId kUc = objects::behavior_type_id("TACTICAL_UNDER_CONSTRUCTION");
            pt.under_construction = kUc != objects::kNoBehavior && types.has_behavior(ti, objects::BehaviorSet::Union, kUc);
            if (pt.under_construction) {  // skai t12: the finished type, for TacticalBuiltStructureCount
                const objects::TypeIndex ft = types.resolve_name(types.get(ti, objects::tags::Tactical_Buildable_Constructed));
                if (ft != objects::kNone && ft < types.size()) pt.constructed_type = static_cast<int32_t>(ft);
            }
            // Pad price 0x3744e0 (_Campaign, else _Multiplayer; [port] skirmish too, no planet
            // factor); other types: the same tags as their tactical price [M] (only TacticalBuild of build plans reads it).
            // Skirmish production of non-UC types (station buy bar): Tactical_Build_Cost_Multiplayer.
            pt.tactical_cost = (!pt.under_construction && b_->cfg_.skirmish)
                                   ? static_cast<float>(std::max(0, types.get(ti, objects::tags::Tactical_Build_Cost_Multiplayer)))
                                   : b_->build_cost(ti);  // int fields (TacticalBattle::build_cost: _Campaign, else _Multiplayer)
            pt.generic_hero = types.get(ti, objects::tags::Is_Generic_Hero);
            pt.special_weapon = types.get(ti, objects::tags::Is_Special_Weapon_In_Space) ||
                                types.has_behavior(ti, objects::BehaviorSet::Union, kLobbing);
            pt.has_ai_script = !types.get(ti, objects::tags::Lua_Script).empty();
            // --- end A2 tactical ---
        }
        w.players.resize(players);
        w.relation.assign(static_cast<size_t>(players) * players, 0);
        for (uint32_t a = 0; a < players; ++a) {
            at::PerceptionPlayerInfo& pi = w.players[a];
            pi.id = static_cast<int32_t>(a);
            if (a < g.players().size() && g.players()[a].faction < g.rules().factions.size()) {
                pi.faction = g.rules().factions[g.players()[a].faction].name;
                pi.neutral_faction = g.rules().factions[g.players()[a].faction].is_neutral;
                pi.is_human = g.players()[a].human;
            }
            pi.attacker = static_cast<int32_t>(a) == b_->req_.info.attacker;
            // skm2 t03 (AI_TACTICAL_PERCEPTION IsDifficulty 0x6e0620): the AI player's difficulty (AIPlayer +0x58); none for a
            // human or a player without a tactical AI here.
            static constexpr const char* kLevels[3] = {"Easy", "Normal", "Hard"};
            const int32_t d = pi.is_human ? -1 : world_->difficulty(static_cast<int32_t>(a));
            pi.ai_difficulty = d >= 0 && d <= 2 ? kLevels[d] : "";
            for (uint32_t bq = 0; bq < players; ++bq) {
                if (a == bq) continue;
                const bool hostile = c.is_enemy(static_cast<int32_t>(a), static_cast<int32_t>(bq));
                // skm2 t02: an ally (relation 0: a skirmish team mate) is friendly to the perception, as the combat table says.
                const bool ally = !hostile && b_->bt_relation(static_cast<int32_t>(a), static_cast<int32_t>(bq)) == 0;
                w.relation[static_cast<size_t>(a) * players + bq] = hostile ? 1 : ally ? 0 : 2;
            }
        }
        // S9 hard points (AI_TACTICAL_PERCEPTION �sec. 4): kind, weapon range, health; power = the unit's AI_Combat_Power
        // shared by its weapon hard points [port M].
        const tc::CombatSystem* cs = &c;
        TacticalBattle* bt = b_;
        if (world_->ai_lane()) {  // ref5 t06: the capture's copy
            w.hard_points = [this](int32_t id, std::vector<at::PerceptionHardPoint>& hps) {
                hps.clear();
                const auto& ix = view_.hp_index;
                const auto it = std::lower_bound(ix.begin(), ix.end(), id, [](const auto& e, int32_t v) { return e.first < v; });
                if (it == ix.end() || it->first != id) return;
                const auto first = view_.hps.begin() + static_cast<std::ptrdiff_t>(it->second.first);
                hps.assign(first, first + static_cast<std::ptrdiff_t>(it->second.second));
            };
        } else {
            w.hard_points = [this, cs, bt](int32_t id, std::vector<at::PerceptionHardPoint>& hps) {
                lane_live_read("hard_points");
                const TacticalBattle::Unit* u = bt->unit_by_id(id);
                combat_hard_points(*cs, id, u ? u->power : 0.0f, hps);
            };
        }
    }
    for (uint32_t a = 0; a < players && a < w.players.size(); ++a) {
        w.players[a].credits = a < g.players().size() ? g.credits(static_cast<int32_t>(a)) : 0.0f;
        w.players[a].pop_used = static_cast<int32_t>(population_used(static_cast<int32_t>(a)));
        const float cap = population_cap(static_cast<int32_t>(a));
        w.players[a].pop_cap = cap < 0.0f ? 0 : static_cast<int32_t>(cap);
        w.players[a].can_retreat = b_->retreat_side_ < 0 && b_->frame_ >= b_->retreat_allowed_frame_ &&
                                   !b_->retreat_prevented(static_cast<int32_t>(a));  // cu8 t01
        // Int-final: IsBombingRunAvailable / IsOrbitalBombardmentAvailable from the battle's land strike managers.
        w.players[a].bombing_run_available = b_->bombing_run_available(static_cast<int32_t>(a));
        w.players[a].orbital_bombardment_available = b_->bombardment_available(static_cast<int32_t>(a));
        w.players[a].reinforcements.clear();
        for (const at::TacticalReinforcement& r : reinforcement_pool(static_cast<int32_t>(a))) w.players[a].reinforcements.push_back(r.type);
    }
    // A1_READINGS §26: TimeLastSeen's cell test - the base passability word (.ted 0x10A, not the effective word with
    // stamped objects) of cell ((int)((x + 10) / 20), (int)((y + 10) / 20)); off the grid counts as blocked. A map
    // without a passability chunk keeps base Clear (MOVEMENT §1.1).
    if (!b_->space_ && !w.point_blocked) {
        const terrain::MapData* map = &b_->map_;
        w.point_blocked = [map](float x, float y) {
            const float fx = (x + 10.0f) / 20.0f, fy = (y + 10.0f) / 20.0f;
            if (!(fx > -1.0f) || !(fy > -1.0f) || !(fx < 65536.0f) || !(fy < 65536.0f)) return true;
            const auto cx = static_cast<uint32_t>(static_cast<int32_t>(fx));
            const auto cy = static_cast<uint32_t>(static_cast<int32_t>(fy));
            if (cx >= map->terrain.width || cy >= map->terrain.height) return true;
            if (!map->passability_present) return false;
            return (map->passability_at(cx, cy) & (terrain::kPassSteep | terrain::kPassWater)) != 0u;
        };
    }
    if (!b_->space_ && !w.point_explore_blocked) {  // aitac t07: Explore_Area's cell test (0x26)
        const terrain::MapData* map = &b_->map_;
        w.point_explore_blocked = [map](float x, float y) {
            const float fx = (x + 10.0f) / 20.0f, fy = (y + 10.0f) / 20.0f;
            if (!(fx > -1.0f) || !(fy > -1.0f) || !(fx < 65536.0f) || !(fy < 65536.0f)) return true;
            const auto cx = static_cast<uint32_t>(static_cast<int32_t>(fx));
            const auto cy = static_cast<uint32_t>(static_cast<int32_t>(fy));
            if (cx >= map->terrain.width || cy >= map->terrain.height) return true;
            if (!map->passability_present) return false;
            return (map->passability_at(cx, cy) & (terrain::kPassSteep | terrain::kPassObstacle | terrain::kPassWater)) != 0u;
        };
    }
    // A1_READINGS §30.3: game type in {1..4} = skirmish / MP. [port] no MP; the port's skirmish battles are game type 3.
    // Set every fill (a load restores it too); the callbacks below are installed whenever missing, so a world read
    // from a save (which keeps only the callbacks its owner copies) gets them back at the next fill.
    w.skirmish_or_mp = b_->cfg_.skirmish;
    // Game.IsLandControlGame (AI_GALACTIC_TOKENS 0x6e2060) = GameModeManager +0x20 == 4, a game-setup value written only
    // when a game starts, so it is fixed for the whole battle. skai t15 [H, measured]: 0 in a conquest land skirmish.
    // Retail run 20261116-171531 (Utapau, won by destroying the base) proposes Hold_Position at 10, then 5 (20 - 15, one
    // activation failure) and 20: 10 * Is_Initial_Skirmish_State_Established (1 with the base, 2 after tech 2), none of
    // it from 20 * IsLandControlGame (SKAI_T15_PLAN_RETEST "IsLandControlGame"). [port] the control-point win condition
    // stands for type 4 (ccpgu t03). It gates Hold_Position, Secure_Victory_Control_Point and May_Lose_Land_Control_Game
    // (which, when 1, holds back the opening unit builds).
    w.land_control = b_->cfg_.skirmish && b_->cfg_.control_point_domination && !b_->space_;
    w.interventions_enabled = b_->cfg_.interventions_enabled;  // mcx8 t09 Game.InterventionsEnabled (game settings flags)
    if (!w.parent_rep_type) {
        // The parent-mode (galactic) object by id (0x28aa50), not found -> -1; its type [M: 0x3973b0 on the galactic
        // object; the port's galactic units / companies are their own representative type, no TRANSPORT carried type].
        const galactic::GalacticSim* gal = b_->galaxy_;
        w.parent_rep_type = [gal](int32_t id) -> int32_t {
            if (!gal || id < 0) return -1;
            const objects::ObjectStore& gs = gal->store();
            const objects::ObjectHandle gh = gs.find(id);
            return gs.exists(gh) ? static_cast<int32_t>(gs.type(gh)) : -1;
        };
    }
    // A1_READINGS §30.4: IsValidBombingTarget's shield step = inside any player's active BASE_SHIELD (3-D), the L1
    // strike validity test. `inside_shield` (IsInside*Shield) stays unset: always 0 in shipped data.
    if (!b_->space_ && !w.in_active_base_shield) {
        const TacticalBattle* sb = b_;
        if (world_->ai_lane()) {
            // ref5 t06 (tru1 t02, TRU1_A_RE_ANSWERS A1): l1_base_shield_covers on the shields the capture found on (combat's
            // shield state); before, the lane counted every live BASE_SHIELD of the snapshot store.
            w.in_active_base_shield = [this](const at::Vec3f& p) {
                for (const LaneView::Shield& s : view_.shields) {
                    const float dx = p.x - s.x, dy = p.y - s.y, dz = p.z - s.z;
                    if (dx * dx + dy * dy + dz * dz < s.r2) return true;
                }
                return false;
            };
        } else {
            w.in_active_base_shield = [this, sb](const at::Vec3f& p) {
                lane_live_read("in_active_base_shield");
                return sb->l1_base_shield_covers({p.x, p.y, p.z});
            };
        }
    }
    // --- fog of war (owner: fog agent): the real fog with force = 1 (the AI target-selection form); the AI gate
    // (non-human players unfogged while AIUsesFogOfWar is False) is applied by the perception's own force-0
    // tests (threat-grid fog memory). fow_active = the battle's FOW-active flag. ---
    if (const tactical::fog::FogOfWar* fog = b_->fog()) {
        w.fow_active = fog->fow_active();
        if (!w.object_fogged && world_->ai_lane()) {  // sp3 slice 4f: the view's bits and grid copies (AI lane)
            w.object_fogged = [this](int32_t player, int32_t id) {
                const auto pk = std::find(view_.fog_players.begin(), view_.fog_players.end(), player);
                if (pk == view_.fog_players.end()) return false;
                const auto& v = view_.fog_hidden;
                const auto it = std::lower_bound(v.begin(), v.end(), id, [](const std::pair<objects::ObjectId, uint32_t>& a, int32_t x) { return a.first < x; });
                return it != v.end() && it->first == id && ((it->second >> static_cast<uint32_t>(pk - view_.fog_players.begin())) & 1u) != 0;
            };
            w.point_fogged = [this](int32_t player, float x, float y) { return view_point_fogged(player, x, y); };
        } else if (!w.object_fogged) {
            const TacticalBattle* fb = b_;
            w.object_fogged = [this, fb](int32_t player, int32_t id) {
                lane_live_read("object_fogged");
                return fb->fog_object_hidden(player, id, true);
            };
            w.point_fogged = [this, fb](int32_t player, float x, float y) {
                lane_live_read("point_fogged");
                return fb->fog_point_fogged(player, x, y, true);
            };
        }
    }
    // --- end fog ---
    // Team containers -> members (battle units, ascending id).
    std::map<objects::ObjectId, std::vector<int32_t>> members;
    for (const TacticalBattle::Unit& u : b_->units_)
        if (u.alive && !u.retreated && u.container != 0xFFFFFFFFu) members[b_->containers_[u.container].id].push_back(u.id);
    // t48: Variable_Self.BaseLevel in a land campaign battle: the conflict planet's ground structures, whoever asks. [inferred] the
    // spec's "without flag 4" = still alive in the galactic store (the flag's meaning is not recorded).
    w.conflict_ground_structures = 0;
    if (const auto pl = b_->req_.info.planet; !b_->space_ && static_cast<size_t>(pl) < g.planets().size())
        for (const objects::ObjectHandle s : g.planets()[pl].structures)
            if (g.store().alive(s)) ++w.conflict_ground_structures;
    // Objects of a live bombing run (the land mode's +0x38 bombing-run manager set, A1_READINGS §6.2 item 9), ascending.
    std::vector<objects::ObjectId> bombing_run_objects;
    for (const TacticalBattle::L1BombingRun& run : b_->l1_runs_)
        for (const TacticalBattle::L1Bomber& bm : run.bombers) bombing_run_objects.push_back(bm.id);
    std::sort(bombing_run_objects.begin(), bombing_run_objects.end());
    // skai: the structures standing on build pads, ascending (TacticalBuiltStructureCount): finished (PadState::built) and,
    // skai t12, under construction (PadState::uc). Retail counts a refinery from the build start: the Rebel AI buys its
    // first units and early upgrade (both gated by Num_Refineries > 0) at 2295, 386 frames after the Mineral Processor's
    // UC object appears (1909) and 364 before it is finished (2659) [H, trace: SKAI_T06_LAND_RUNS s7].
    std::vector<objects::ObjectId> pad_built;
    for (const TacticalBattle::PadState& ps : b_->pads_) {
        if (ps.built != objects::kNoObjectId) pad_built.push_back(ps.built);
        if (ps.uc != objects::kNoObjectId) pad_built.push_back(ps.uc);
    }
    std::sort(pad_built.begin(), pad_built.end());
    // OPT2 t03: the listed objects (store order) first; their entries are built on the job system (per-index outputs, every
    // read below is const); the UNIT_AI stamp draws then run serially in store order.
    std::vector<objects::ObjectHandle> listed;
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.active(h) || st.has_behavior(h, objects::beh::Particle)) continue;
        const TacticalBattle::Unit* u = b_->unit_by_id(st.id(h));
        if (u && (!u->alive || u->retreated)) continue;
        listed.push_back(h);
    }
    w.objects.clear();
    out.clear();
    w.objects.resize(listed.size());
    out.resize(listed.size());
    static const objects::BehaviorTypeId kDummyStarBase = objects::behavior_type_id("DUMMY_STAR_BASE");
    static const objects::BehaviorTypeId kDummyGroundStructure = objects::behavior_type_id("DUMMY_GROUND_STRUCTURE");
    const auto build = [&](size_t i) {
        const objects::ObjectHandle h = listed[i];
        const objects::ObjectId id = st.id(h);
        const auto type = static_cast<int32_t>(st.type(h));
        const auto ti = st.type(h);
        const TypeFacts& tf = type_facts(ti);
        const TacticalBattle::Unit* u = b_->unit_by_id(id);
        at::PerceptionObject o;
        o.id = id;
        o.owner = st.owner(h);
        o.type = type;
        if (u && u->container != 0xFFFFFFFFu) o.rep_type = static_cast<int32_t>(st.type(b_->containers_[u->container].h));
        if (const objects::TypeIndex ct = st.carried_type(h); ct != objects::kNone) o.rep_type = static_cast<int32_t>(ct);  // a1i t04: §35.3
        o.has_unit_ai = st.has_behavior(h, kUnitAi) && c.has_unit(id);
        o.team_or_hide_when_fogged = st.has_behavior(h, kTeam) || st.has_behavior(h, kHideWhenFogged);
        o.mobile = u && u->movable;
        if (!u && st.has_behavior(h, kTeam))  // battle host: a TEAM container moves with its live members
            if (const auto tm = members.find(id); tm != members.end())
                for (const int32_t m : tm->second)
                    if (const TacticalBattle::Unit* mu = b_->unit_by_id(m); mu && mu->movable) o.mobile = true;
        const Vec3 p = st.position(h), f = st.facing(h);
        o.position = {p.x, p.y, p.z};
        o.transform[0] = f.x;
        o.transform[4] = f.y;
        o.transform[3] = p.x;
        o.transform[7] = p.y;
        o.transform[11] = p.z;
        if (c.has_unit(id)) {
            const float mx = c.max_health(id);
            o.health = mx > 0.0f ? c.health(id) / mx : 1.0f;
            o.shield = c.shield_fraction(id);
            o.max_attack_range = u ? u->range : 0.0f;
        }
        if (o.mobile && b_->movement_->has_unit(id)) o.speed = b_->movement_->view(id).speed * 30.0f;  // [M] units per second
        o.combat_power = u ? u->power : (static_cast<size_t>(type) < w.types.size() ? w.types[static_cast<size_t>(type)].combat_power : 0.0f);
        // A1_READINGS §29 [H]: a TEAM container is in the threat grid (UNIT_AI from its own type) as one whole-object entry
        // of its company type (0x396380, team data +0x58), with that type's tactical power, the first member's attack range
        // and the mean of the members' hull fractions; the members (no UNIT_AI) have no entries.
        if (!u && st.has_behavior(h, kTeam))
            for (const TacticalBattle::Container& ct : b_->containers_) {
                if (ct.id != id || !ct.team) continue;
                if (ct.squad_type < w.types.size()) {
                    o.type = static_cast<int32_t>(ct.squad_type);
                    o.rep_type = type;  // 0x3973b0: Create_Team_Type, else the company type
                    o.combat_power = w.types[ct.squad_type].combat_power;
                }
                o.has_unit_ai = st.has_behavior(h, kUnitAi);
                if (const auto tm = members.find(id); tm != members.end() && !tm->second.empty()) {
                    float hull = 0.0f;
                    for (const int32_t m : tm->second)
                        hull += c.has_unit(m) && c.max_health(m) > 0.0f ? c.health(m) / c.max_health(m) : 1.0f;
                    o.health = hull / static_cast<float>(tm->second.size());
                    if (const TacticalBattle::Unit* mu = b_->unit_by_id(tm->second.front())) o.max_attack_range = mu->range;
                }
                break;
            }
        o.has_damage_tracking = b_->damage_tracking_.tracks(id);
        o.is_build_pad = st.has_behavior(h, kBuildObjects);
        o.is_cash_point = st.has_behavior(h, kCashPoint);
        // Variable_Target.IsContestable (AI_TACTICAL_PERCEPTION §3, 0x731400 [M]): behaviour 0x3D (CAPTURE_POINT,
        // OBJECT_RUNTIME §4.3) and the type is affiliated (0x3751a0) with the faction of the special player 0x331cc0 (the
        // first player of an Is_Neutral faction) [port: = the first Is_Neutral faction, GameObjectTypes::default_faction()].
        if (st.has_behavior(h, kCapturePoint)) {
            const std::span<const uint32_t> fs = types.factions(ti);
            o.contestable = std::find(fs.begin(), fs.end(), types.default_faction()) != fs.end();
        }
        // t41: Variable_Self.BaseLevel in space (AI_TACTICAL_PERCEPTION, Player tokens). Unset, every station read as level 0, so a
        // defender holding one passed the "BaseLevel == 0" retreat test of the shipped Want_To_Retreat_From_Space.
        // [inferred] the level (0x397540) = the type's Base_Level: the battle does not lower it while the station lives.
        if (kDummyStarBase != objects::kNoBehavior && st.has_behavior(h, kDummyStarBase)) {
            o.dummy_star_base = true;
            o.starbase_level = tf.base_level;
        }
        // t48: Variable_Self.BaseLevel in land skirmish: the count of P's alive objects with this behaviour.
        o.dummy_ground_structure = kDummyGroundStructure != objects::kNoBehavior && st.has_behavior(h, kDummyGroundStructure);
        o.no_owner_list = (st.flags(h) & objects::kFlagNoAiOwnerList) != 0;  // skai t15 (SKAI_T15_TARGET_CHOICE Q5)
        // skai: TacticalBuiltStructureCount's free-store table of built structures (0x4d3630) [M]: the finished structures on
        // build pads (pre-built skirmish base included). Unset, Num_Refineries stayed 0 and the land skirmish AI never bought units.
        o.built_structure = std::binary_search(pad_built.begin(), pad_built.end(), id);
        o.transport_landing = st.has_behavior(h, objects::beh::TransportLanding);
        // obj +0x54 (A1_READINGS §30.3): the galactic counterpart's id, -1 none
        if (const objects::ObjectId pm = st.parent_mode_id(h); pm != objects::kNoObjectId) o.parent_object = pm;
        o.visible_to = ~0ull;
        // land: the first zone (list order) whose cell set holds the position's cell (A1_READINGS §4, ZoneLocator)
        if (!zones_.empty()) o.zone = zone_locator_.zone_at(p.x, p.y);
        const auto mt = members.find(id);
        at::TacticalAiObject a;
        a.handle = h;
        const objects::TypeIndex rep = o.rep_type >= 0 ? static_cast<objects::TypeIndex>(o.rep_type) : ti;
        a.target.id = id;
        a.target.rep_has_land_evaluator = type_facts(rep).has_land_evaluator;
        a.target.rep_has_space_evaluator = type_facts(rep).has_space_evaluator;
        a.target.own_has_space_evaluator = tf.has_space_evaluator;
        a.applies.has_locomotor = st.has_behavior(h, objects::beh::Locomotor);
        a.applies.has_build_objects = o.is_build_pad;
        a.applies.has_cash_point = o.is_cash_point;
        a.applies.reinforcement_enabling_radius = tf.reinforcement_enabling_radius;
        a.applies.last_state_visible_under_fow = tf.last_state_visible_under_fow;
        a.applies.initial_state_visible_under_fow = tf.initial_state_visible_under_fow;
        at::FreeStoreObjectInfo& fs = a.free_store;
        fs.is_transport = st.has_behavior(h, objects::beh::Transport);
        fs.type_create_team = tf.create_team;
        fs.type_is_dummy = tf.is_dummy;
        fs.type_stay_in_transport = tf.stay_in_transport;
        fs.has_indigenous_or_explosion = st.has_behavior(h, kIndigenous);
        fs.has_lobbing_superweapon = st.has_behavior(h, kLobbing);
        fs.has_locomotor = a.applies.has_locomotor && o.mobile;
        fs.is_starship_combatant = st.has_behavior(h, objects::beh::StarshipCombatant) || (u != nullptr && o.mobile);  // [port] battle units
        fs.is_team = st.has_behavior(h, kTeam);
        // Behaviour-in-type tests (0x370320) read the type's behaviour union [M: which set]; the rest are object tests.
        fs.type_category_bit_5f = tf.dummy_ground_structure;
        fs.type_bit_5b = tf.dummy_ground_base;
        // TEAM: the representative (container) type or any live member type has INDIGENOUS_UNIT
        if (fs.is_team) {
            fs.team_has_bit_20 = tf.indigenous;
            if (mt != members.end())
                for (const int32_t m : mt->second)
                    if (const objects::ObjectHandle mh = st.find(static_cast<objects::ObjectId>(m)); mh && type_facts(st.type(mh)).indigenous)
                        fs.team_has_bit_20 = true;
        }
        // obj +0x2b0 / +0xf8 (contained or attached): inside a garrison, a hard point / attached child (store attach
        // parent), or a DEPLOY_TROOPERS member pinned to its carrier. [port] a team member's link to its TEAM container
        // (TacticalBattle::Unit::container, store container) is not read as "contained" here.
        fs.subobject_f8_nonempty = b_->garrison_holder_of(id) != objects::kNoObjectId || static_cast<bool>(st.attach_parent(h)) ||
                                   (u != nullptr && u->attached_to != objects::kNoObjectId);
        fs.in_mode_manager_set = !b_->space_ && std::binary_search(bombing_run_objects.begin(), bombing_run_objects.end(), id);
        fs.dead_or_removing = (u != nullptr && !u->alive) || !st.alive(h);  // dead and dying (refix t01 clone) objects are skipped above: always false here
        fs.in_world = true;  // [M] obj +0x3c8: every listed battle object is in the world (retreated units are skipped above)
        fs.type_squadron_member = tf.squadron_member;
        fs.type_company_member = tf.company_member;
        fs.has_special_weapon = st.has_behavior(h, kSpecialWeapon);
        fs.type_dummy_orbital_structure = tf.dummy_orbital_structure;
        fs.type_dummy_star_base = tf.dummy_star_base;
        fs.has_tactical_super_weapon = st.has_behavior(h, kTacticalSuperWeapon);
        if (mt != members.end()) a.team_members = mt->second;
        a.moving = o.mobile && b_->movement_->has_unit(id) && b_->movement_->view(id).has_goal;
        a.attack_target = c.has_unit(id) && c.current_target(id) != tc::kNoObject ? c.current_target(id) : -1;
        // A1 t06 [port]: a TEAM container has no movement / combat entry of its own (its orders go to the live members,
        // team_units), so its move and attack state is its members': moving while any member has a movement goal, the
        // attack target of the first member (ascending id) that has one. The original's TEAM object moves as one unit, so
        // its blocking move statuses run until the team arrives. Before, the container always read idle: every TF move of
        // an infantry squad (Land_Scout_Area's Attack_Move / Explore_Area, Destroy_Structure's Attack_Move) completed one
        // Planning service after the order, and the order-lock clearing handed the squad back to the free store at once.
        if (!u && mt != members.end()) {
            for (const int32_t m : mt->second) {
                const auto mid = static_cast<objects::ObjectId>(m);
                const TacticalBattle::Unit* mu = b_->unit_by_id(mid);
                if (mu && mu->movable && b_->movement_->has_unit(mid) && b_->movement_->view(mid).has_goal) a.moving = true;
                if (a.attack_target < 0 && c.has_unit(mid) && c.current_target(mid) != tc::kNoObject) a.attack_target = c.current_target(mid);
            }
        }
        // mcx6 t06 (LUA_API.md "Task force: Enable_Attack_Positioning", 0x4f6470): the formation attack target (0x41a2e0 =
        // CombatSystem::attack_goal) of the unit, of a TEAM container its first member's that has one (ascending id) [port:
        // the container has no combat entry]; the aimed point = the goal's hard point world position, else the target's
        // position; the 2-D extent of the target, of a TEAM container target its first live member's [port].
        {
            objects::ObjectId holder = c.has_unit(id) ? id : objects::kNoObjectId;
            if (holder == objects::kNoObjectId && !u && mt != members.end())
                for (const int32_t m : mt->second)
                    if (c.has_unit(m) && c.attack_goal(m) != tc::kNoObject) {
                        holder = m;
                        break;
                    }
            const tc::ObjectId goal = holder != objects::kNoObjectId ? c.attack_goal(holder) : tc::kNoObject;
            if (goal != tc::kNoObject) {
                a.formation_target = static_cast<int32_t>(goal);
                tactical::Vec3 hv{};
                const int32_t ghp = c.attack_goal_hardpoint(holder);
                if (ghp >= 0 && c.hardpoint_world(goal, ghp, hv)) {
                    a.formation_target_point = {hv.x, hv.y, hv.z};
                } else if (const objects::ObjectHandle gh = st.find(static_cast<objects::ObjectId>(goal)); st.exists(gh)) {
                    const Vec3 gp = st.position(gh);
                    a.formation_target_point = {gp.x, gp.y, gp.z};
                } else {
                    hv = c.position(goal);
                    a.formation_target_point = {hv.x, hv.y, hv.z};
                }
                tc::ObjectId ext = goal;
                if (!c.has_unit(goal))
                    if (const auto gm = members.find(static_cast<objects::ObjectId>(goal)); gm != members.end() && !gm->second.empty())
                        ext = gm->second.front();
                a.formation_target_extent = c.extent_2d(ext);
            }
        }
        a.damage_tracking = o.has_damage_tracking || st.has_behavior(h, kDamageTracking);
        // q6 t19 - 0x5680b0 (A1_READINGS §33.2.1): TEAM container or both type flags; not garrisoned / a hidden landing
        // (+0x3a7); not dead (listed objects are alive); not in a transport [port: battles model no carried units, the
        // garrison holder stands in]; no hero clash (+0x3b4, s78n t06: kUnitHeroClash); SPECIAL_WEAPON only on a
        // DUMMY_STAR_BASE.
        a.hero_clash = c.has_unit(id) && (c.flags(id) & tc::kUnitHeroClash) != 0;
        a.pad_occupied = b_->pad_occupied(id);  // b0416 (Setup A test 11)
        a.valid_target = (fs.is_team || tf.valid_target) &&
                         b_->garrison_holder_of(id) == objects::kNoObjectId && !b_->landing_hidden(id) && !fs.dead_or_removing &&
                         !a.hero_clash && (!fs.has_special_weapon || o.dummy_star_base);
        w.objects[i] = std::move(o);
        out[i] = std::move(a);
    };
    host_->parallel_for()(static_cast<uint32_t>(listed.size()), 64, [&build](uint32_t b, uint32_t e) {
        for (uint32_t i = b; i < e; ++i) build(i);
    });
    // UnitAI data pack (A1_READINGS §14): two synced draws per UNIT_AI object, F + Get_Int(0, 300) - 300 then
    // F + Get_Int(0, 150) - 150. [port] drawn when fill() first lists the object (store order, after the battle tick
    // of its creation frame), not inside the object's construction.
    // mcx6 t06: the second draw (+0x34) is kept too (unit_ai_second_), for the attack-positioning step.
    std::map<objects::ObjectId, int32_t> stamps, seconds;
    for (size_t i = 0; i < listed.size(); ++i) {
        if (!st.has_behavior(listed[i], kUnitAi)) continue;
        const objects::ObjectId id = w.objects[i].id;
        const auto it = unit_ai_stamps_.find(id);
        at::UnitAiStamps s;
        if (it != unit_ai_stamps_.end()) {
            s.reprojection_check = it->second;
            const auto sit = unit_ai_second_.find(id);
            s.second = sit != unit_ai_second_.end() ? sit->second : it->second;  // [port] a pre-mcx6 save kept only the first
        } else {
            s = at::draw_unit_ai_stamps(host_->random().synced(), b_->frame_);
        }
        stamps.emplace(id, s.reprojection_check);
        seconds.emplace(id, s.second);
        out[i].reprojection_stamp = s.reprojection_check;
        out[i].attack_position_stamp = s.second;
    }
    unit_ai_stamps_ = std::move(stamps);  // objects no longer listed drop out
    unit_ai_second_ = std::move(seconds);
}

// mcx6 t06 (LUA_API.md attack position 0x4f5390, terrain check 0x4f48a0 [M]): the static line test 0x5a4c10 of the land
// passability grid for the unit's movement class (a TEAM container: its first live member's, ascending id); space and units
// without a movement entry: clear.
bool BattleAi::attack_position_line_clear(int32_t object_id, const at::Vec3f& a, const at::Vec3f& b) {
    if (b_->space_) return true;
    lane_live_read("attack_position_line_clear");
    auto mover = static_cast<objects::ObjectId>(object_id);
    if (!b_->movement_->has_unit(mover)) {
        mover = objects::kNoObjectId;
        for (const TacticalBattle::Unit& u : b_->units_)
            if (u.alive && !u.retreated && u.container != 0xFFFFFFFFu && b_->containers_[u.container].id == object_id &&
                b_->movement_->has_unit(u.id)) {
                mover = u.id;
                break;
            }
        if (mover == objects::kNoObjectId) return true;
    }
    return mv::line_passable(b_->movement_->grid(), mv::V2{a.x, a.y}, mv::V2{b.x, b.y}, b_->movement_->mask_of(mover));
}

// What the store-reading fact rules read: the battle (env.view false), or the lane view (the snapshot store, the captured
// pad states, relations, live units and station upgrades) on the AI lane.
struct BattleAi::FactEnv {
    const BattleAi& ai;
    const objects::ObjectStore& st;
    bool view;
    bool allied(int32_t p, int32_t q) const {
        if (!view) return ai.b_->bt_allied(p, q);
        const int32_t n = ai.view_.allied_n;  // as TacticalBattle::bt_relation: unknown players are only allied with themselves
        if (p < 0 || q < 0 || p >= n || q >= n) return p == q;
        return ai.view_.allied[static_cast<size_t>(p * n + q)] != 0;
    }
    bool enemy(int32_t a, int32_t b) const {  // CombatSystem::is_enemy
        if (!view) return ai.b_->combat_->is_enemy(a, b);
        const int32_t n = ai.view_.players;
        return a >= 0 && b >= 0 && a < n && b < n && ai.view_.enemy[static_cast<size_t>(a * n + b)] != 0;
    }
    const LaneView::Pad* view_pad(objects::ObjectId pad) const {
        const auto& v = ai.view_.pads;
        const auto it = std::lower_bound(v.begin(), v.end(), pad, [](const LaneView::Pad& p, objects::ObjectId id) { return p.pad < id; });
        return it != v.end() && it->pad == pad ? &*it : nullptr;
    }
    objects::ObjectId pad_contents(objects::ObjectId pad) const {  // TacticalBattle::pad_contents
        if (!view) return ai.b_->pad_contents(pad);
        const LaneView::Pad* p = view_pad(pad);
        return p ? p->contents : objects::kNoObjectId;
    }
    bool in_cooldown(objects::ObjectId pad) const {  // the pad's rebuild cooldown runs past the battle frame
        if (!view) {
            const TacticalBattle::PadState* s = ai.b_->pad_state(pad);
            return s && s->cooldown_end > ai.b_->frame_;
        }
        const LaneView::Pad* p = view_pad(pad);
        return p && p->cooldown_end > ai.view_.frame;
    }
    bool rule_blocker(objects::ObjectHandle h, int32_t pad_owner) const {
        if (!view) return ai.b_->pad_rule_blocker(st, h, pad_owner);
        // as TacticalBattle::pad_rule_blocker (tactical_battle_pads.cpp), on the lane view
        static const objects::BehaviorTypeId kBaseShield = objects::behavior_type_id("BASE_SHIELD");
        static const objects::BehaviorTypeId kGroundStructure = objects::behavior_type_id("DUMMY_GROUND_STRUCTURE");
        if (!std::binary_search(ai.view_.live_units.begin(), ai.view_.live_units.end(), st.id(h))) return false;
        if (kBaseShield != objects::kNoBehavior && st.has_behavior(h, kBaseShield)) return false;
        if (kGroundStructure != objects::kNoBehavior && st.has_behavior(h, kGroundStructure)) return false;
        return !allied(pad_owner, st.owner(h));
    }
    std::string faction(int32_t player) const {
        if (view) return player >= 0 && static_cast<size_t>(player) < ai.view_.faction.size() ? ai.view_.faction[static_cast<size_t>(player)] : std::string();
        const auto& ps = ai.b_->galaxy_->players();
        if (player >= 0 && static_cast<size_t>(player) < ps.size() && ps[static_cast<size_t>(player)].faction < ai.b_->galaxy_->rules().factions.size())
            return ai.b_->galaxy_->rules().factions[ps[static_cast<size_t>(player)].faction].name;
        return {};
    }
    template <class F>
    void each_upgrade(F&& f) const {  // the station upgrade records (old, new), battle order
        if (view) {
            for (const auto& [from, to] : ai.view_.upgrades) f(from, to);
            return;
        }
        for (const TacticalBattle::StationUpgrade& u : ai.b_->station_upgrades()) f(u.old_station, u.new_station);
    }
};

BattleAi::FactEnv BattleAi::live_env() const { return FactEnv{*this, mode_->primary(), false}; }
BattleAi::FactEnv BattleAi::view_env() const { return FactEnv{*this, world_->view_store(), true}; }

// ---- ref5 t06: the AI lane's remaining host answers, from the capture ----------------------------------------------

void BattleAi::capture_lane_rest() {
    // Base shields: the ones l1_base_shield_covers counts (store order; the test is an any-of).
    view_.shields.clear();
    if (b_->l1_mode_ && !b_->space_) {
        static const objects::BehaviorTypeId kBaseShield = objects::behavior_type_id("BASE_SHIELD");
        const objects::ObjectStore& st = b_->l1_mode_->primary();
        if (kBaseShield != objects::kNoBehavior)
            for (const objects::ObjectHandle h : st.all_objects()) {
                if (!st.active(h) || !st.has_behavior(h, kBaseShield)) continue;
                if (b_->combat_->has_unit(st.id(h)) && !b_->combat_->base_shield_active(st.id(h))) continue;
                const float r = b_->catalog_->types().get(st.type(h), objects::tags::Base_Shield_Radius);
                if (!(r > 0.0f)) continue;
                const Vec3 c = st.position(h);
                view_.shields.push_back({c.x, c.y, c.z, r * r});
            }
    }
    // The land passability grid (copied when it changed since the last capture).
    if (!b_->space_ && b_->movement_) {
        const mv::PassabilityGrid& g = b_->movement_->grid();
        if (!view_.grid_valid || view_.grid_version != g.version() || view_.grid_w != g.width() || view_.grid_h != g.height()) {
            const std::span<const uint32_t> eff = g.effective_values();
            view_.grid_eff.assign(eff.begin(), eff.end());
            view_.grid_w = g.width();
            view_.grid_h = g.height();
            view_.grid_version = g.version();
            view_.grid_valid = true;
        }
    } else {
        view_.grid_valid = false;
        view_.grid_eff.clear();
    }
    // Combat hard points of every combat object (ascending id).
    view_.hp_index.clear();
    view_.hps.clear();
    if (const tc::CombatSystem* cs = b_->combat_.get()) {
        const objects::ObjectStore& st = mode_->primary();
        std::vector<objects::ObjectId> ids;
        for (const objects::ObjectHandle h : st.all_objects())
            if (cs->has_unit(st.id(h)) && cs->hardpoint_count(st.id(h)) > 0) ids.push_back(st.id(h));
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        std::vector<at::PerceptionHardPoint> one;
        for (const objects::ObjectId id : ids) {
            const TacticalBattle::Unit* u = b_->unit_by_id(id);
            combat_hard_points(*cs, id, u ? u->power : 0.0f, one);
            view_.hp_index.push_back({id, {static_cast<uint32_t>(view_.hps.size()), static_cast<uint32_t>(one.size())}});
            view_.hps.insert(view_.hps.end(), one.begin(), one.end());
        }
    }
    // Station upgrades (battle order).
    view_.upgrades.clear();
    for (const TacticalBattle::StationUpgrade& u : b_->station_upgrades()) view_.upgrades.emplace_back(u.old_station, u.new_station);
    // Skirmish production: every type hg_build_check can accept (in types_used_, a tactical queue), per AI player
    // (ref5 t07: the builders and can_build fact tables; can_build only where some builder lists the type).
    {
        std::vector<std::pair<int32_t, int32_t>> keys;
        if (b_->production_enabled()) {
            const objects::GameObjectTypes& types = b_->catalog_->types();
            std::vector<int32_t> cand;
            for (const uint32_t t : b_->types_used_)
                if (t < types.size() && b_->hg_queue_of(static_cast<objects::TypeIndex>(t)) >= 0) cand.push_back(static_cast<int32_t>(t));
            std::sort(cand.begin(), cand.end());
            cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
            for (int32_t p = 0; p < static_cast<int32_t>(host_->players()); ++p)
                if (controls(p))
                    for (const int32_t t : cand) keys.emplace_back(p, t);
        }
        facts_.builders.capture(keys);
        std::vector<std::pair<int32_t, int32_t>> listed;
        for (const auto& k : keys)
            if (!facts_.builders.get(true, k).empty()) listed.push_back(k);
        facts_.can_build.capture(std::move(listed));
    }
    // Reserve entries (the drop tests) and the playable box.
    view_.reserve.clear();
    for (const TacticalBattle::Participant& q : b_->parts_)
        if (q.reserve && !q.deployed)
            view_.reserve.push_back({q.galactic_id, q.owner, q.type, b_->space_ ? 0.0f : b_->drop_yaw(q.side)});
    view_.box = !b_->space_ && b_->map_.playable_box(view_.box_min, view_.box_max);
}

bool BattleAi::view_line_clear(int32_t object_id, const at::Vec3f& a, const at::Vec3f& b) const {
    if (!view_.grid_valid) return true;
    // The mover: the object itself when movement has it, else a TEAM container's first live member with an entry.
    const LaneView::Unit* mover = view_unit(static_cast<objects::ObjectId>(object_id));
    if (!mover || !mover->mv) {
        mover = nullptr;
        for (const LaneView::Unit& u : view_.units)
            if (u.live && u.mv && u.container < view_.teams.size() && view_.teams[u.container].id == object_id) {
                mover = &u;
                break;
            }
        if (!mover) return true;
    }
    const uint32_t mask = mover->mask;
    return mv::walk_cells(mv::V2{a.x, a.y}, mv::V2{b.x, b.y}, [&](int32_t cx, int32_t cy) {
        const bool in = cx >= 0 && cy >= 0 && static_cast<uint32_t>(cx) < view_.grid_w && static_cast<uint32_t>(cy) < view_.grid_h;
        const uint32_t cell = in ? view_.grid_eff[static_cast<size_t>(cy) * view_.grid_w + static_cast<size_t>(cx)] : mv::kOutOfRangeCell;
        return mv::passable(cell, mask);
    });
}

bool BattleAi::view_point_fogged(int32_t player, float x, float y) const {  // FogOfWar::point_fogged, force 1
    if (!view_.fog_live) return false;
    const auto pk = std::find(view_.fog_players.begin(), view_.fog_players.end(), player);
    if (pk == view_.fog_players.end()) return false;
    const tactical::fog::FogGrid& g = view_.fog_grids[static_cast<size_t>(pk - view_.fog_players.begin())];
    return g.valid() && g.fogged_at(x, y);
}

const BattleAi::LaneView::Reserve* BattleAi::view_reserve(int32_t player, int32_t key) const {
    for (const LaneView::Reserve& r : view_.reserve)
        if (r.key == static_cast<objects::ObjectId>(key) && r.owner == player) return &r;
    return nullptr;
}

// TacticalBattle::land_drop_valid's tests 1-2 (slots in the playable box, passable for their member, not fogged) on the
// capture. [port] tier 2, lane only: the footprint and reinforcement-radius tests (3-4) run at the Reinforce's apply
// (lane_drop_valid), so a spot they refuse fails the drop instead of moving it to the next ring spot.
bool BattleAi::view_land_drop_ok(int32_t player, int32_t key, const at::Vec3f& p) const {
    if (b_->space_ || !b_->movement_) return true;  // land only (as land_drop_valid)
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    const LaneView::Reserve* r = view_reserve(player, key);
    if (!r) return false;
    const objects::GameObjectTypes& types = b_->catalog_->types();
    const LandDropLayout lay = land_drop_layout(land_drop_shape(types, b_->catalog_, *b_->rules_, r->type), p.x, p.y, r->yaw);
    if (!lay.ok) return false;
    for (const LandDropSlot& s : lay.slots) {
        if (view_.box && (s.x < view_.box_min[0] || s.x > view_.box_max[0] || s.y < view_.box_min[1] || s.y > view_.box_max[1])) return false;
        if (s.type >= types.size()) return false;
        if (view_.grid_valid) {
            const mv::UnitDesc md = mv::unit_desc_from_type(types, s.type);
            const uint32_t mask = b_->movement_->classes().mask(md.movement_class);
            const int32_t cx = mv::world_to_cell(s.x), cy = mv::world_to_cell(s.y);
            const bool in = cx >= 0 && cy >= 0 && static_cast<uint32_t>(cx) < view_.grid_w && static_cast<uint32_t>(cy) < view_.grid_h;
            if (!mv::passable(in ? view_.grid_eff[static_cast<size_t>(cy) * view_.grid_w + static_cast<size_t>(cx)] : mv::kOutOfRangeCell, mask))
                return false;
        }
        if (view_point_fogged(player, s.x, s.y)) return false;
    }
    return true;
}

// space_drop_ok's box, prevention and reserve tests on the capture. [port] tier 2, lane only: the fog test and the
// occupancy sweep (movement over future frames) run at the Reinforce's apply (lane_drop_valid).
bool BattleAi::view_space_drop_ok(int32_t player, int32_t key, const at::Vec3f& p) {
    const tactical::Aabb& bb = mode_->bounds();
    if (!(p.x >= bb.min.x && p.x <= bb.max.x && p.y >= bb.min.y && p.y <= bb.max.y)) return false;
    if (prevention_hit_in(view_env(), player, p)) return false;  // the view's
    return view_reserve(player, key) != nullptr;
}

// A lane Reinforce at its apply (critical lane): the full drop test the AI's prediction left out. Space: the facing of
// the player's drop frame and the AI's sweep constant, as the ring search passes them.
bool BattleAi::lane_drop_valid(const cmd::Command& c) {
    const auto player = static_cast<int32_t>(c.header.player);
    const auto key = static_cast<int32_t>(c.payload.u("unit"));
    const at::Vec3f p{c.payload.f("x"), c.payload.f("y"), c.payload.f("z")};
    if (!b_->space_) return land_drop_valid(player, key, p, false);
    at::SpaceDropFrame f;
    if (!space_drop_frame(player, f)) f = at::SpaceDropFrame{};
    return space_drop_ok(player, key, p, f.entry_facing_deg, world_->space_reinforcement_sweep());
}

namespace {
constexpr uint16_t kLaneHostSubtype = 0x4148;  // "AH": ref5 t06, an AI host write (op, player, object, path)
constexpr uint8_t kHostUnitReleased = 1, kHostUnitLeftTaskForce = 2;
}  // namespace

void BattleAi::queue_host_release(uint8_t op, int32_t player, int32_t object_id, uint8_t path) {
    ByteWriter w;
    w.u8(op).i32(player).i32(object_id).u8(path);
    cmd::Command c;
    c.header.player = static_cast<uint16_t>(player >= 0 && player < static_cast<int32_t>(kMaxPlayers) ? player : 0);
    c.header.source = cmd::CommandSource::AI;
    c.header.sequence = lane_seq_++;
    c.header.issue_tick = host_->frame();
    c.header.scheduled_tick = host_->frame() + world_->k_ai();
    c.payload = cmd::ev::port_extension(kLaneHostSubtype, w.bytes());
    lane_job_.batch.push_back(std::move(c));
}

void BattleAi::apply_host_release(const cmd::Command& c) {
    ByteReader r(c.payload.bytes_view("data"));
    const uint8_t op = r.u8();
    const int32_t player = r.i32();
    const int32_t id = r.i32();
    const uint8_t path = r.u8();
    if (!r.ok() || !r.done()) return;
    if (op == kHostUnitReleased && path <= static_cast<uint8_t>(at::TacticalAiHost::UnitRelease::TaskForce))
        unit_released_live(player, id, static_cast<at::TacticalAiHost::UnitRelease>(path));
    else if (op == kHostUnitLeftTaskForce)
        unit_left_task_force_live(id);
}

// ---- order execution ----------------------------------------------------------------------------------------------

bool BattleAi::move_unit(objects::ObjectId id, float x, float y, float threat_threshold, uint8_t flags, objects::ObjectId goal_object,
                         bool object_block) {
    const TacticalBattle::Unit* u = b_->unit_by_id(id);
    if (!u || !u->alive || u->retreated || !u->movable) return false;
    if (b_->movement_locked(id)) return true;  // OI follow-up t03: the locked unit keeps its order (A1_READINGS §28.9)
    const tactical::Aabb& bb = mode_->bounds();
    mv::MoveOrder o;
    o.units = {id};
    o.destination = {std::clamp(x, bb.min.x, bb.max.x), std::clamp(y, bb.min.y, bb.max.y)};
    o.threat_threshold = threat_threshold;
    o.flags = flags;  // s78f t10: the order's block flags (0x01 attack-move / 0x02 guard)
    o.goal_object = goal_object == objects::kNoObjectId ? -1 : goal_object;  // s78f t10: the object block (movement: -1 = none)
    // s78n t20 (MOVEMENT 4.3.1 items 2 / 6): an object goal (block kind 3) takes neither the destination check nor the
    // single-member shortcut. s78n t25.3: land attack blocks are TacticalBattle::land_attack_order; what is left here on
    // land is the a1h follow and the Attack_Move blocks on a non-attackable object ([port]; s78n t25.4: Guard_Target on an
    // object is a guard block of land_attack_order). The check's object test has no exemption for the order's target (retail sends kind 3 to
    // the attack-arc slot test 0x438c10, which leaves the target out), so run on an object goal it moves the goal off
    // the very object the units were sent to: attackers end out of weapon range of a standing enemy and never fire.
    o.skip_destination_check = object_block || goal_object != objects::kNoObjectId;
    b_->movement_->move(o, host_->random().synced(), b_->frame_);
    return true;
}

bool BattleAi::is_fighter(objects::ObjectId id) const {
    return b_->space_ && b_->movement_->has_unit(id) && b_->movement_->view(id).locomotor == mv::LocomotorKind::Fighter;
}

// ---- a1h t02: object-goal follow (A1_READINGS §33.4, rule §33.5.2) ----------------------------------------------------

void BattleAi::follow_drop(const std::vector<objects::ObjectId>& units) {
    b_->team_attack_drop(units);  // q6: a newer order ends an attack on a TEAM container (signal 0xb)
    for (ObjectFollow& f : follows_)
        f.units.erase(std::remove_if(f.units.begin(), f.units.end(),
                                     [&units](objects::ObjectId id) { return std::find(units.begin(), units.end(), id) != units.end(); }),
                      f.units.end());
    follows_.erase(std::remove_if(follows_.begin(), follows_.end(), [](const ObjectFollow& f) { return f.units.empty(); }), follows_.end());
}

void BattleAi::follow_add(const std::vector<objects::ObjectId>& units, objects::ObjectId target, float x, float y, float threat,
                          bool guard) {
    if (units.empty()) return;
    const TacticalBattle::Unit* lead = b_->unit_by_id(units.front());
    ObjectFollow f;
    f.units = units;
    f.target = target;
    f.gx = x;
    f.gy = y;
    // R = half the lead member's max attack range (0x3749d0 / 2); cmb1 t04 (FIGHTER_FLYING §4.11 G 1 [H]): a land guard top
    // caps it at Land_Guard_Range (GC +0x11ec4, 150)
    f.r = lead ? 0.5f * lead->range : 0.0f;
    if (guard && !b_->space_) f.r = std::min(f.r, b_->movement_->constants().land_guard_range);
    f.threat = threat;
    f.guard = guard;  // s78n t25.4
    f.next = b_->frame_ + 30;  // the formation update runs at most once per FPS frames
    follows_.push_back(std::move(f));
}

void BattleAi::follow_service() {
    const objects::ObjectStore& st = mode_->primary();
    const tactical::Aabb& bb = mode_->bounds();
    for (ObjectFollow& f : follows_) {
        f.units.erase(std::remove_if(f.units.begin(), f.units.end(),
                                     [this](objects::ObjectId id) {
                                         const TacticalBattle::Unit* u = b_->unit_by_id(id);
                                         return !u || !u->alive || u->retreated || !u->movable;
                                     }),
                      f.units.end());
        const objects::ObjectHandle th = st.find(f.target);
        const TacticalBattle::Unit* tu = b_->unit_by_id(f.target);
        if (!st.alive(th) || (tu && (!tu->alive || tu->retreated))) f.units.clear();  // signal 0x10: the goal ends
        if (f.units.empty()) continue;
        // cmb1 t04: a land guard's formation step runs every tick (C 3), its re-path / re-check on the 30-frame timer
        if (f.guard && !b_->space_) {
            guard_follow_step(f, st.position(th));
            continue;
        }
        if (b_->frame_ < f.next) continue;
        f.next = b_->frame_ + 30;
        // cmb1 t01 (FIGHTER_FLYING §4.11 B 3 [H]): while an auto attack block is on top of the object block, the follow
        // (0x4192b0 step 3) is skipped; the block's own checks run (TacticalBattle::land_engage_service)
        if (!b_->space_ && b_->land_auto_pushed(f.units.front())) continue;
        // [port] the object's goal point (0x583150) is read as its position; the path copy of a moving object (0x40dbc0)
        // is the same re-path.
        const Vec3 tp = st.position(th);
        const float dx = tp.x - f.gx, dy = tp.y - f.gy;
        if (dx * dx + dy * dy <= f.r * f.r) continue;
        // mcx6 t06 (LUA_API.md "Readers of UnitAI +0x38" 2, FIGHTER_FLYING §4.11 E): members[0]'s UNIT_AI vetoes the
        // re-path (0x4f5780) while its attack-positioning flag is set (Keep_Moving_In_Battle: unset in FoC, not read).
        // [port] a TEAM member holds no UNIT_AI of its own here: its container's flag is read. Space never sets it.
        if (!b_->space_) {
            const objects::ObjectId lead = f.units.front();
            const objects::ObjectHandle lh = st.find(lead);
            const objects::ObjectHandle ch = st.exists(lh) ? st.container(lh) : objects::ObjectHandle{};
            if (world_->attack_positioning(lead) || (st.exists(ch) && world_->attack_positioning(st.id(ch)))) continue;
        }
        f.gx = tp.x;
        f.gy = tp.y;
        // s78n t20 fix (MOVEMENT 4.3.1 item 2): the follow stands for the formation's re-path of a kind-3 goal
        // (0x4192b0) - no destination check. (s78n t26 review round 2: Move_To(object) adds no follow, it is kind 1.)
        if (f.units.size() == 1) {
            move_unit(f.units.front(), tp.x, tp.y, f.threat, 0, objects::kNoObjectId, true);
            continue;
        }
        mv::MoveOrder o;
        o.units = f.units;
        o.threat_threshold = f.threat;
        o.destination = {std::clamp(tp.x, bb.min.x, bb.max.x), std::clamp(tp.y, bb.min.y, bb.max.y)};
        o.skip_destination_check = true;
        b_->movement_->move(o, host_->random().synced(), b_->frame_);
    }
    follows_.erase(std::remove_if(follows_.begin(), follows_.end(), [](const ObjectFollow& f) { return f.units.empty(); }), follows_.end());
}

// cmb1 t04 (b0070 / b0200; FIGHTER_FLYING §4.11 A / C 3 / G 1, BKR2 Q6 [H]): one formation step of a land guard follow (the
// guard block's object f.target; tp its position). A re-plan is the guard block's (MOVEMENT §4.3.2 item 8: items 6 / 7 again,
// the slot and the formations kept; TacticalBattle::land_attack_order guard, replan).
//   - an auto attack block on top (land_engage_service): nothing - step 3 is skipped while it is the top;
//   - arrived (no mover has a goal) and the object hidden for the lead's owner or stealthed against its type (C 3, state 3,
//     every step): P = the guard's last-observed point (LandFormationRecord::seen), d = 2-D |L - P| (L the movers' centroid):
//     d >= 40 -> the plan toward P now; d < 40 -> a position guard at L (flags 0x02 kept: the record's goal L, no object;
//     every unit's attack order and attack goal cleared, the movement's attack block dropped, the plan to L), the follow ends;
//   - every 30 frames (F +0x1d8), moving (0x4192b0 step 3, no fog test): D = |recorded goal - P| > R (R = min(half the lead's
//     range, Land_Guard_Range 150), G 1) -> re-plan toward P (P the last-observed point, below);
//   - every 30 frames, arrived (0x410a60 with a guard top: a is always true): a mover's lead with TARGETING out of its max
//     range or inside its min range of the object, or with its line of fire blocked (0x40edd0: CombatSystem::can_hit) ->
//     re-plan toward P. A collision-owned top record (pushed path) waits.
void BattleAi::guard_follow_step(ObjectFollow& f, Vec3 tp) {
    const objects::ObjectStore& st = mode_->primary();
    if (b_->land_auto_pushed(f.units.front())) return;
    std::vector<objects::ObjectId> movers;
    bool moving = false, pushed = false, failed = false;
    for (const objects::ObjectId id : f.units) {
        const objects::ObjectId m = b_->attack_mover(id);
        if (!b_->movement_->has_unit(m) || std::find(movers.begin(), movers.end(), m) != movers.end()) continue;
        movers.push_back(m);
        const mv::UnitView v = b_->movement_->view(m);
        moving = moving || v.has_goal;
        pushed = pushed || v.pushed;
        failed = failed || v.failed;
    }
    if (movers.empty()) return;
    // cmb2 t03 (BKR2 Q7 item 6 [H]): after a failed plan the formation is in state 0 until 2 x FPS < now - the failure
    // frame (f.next, stamped by replan below): no C 3, no check
    if (failed && !moving && b_->frame_ < f.next) return;
    const objects::ObjectId lead_id = b_->attack_lead(movers.front());
    const TacticalBattle::Unit* lead = lead_id == objects::kNoObjectId ? nullptr : b_->unit_by_id(lead_id);
    if (!lead) return;
    SimRng& rng = host_->random().synced();
    const auto replan = [&](Vec3 goal) {
        f.gx = goal.x;
        f.gy = goal.y;
        // cmb2 t03 (BKR2 Q7 item 6 [H]): a failed plan keeps the guard block; the next try waits until 2 x FPS < now - frame
        if (b_->land_attack_order(*mode_, f.units, f.target, goal, mv::kMoveFlagGuardMove, true, rng, true) ==
            TacticalBattle::AttackPlan::Failed)
            f.next = b_->frame_ + 61;
    };
    // BKR2 Q6 [H] (review): every plan, re-path and C 3 test reads the guard's last-observed point P - seen: the object's
    // position (tp); hidden for the lead's owner: frozen (LandFormationRecord::seen); stealthed against the lead's type: the
    // formation lead's position (the mover nearest the movers' centroid L, strict <, the first wins)
    mv::V2 L{0.0f, 0.0f};
    for (const objects::ObjectId m : movers) L = L + b_->movement_->view(m).pos;
    L = L * (1.0f / static_cast<float>(movers.size()));
    bool hidden = false, stealthed = false;
    b_->attack_target_unseen(st, *lead, f.target, hidden, stealthed);
    Vec3 P = tp;
    if (hidden) {
        if (const LandFormationRecord* r = b_->land_formation(movers.front())) P = r->seen;
    } else if (stealthed) {
        objects::ObjectId near = movers.front();
        float best = 3.0e38f;
        for (const objects::ObjectId m : movers) {
            const mv::V2 q = b_->movement_->view(m).pos;
            const float dd = (q.x - L.x) * (q.x - L.x) + (q.y - L.y) * (q.y - L.y);
            if (dd < best) {
                best = dd;
                near = m;
            }
        }
        const objects::ObjectId nl = b_->attack_lead(near);
        const TacticalBattle::Unit* nu = nl == objects::kNoObjectId ? nullptr : b_->unit_by_id(nl);
        P = nu ? st.position(nu->h) : Vec3{b_->movement_->view(near).pos.x, b_->movement_->view(near).pos.y, 0.0f};
    }
    if (!moving && !pushed) {
        if (hidden || stealthed) {
            const float dx = L.x - P.x, dy = L.y - P.y;
            if (dx * dx + dy * dy >= 40.0f * 40.0f) {
                f.next = b_->frame_ + 30;
                ++b_->stats_.guard_unseen_plans;
                replan(P);
                return;
            }
            std::vector<objects::ObjectId> us;
            for (const objects::ObjectId m : movers)
                for (const objects::ObjectId u : b_->land_mover_units(m)) {
                    us.push_back(u);
                    if (b_->combat_->has_unit(u)) {
                        b_->combat_->clear_order(u);
                        b_->combat_->set_attack_goal(u, tactical::combat::kNoObject);
                    }
                }
            b_->movement_->clear_attack_block(movers);
            b_->land_order(us, mv::kMoveFlagGuardMove, Vec3{L.x, L.y, 0.0f});
            mv::MoveOrder o;
            for (const objects::ObjectId u : us)
                if (!b_->movement_locked(u)) o.units.push_back(u);
            if (!o.units.empty()) {
                const tactical::Aabb& bb = mode_->bounds();
                o.destination = {std::clamp(L.x, bb.min.x, bb.max.x), std::clamp(L.y, bb.min.y, bb.max.y)};
                o.flags = mv::kMoveFlagGuardMove;
                b_->movement_->move(o, rng, b_->frame_);
            }
            ++b_->stats_.guard_unseen_drops;
            f.units.clear();
            return;
        }
    }
    if (b_->frame_ < f.next) return;
    f.next = b_->frame_ + 30;
    if (moving) {
        const float dx = P.x - f.gx, dy = P.y - f.gy;
        if (dx * dx + dy * dy <= f.r * f.r) return;
        ++b_->stats_.guard_repaths;
        replan(P);
        return;
    }
    if (pushed) return;
    // a TEAM container object is tested through a member (§4.11 D 7: the viewer's TARGETING pick; [port] its first live one)
    const objects::ObjectId tgt = b_->movement_->is_land_team_container(f.target) ? b_->attack_lead(f.target) : f.target;
    if (tgt == objects::kNoObjectId) return;
    for (const objects::ObjectId m : movers) {
        const objects::ObjectId id = b_->attack_lead(m);
        if (id == objects::kNoObjectId || !b_->combat_->has_unit(id)) continue;
        const tactical::combat::CanHit c = b_->attack_can_hit(id, tgt);
        if (c == tactical::combat::CanHit::Yes || c == tactical::combat::CanHit::NoTarget) continue;
        ++b_->stats_.guard_rechecks;
        replan(P);
        return;
    }
}

// S7 t26 (AI_TACTICAL §7.3.3 item 2, A1_READINGS §33.3): the orders executed here (group / unit moves, MoveToObject, attack,
// the Lua object orders Move_To / Attack_Move / Guard_Target / Stop) raise signal 0xb on each unit they take (TEAM
// containers: their members; the world adds the container), which ends the Activate_Ability blocks waiting on it.
// S7 t31: raised through TacticalBattle::raise_order_replaced, as the human commands and the story orders are.
bool BattleAi::hostile_owner(uint8_t player, int32_t owner) const {
    // S7 t30: the relation table (TACTICAL_COMBAT §2.10) already makes an Is_Neutral player relation 2 (not enemy).
    return b_->combat_->is_enemy(static_cast<int32_t>(player), owner);
}

void BattleAi::order_signals(uint8_t player, const cmd::EventData& ev) {
    using T = cmd::EventType;
    std::vector<uint32_t> wire;
    switch (ev.type()) {
        case T::MoveToPositionFacing: wire = ev.ids("units"); break;
        case T::MoveToObject: wire = ev.ids("movers"); break;
        case T::Attack: wire = ev.ids("attackers"); break;
        case T::PortExtension: {
            const uint32_t sub = ev.u("subtype");
            const auto b = ev.bytes_view("data");
            if (sub < lua::kLuaOrderSubtype || sub > lua::kLuaOrderSubtype + 0xFFu || b.size() < lua::kLuaOrderPayloadBytes) return;
            const auto order = static_cast<lua::LuaOrder>(sub - lua::kLuaOrderSubtype);
            if (order != lua::LuaOrder::MoveTo && order != lua::LuaOrder::AttackMove && order != lua::LuaOrder::Guard &&
                order != lua::LuaOrder::Stop)
                return;
            wire.push_back(le32(b, 0));
            for (size_t off = lua::kLuaOrderPayloadBytes; off + 4 <= b.size(); off += 4) wire.push_back(le32(b, off));
            break;
        }
        default: return;
    }
    std::vector<objects::ObjectId> done;
    for (const uint32_t w : wire)
        for (const objects::ObjectId id : team_units(player, w))
            if (std::find(done.begin(), done.end(), id) == done.end()) done.push_back(id);
    b_->raise_order_replaced(done);  // S7 t31: every listener (this world, the story bridge)
}

cmd::RejectReason BattleAi::execute(uint8_t player, const cmd::EventData& ev) {
    facts_.order_refused.clear();
    facts_.wrote();  // ref5 t07
    if (world_ && world_->ai_lane()) {  // sp3 t04: queued for the batch due N + k_ai
        // sp3 slice 4c: an event execute_order does not act on is not run from the AI side either
        if (!lane_batched(ev)) return cmd::RejectReason::Unhandled;
        const cmd::RejectReason p = predict_order(player, ev);
        if (p == cmd::RejectReason::None) {
            cmd::Command c;
            c.header.player = player;
            c.header.source = cmd::CommandSource::AI;
            c.header.sequence = lane_seq_++;
            c.header.issue_tick = host_->frame();
            c.header.scheduled_tick = host_->frame() + world_->k_ai();
            c.payload = ev;
            lane_job_.batch.push_back(std::move(c));  // visible when the job is collected
        }
        return p;
    }
    return execute_live(player, ev);
}

cmd::RejectReason BattleAi::execute_live(uint8_t player, const cmd::EventData& ev) {
    const cmd::RejectReason r = execute_order(player, ev);
    if (r == cmd::RejectReason::None) order_signals(player, ev);
    count_executed(ev, r);
    return r;
}

void BattleAi::count_executed(const cmd::EventData& ev, cmd::RejectReason r) {
    std::string key(ev.def().name);
    if (ev.type() == cmd::EventType::PortExtension) key += "." + std::to_string(ev.u("subtype") & 0xFFu);
    auto& e = executed_[key + (r == cmd::RejectReason::None ? "" : std::string(" ") + std::string(cmd::reject_reason_name(r)))];
    ++(r == cmd::RejectReason::None ? e.first : e.second);
}

// ---- sp3 t04: the AI lane's command batch ---------------------------------------------------------------------------

bool BattleAi::lane_batched(const cmd::EventData& ev) {
    using T = cmd::EventType;
    switch (ev.type()) {
        case T::MoveToPositionFacing:
        case T::MoveToObject:
        case T::Attack:
        case T::Reinforce:  // sp3 slice 4d
        case T::Retreat:  // sp3 slice 4c: every other type execute_order acts on
        case T::Production:
        case T::TacticalBuild:
        case T::SetUnitAbilityMode:
        case T::TacticalSpecialAbility:
        case T::TacticalSpecialAbilityWithDummyTarget:
        case T::SetAbilityAutofire:
        case T::Facing:  // ref2 t06: the types execute_order now passes to TacticalBattle::apply_order
        case T::Garrison:
        case T::MoveToGarrison:
        case T::TacticalSell: return true;
        case T::PortExtension: {
            const uint32_t sub = ev.u("subtype");
            if (sub < lua::kLuaOrderSubtype || sub > lua::kLuaOrderSubtype + 0xFFu || ev.bytes_view("data").size() < lua::kLuaOrderPayloadBytes)
                return false;
            const auto order = static_cast<lua::LuaOrder>(sub - lua::kLuaOrderSubtype);
            return order == lua::LuaOrder::MoveTo || order == lua::LuaOrder::AttackMove || order == lua::LuaOrder::Guard ||
                   order == lua::LuaOrder::Stop || order == lua::LuaOrder::HyperspaceAway || order == lua::LuaOrder::CancelHyperspace;
        }
        default: return false;
    }
}

cmd::RejectReason BattleAi::predict_order(uint8_t player, const cmd::EventData& ev) {
    using R = cmd::RejectReason;
    using T = cmd::EventType;
    // sp3 slice 4c: reads the lane view and the snapshot store only (finished_ is set on the critical lane before the
    // AI slot of the deciding tick).
    if (b_->finished_) return R::NotAllowed;
    const objects::ObjectStore& st = world_->view_store();
    const auto movable = [this](objects::ObjectId id) {
        const LaneView::Unit* u = view_unit(id);
        return u && u->movable;
    };
        switch (ev.type()) {
        case T::MoveToPositionFacing: {
            bool any = false;
            for (const uint32_t id : ev.ids("units"))
                for (const objects::ObjectId mid : view_team_units(player, id)) any = any || movable(mid);
            if (!any) return R::InvalidUnit;
            return std::isfinite(ev.f("x")) && std::isfinite(ev.f("y")) ? R::None : R::InvalidPosition;
        }
        case T::MoveToObject: {
            const uint32_t tw = ev.u("target");
            const objects::ObjectHandle th = tw <= 0x7FFFFFFFu ? st.find(static_cast<objects::ObjectId>(tw)) : objects::ObjectHandle{};
            if (!st.alive(th)) return R::None;  // a silent no-op when applied
            for (const uint32_t id : ev.ids("movers"))
                for (const objects::ObjectId mid : view_team_units(player, id))
                    if (mid != static_cast<objects::ObjectId>(tw) && movable(mid)) return R::None;
            return R::InvalidUnit;
        }
        case T::Attack: {
            const uint32_t tw = ev.u("target");
            const LaneView::Unit* t = tw <= 0x7FFFFFFFu ? view_unit(static_cast<objects::ObjectId>(tw)) : nullptr;
            bool team = false;
            if (!t && tw <= 0x7FFFFFFFu)
                for (const LaneView::Team& c : view_.teams)
                    if (c.id == static_cast<objects::ObjectId>(tw)) {
                        team = c.target != 0;
                        break;
                    }
            const objects::ObjectHandle th = team ? st.find(static_cast<objects::ObjectId>(tw)) : objects::ObjectHandle{};
            if (team ? !view_hostile(player, st.exists(th) ? st.owner(th) : -1) : (!t || !t->live || !view_hostile(player, t->owner)))
                return R::InvalidTarget;
            for (const uint32_t id : ev.ids("attackers"))
                if (!view_team_units(player, id).empty()) return R::None;
            return R::InvalidUnit;
        }
        case T::PortExtension: {
            const auto b = ev.bytes_view("data");
            const auto order = static_cast<lua::LuaOrder>(ev.u("subtype") - lua::kLuaOrderSubtype);
            std::vector<objects::ObjectId> ids = view_team_units(player, le32(b, 0));
            for (size_t off = lua::kLuaOrderPayloadBytes; off + 4 <= b.size(); off += 4)
                for (const objects::ObjectId mid : view_team_units(player, le32(b, off))) ids.push_back(mid);
            if (ids.empty()) return R::InvalidUnit;
            if (order == lua::LuaOrder::Stop || order == lua::LuaOrder::HyperspaceAway) return R::None;  // ref2 t06
            if (order == lua::LuaOrder::CancelHyperspace) return R::NotAllowed;  // ref2 t06: the documented refusal
            bool any = false;
            for (const objects::ObjectId id : ids) any = any || movable(id);
            if (!any) return R::InvalidUnit;
            // An object goal: a dead target is a silent no-op, a live one gives the object's position (refusals of a
            // task-force Attack_Move by every unit are not predicted: the apply reports them, t05).
            if (le32(b, 4) != cmd::kNoObject) return R::None;
            return std::isfinite(std::bit_cast<float>(le32(b, 8))) && std::isfinite(std::bit_cast<float>(le32(b, 12))) ? R::None
                                                                                                                       : R::InvalidPosition;
        }
        // sp3 slice 4c: the rest of the batched types, from the view (the apply reports a differing answer)
        case T::Reinforce: {  // slice 4d: in the pool, room (can_reinforce, land population), a finite point
            const auto key = static_cast<int32_t>(ev.u("unit"));
            const int32_t pi = player;  // ref5 t07: the facts' capture (a player it did not list: no reinforcing)
            if (!facts_.can_reinforce(pi)) return R::NotAllowed;
            const std::vector<at::TacticalReinforcement> pool = facts_.reinforcement_pool(pi);
            const auto it = std::find_if(pool.begin(), pool.end(), [key](const at::TacticalReinforcement& e) { return e.key == key; });
            if (it == pool.end()) return R::NotAllowed;
            const float cap = facts_.population_cap(pi);
            if (!b_->space_ && cap >= 0.0f && facts_.population_used(pi) + it->population > cap) return R::QueueFull;
            return std::isfinite(ev.f("x")) && std::isfinite(ev.f("y")) ? R::None : R::InvalidPosition;
        }
        case T::Retreat:
            return static_cast<size_t>(player) < view_.retreat_ok.size() && view_.retreat_ok[player] ? R::None : R::NotAllowed;
        case T::Production: {
            if (!view_.production) return R::Unhandled;
            const uint32_t loc = ev.u("location");
            return loc == cmd::kNoObject || loc > 0x7FFFFFFFu ? R::InvalidTarget : R::None;
        }
        case T::TacticalBuild: {
            const uint32_t pw = ev.u("pad");
            if (pw == cmd::kNoObject || pw > 0x7FFFFFFFu) return R::InvalidTarget;
            // the pad checks the AI's build_pad_check runs (relation, contents, rebuild cooldown); cost and type are not
            const at::PadCheck c = facts_.build_pad_check(static_cast<int32_t>(player), static_cast<int32_t>(pw),
                                                          static_cast<int32_t>(ev.u("type_index")));
            return c == at::PadCheck::Ok ? R::None : R::NotAllowed;
        }
        case T::SetUnitAbilityMode:
        case T::TacticalSpecialAbility:
        case T::TacticalSpecialAbilityWithDummyTarget:
        case T::SetAbilityAutofire: {
            const bool users = ev.type() == T::TacticalSpecialAbility || ev.type() == T::TacticalSpecialAbilityWithDummyTarget;
            for (const uint32_t w : ev.ids(users ? "users" : "units")) {
                const objects::ObjectHandle h = w <= 0x7FFFFFFFu ? st.find(static_cast<objects::ObjectId>(w)) : objects::ObjectHandle{};
                if (st.alive(h) && st.owner(h) == static_cast<int32_t>(player)) return R::None;
            }
            return R::InvalidUnit;
        }
        // ref2 t06: the types apply_order takes for the AI - an owned unit in the view (the apply reports a differing answer)
        case T::Facing:
        case T::Garrison:
        case T::TacticalSell: {
            std::vector<uint32_t> wire = ev.type() == T::Facing ? ev.ids("units") : std::vector<uint32_t>{ev.u(ev.type() == T::Garrison ? "unit" : "object")};
            for (const uint32_t w : wire) {
                const objects::ObjectHandle h = w <= 0x7FFFFFFFu ? st.find(static_cast<objects::ObjectId>(w)) : objects::ObjectHandle{};
                if (st.alive(h) && st.owner(h) == static_cast<int32_t>(player)) return R::None;
            }
            return R::InvalidUnit;
        }
        case T::MoveToGarrison: return R::None;
        default: return R::Unhandled;
    }
}

const BattleAi::LaneView::Unit* BattleAi::view_unit(objects::ObjectId id) const {
    const auto it = std::lower_bound(view_.units.begin(), view_.units.end(), id, [](const LaneView::Unit& u, objects::ObjectId v) { return u.id < v; });
    return it != view_.units.end() && it->id == id ? &*it : nullptr;
}

std::vector<objects::ObjectId> BattleAi::view_team_units(uint8_t player, uint32_t wire) const {  // team_units on the view
    std::vector<objects::ObjectId> out;
    if (wire == cmd::kNoObject || wire > 0x7FFFFFFFu) return out;
    const auto id = static_cast<objects::ObjectId>(wire);
    if (const LaneView::Unit* u = view_unit(id)) {
        if (u->live && u->owner == static_cast<int32_t>(player)) out.push_back(u->id);
        return out;
    }
    for (size_t c = 0; c < view_.teams.size(); ++c) {
        if (view_.teams[c].id != id || view_.teams[c].removed) continue;
        for (const LaneView::Unit& m : view_.units)
            if (m.container == c && m.live && m.owner == static_cast<int32_t>(player)) out.push_back(m.id);
        break;
    }
    return out;
}

bool BattleAi::view_hostile(uint8_t player, int32_t owner) const {  // hostile_owner (CombatSystem::is_enemy) on the view
    const size_t n = view_.retreat_ok.size();
    if (owner < 0 || static_cast<size_t>(owner) >= n || player >= n) return false;
    return view_.enemy[static_cast<size_t>(player) * n + static_cast<size_t>(owner)] != 0;
}

void BattleAi::apply_lane_batch(uint64_t frame) {
    if (lane_job_.pending && lane_job_.frame + world_->k_ai() <= frame) lane_collect();  // k_ai = 1: due before the next update
    if (lane_batch_.empty()) return;
    std::vector<cmd::Command> due, later;
    for (cmd::Command& c : lane_batch_) (c.header.scheduled_tick <= frame ? due : later).push_back(std::move(c));
    lane_batch_ = std::move(later);
    std::stable_sort(due.begin(), due.end(), [](const cmd::Command& a, const cmd::Command& b) { return cmd::drain_order_less(a.header, b.header); });
    for (const cmd::Command& c : due) {
        if (lane_write_command(c.payload)) {  // sp3 slice 4b: a binding's queued write
            apply_lane_write(c);
            count_executed(c.payload, cmd::RejectReason::None);
            continue;
        }
        if (c.payload.type() == cmd::EventType::PortExtension && c.payload.u("subtype") == kLaneGlobalSubtype) {  // slice 4e
            std::string name;
            lua::AiGlobalValue v;
            if (decode_global(c.payload, name, v)) {
                if (lua::GlobalValueStore* gv = host_->global_values()) gv->set(name, v);
            }
            count_executed(c.payload, cmd::RejectReason::None);
            continue;
        }
        if (c.payload.type() == cmd::EventType::PortExtension && c.payload.u("subtype") == kLaneHostSubtype) {  // ref5 t06
            apply_host_release(c);
            count_executed(c.payload, cmd::RejectReason::None);
            continue;
        }
        facts_.order_refused.clear();
        const bool reinforce = c.payload.type() == cmd::EventType::Reinforce;
        const auto player = static_cast<int32_t>(c.header.player);
        const size_t before = reinforce ? mode_->primary().owned_by(player).size() : 0;
        // ref5 t06: the drop tests the AI's prediction left out (the drop searches run here, on the critical lane)
        const cmd::RejectReason r = reinforce && !lane_drop_valid(c) ? cmd::RejectReason::InvalidPosition
                                                                     : execute_order(static_cast<uint8_t>(c.header.player), c.payload);  // uint8_t ids: lossless
        if (r == cmd::RejectReason::None) order_signals(static_cast<uint8_t>(c.header.player), c.payload);
        else ++lane_mismatch_;  // queued only when predicted accepted
        count_executed(c.payload, r);
        if (reinforce) {  // slice 4d: the AI waits for an unload record of every lane drop
            const auto key = static_cast<int32_t>(c.payload.u("unit"));
            if (r == cmd::RejectReason::None && landing_in_flight(player, key)) continue;  // the battle's signal 0x19 comes later
            ai::tactical::ReinforcementsUnloaded rec;
            rec.player = player;
            rec.key = key;
            if (r == cmd::RejectReason::None) {
                const objects::ObjectStore& st = mode_->primary();
                const auto owned = st.owned_by(player);
                for (size_t k = before; k < owned.size(); ++k)
                    if (st.active(owned[k]) && st.has_behavior(owned[k], objects::beh::Locomotor)) rec.units.push_back(st.id(owned[k]));
            }
            lane_unloaded_.push_back(std::move(rec));
        }
    }
}

cmd::RejectReason BattleAi::execute_order(uint8_t player, const cmd::EventData& ev) {
    using R = cmd::RejectReason;
    using T = cmd::EventType;
    if (b_->finished_) return R::NotAllowed;
    objects::ObjectStore& st = mode_->primary();
    // battle host: unit orders resolve through team_units (a unit, or a TEAM container's live members).
    switch (ev.type()) {
        case T::MoveToPositionFacing: {
            const tactical::Aabb& bb = mode_->bounds();
            mv::MoveOrder o;
            for (const uint32_t id : ev.ids("units"))  // battle host: TEAM containers -> their members
                for (const objects::ObjectId mid : team_units(player, id))
                    if (b_->unit_by_id(mid)->movable && std::find(o.units.begin(), o.units.end(), mid) == o.units.end()) o.units.push_back(mid);
            if (o.units.empty()) return R::InvalidUnit;
            const float x = ev.f("x"), y = ev.f("y");
            if (!std::isfinite(x) || !std::isfinite(y)) return R::InvalidPosition;
            b_->drop_locked(o.units);  // OI follow-up t03: the group move leaves locked units out
            if (o.units.empty()) return R::None;
            o.destination = {std::clamp(x, bb.min.x, bb.max.x), std::clamp(y, bb.min.y, bb.max.y)};
            follow_drop(o.units);
            for (const objects::ObjectId mid : o.units) if (b_->abilities_) b_->abilities_->move_ordered(mid, b_->frame_);  // mcx4: Terminate_Effect_On_Move_Command (MCX4_T10_RE_ANSWERS §13)
            b_->movement_->move(o, host_->random().synced(), b_->frame_);
            for (const objects::ObjectId id : o.units) {
                b_->combat_->clear_order(id);
                b_->combat_->set_attack_goal(id, tc::kNoObject);  // refixf t06: a position block
            }
            b_->fighter_order(o.units, 0, Vec3{o.destination.x, o.destination.y, 0.0f});  // S7 t22: the squadrons' formation
            return R::None;
        }
        case T::MoveToObject: {
            // Lua Move_To(object) without a threat threshold (ev_move_to_object): a move to a snapshot of the target's
            // position, as the LuaOrder MoveTo path. s78n t26 review round 2 (LUA_API "Move_To / Attack_Move / Guard_Target"
            // [H]; TACTICAL_COMBAT §2.8 "Summary by order"): Move_Internal's flags 0 give a kind-1 position block, which is
            // never followed (A1_READINGS §33.4: the 30-frame re-path follows kind-3 goals only) - no follow record; the
            // destination check and the single-member shortcut apply as for a position.
            const uint32_t tw = ev.u("target");
            const objects::ObjectHandle th = tw <= 0x7FFFFFFFu ? st.find(static_cast<objects::ObjectId>(tw)) : objects::ObjectHandle{};
            if (!st.alive(th)) {
                ++stale_;
                return R::None;  // COMMANDS.md sec. 6.2: a missing / dying target is a silent no-op
            }
            mv::MoveOrder o;
            for (const uint32_t id : ev.ids("movers"))  // battle host: TEAM containers -> their members
                for (const objects::ObjectId mid : team_units(player, id))
                    if (mid != static_cast<objects::ObjectId>(tw) && b_->unit_by_id(mid)->movable &&
                        std::find(o.units.begin(), o.units.end(), mid) == o.units.end())
                        o.units.push_back(mid);
            if (o.units.empty()) return R::InvalidUnit;
            const Vec3 tp = st.position(th);
            b_->drop_locked(o.units);  // OI follow-up t03
            if (o.units.empty()) return R::None;
            for (const objects::ObjectId id : o.units) b_->combat_->set_attack_goal(id, tc::kNoObject);  // refixf t06: kind 1
            b_->fighter_order(o.units, 0, tp);  // S7 t22: a position snapshot (kind 1)
            follow_drop(o.units);
            for (const objects::ObjectId mid : o.units) if (b_->abilities_) b_->abilities_->move_ordered(mid, b_->frame_);  // mcx4: Terminate_Effect_On_Move_Command (MCX4_T10_RE_ANSWERS §13)
            if (o.units.size() == 1) {
                b_->combat_->clear_order(o.units.front());
                return move_unit(o.units.front(), tp.x, tp.y) ? R::None : R::InvalidUnit;
            }
            const tactical::Aabb& gb = mode_->bounds();
            o.destination = {std::clamp(tp.x, gb.min.x, gb.max.x), std::clamp(tp.y, gb.min.y, gb.max.y)};
            b_->movement_->move(o, host_->random().synced(), b_->frame_);
            for (const objects::ObjectId id : o.units) b_->combat_->clear_order(id);
            return R::None;
        }
        case T::Attack: {
            const uint32_t tw = ev.u("target");
            const TacticalBattle::Unit* t = tw <= 0x7FFFFFFFu ? b_->unit_by_id(static_cast<objects::ObjectId>(tw)) : nullptr;
            // q6: a TEAM container is a valid target on its own (A1_READINGS §33.2.1 row 1, 0x5680b0); each attacker attacks
            // the container's live member nearest it, re-picked until the container is gone (TacticalBattle::TeamAttack).
            const TacticalBattle::Container* ct = t || tw > 0x7FFFFFFFu ? nullptr : b_->team_target(static_cast<objects::ObjectId>(tw));
            if (ct ? !hostile_owner(player, st.owner(ct->h)) : (!t || !t->alive || t->retreated || !hostile_owner(player, st.owner(t->h))))
                return R::InvalidTarget;
            int32_t hp = -1;
            if (const uint32_t ref2 = ev.u("ref2"); t && ref2 > 0)
                for (uint32_t k = 0; k < b_->combat_->hardpoint_count(t->id) && hp < 0; ++k)
                    if (static_cast<uint32_t>(b_->combat_->hardpoint_kind(t->id, k)) == ref2 - 1 && b_->combat_->hardpoint_targetable(t->id, k))
                        hp = static_cast<int32_t>(k);
            uint32_t n = 0;
            std::vector<objects::ObjectId> attackers;  // battle host: TEAM containers -> their members
            for (const uint32_t id : ev.ids("attackers"))
                for (const objects::ObjectId mid : team_units(player, id))
                    if (std::find(attackers.begin(), attackers.end(), mid) == attackers.end()) attackers.push_back(mid);
            follow_drop(attackers);  // a1h t02: a newer order (signal 0xb, A1_READINGS §33.3)
            std::vector<TacticalBattle::TeamPick> picks;
            // s78n t25.3 (MOVEMENT §4.3.2 item 2): on land the attackers of each resolved target (groups in order of first
            // appearance) form one movement order with the attack block (TacticalBattle::land_attack_order)
            std::vector<std::pair<objects::ObjectId, std::vector<objects::ObjectId>>> groups;
            for (const objects::ObjectId aid : attackers) {
                const TacticalBattle::Unit* u = b_->unit_by_id(aid);
                if (!u) continue;
                const TacticalBattle::Unit* tu = t;
                if (ct) {
                    const objects::ObjectId m = b_->team_pick(*mode_, *ct, st.position(u->h), b_->combat_->share_group(u->id), picks);
                    tu = m != objects::kNoObjectId ? b_->unit_by_id(m) : nullptr;
                    if (!tu) continue;
                }
                const Vec3 tp = st.position(tu->h);
                // cmb1 t02 (BKR1 Q2 [H]): on land Order_Attack(ordered 1) takes order_attack_issue's paths (a unit that is
                // not a team container / member / LUCKY_SHOT: weapon path, no unlocked ordered target; path B locks the
                // goal in weapon range). [port] a TEAM container target keeps the ordered slot (TeamAttack).
                if (!b_->space_ && !ct) (void)b_->combat_->order_attack_issue(u->id, tu->id, hp);
                else b_->combat_->order_attack(u->id, tu->id, hp);
                b_->combat_->set_attack_goal(u->id, tu->id, hp);  // refixf t06: Attack_Target block (kind 3, hard point, §2.8)
                if (ct && b_->combat_->has_unit(u->id)) b_->team_attack_set(u->id, ct->id, tu->id, true);
                if (!b_->movement_locked(u->id))  // S7 t22: an object block; a locked unit keeps its formation (as Move_To)
                    b_->fighter_order(std::span<const objects::ObjectId>(&u->id, 1), 0, tp,
                                      ct && b_->movement_->fighter_squad(ct->id) ? ct->id : tu->id);
                ++n;
                if (!u->movable) continue;
                if (!b_->space_) {  // s78n t20 stage A / t25.3 (MOVEMENT §4.3.2, FIGHTER_FLYING §4.11 A / E)
                    auto g = std::find_if(groups.begin(), groups.end(), [&](const auto& x) { return x.first == tu->id; });
                    if (g == groups.end()) g = groups.insert(groups.end(), {tu->id, {}});
                    g->second.push_back(u->id);
                    b_->attack_block_set(u->id, ct ? ct->id : tu->id, 0, tp, ct ? -1 : hp);  // cmb2 t04: the named hard point
                    continue;
                }
                const Vec3 up = st.position(u->h);
                const float dx = tp.x - up.x, dy = tp.y - up.y, d = dmath::sqrt(dx * dx + dy * dy);
                const float range = std::max(u->range, 100.0f);
                // s78f t10: as the human path (TacticalBattle::exec_attack, FIGHTER_FLYING §4.11 B 1 / B 3): a fighter's move
                // names the target as its object block; in range nobody moves and the formation is still replaced.
                // modc t06: with a TEAM container target `t` is null; the block is the human path's (the squadron
                // container itself, else the picked member).
                const objects::ObjectId block = ct && b_->movement_->fighter_squad(ct->id) ? ct->id : tu->id;
                const objects::ObjectId fobj = is_fighter(u->id) ? block : objects::kNoObjectId;
                if (d > 0.8f * range) {
                    const float k = (d - 0.7f * range) / d;
                    // s78n t20 fix (MOVEMENT 4.3.1 item 2): the approach stands for the attack's object-goal block -
                    // no destination check (as TacticalBattle::attack_approach)
                    move_unit(u->id, up.x + dx * k, up.y + dy * k, mv::kThreatOff, 0, fobj, true);
                } else if (fobj != objects::kNoObjectId && !b_->movement_locked(u->id)) {
                    mv::MoveOrder o;
                    o.units = {u->id};
                    const tactical::Aabb& gb = mode_->bounds();
                    o.destination = {std::clamp(tp.x, gb.min.x, gb.max.x), std::clamp(tp.y, gb.min.y, gb.max.y)};
                    o.goal_object = fobj;
                    b_->movement_->fighter_new_order(o, b_->frame_);
                }
            }
            for (const auto& [tid, us] : groups)
                (void)b_->land_attack_order(*mode_, us, tid, st.position(b_->unit_by_id(tid)->h), 0, false, host_->random().synced());
            return n ? R::None : R::InvalidUnit;
        }
        case T::PortExtension: {
            const uint32_t sub = ev.u("subtype");
            const auto b = ev.bytes_view("data");
            if (sub < lua::kLuaOrderSubtype || sub > lua::kLuaOrderSubtype + 0xFFu || b.size() < lua::kLuaOrderPayloadBytes) return R::Unhandled;
            const auto order = static_cast<lua::LuaOrder>(sub - lua::kLuaOrderSubtype);
            // battle host: a TEAM container's order goes to its live members (the container itself has no movement).
            // refixf t06: a task-force group order (tactical_ai.hpp HOST CONTRACT) carries its further units after the 24-byte
            // block, one u32 each: all of them form one order (MOVEMENT §6.3 [H]: one coordinator for the task force).
            std::vector<objects::ObjectId> ids = team_units(player, le32(b, 0));
            for (size_t off = lua::kLuaOrderPayloadBytes; off + 4 <= b.size(); off += 4)
                for (const objects::ObjectId mid : team_units(player, le32(b, off)))
                    if (std::find(ids.begin(), ids.end(), mid) == ids.end()) ids.push_back(mid);
            if (ids.empty()) return R::InvalidUnit;
            if (order == lua::LuaOrder::Stop) {
                follow_drop(ids);  // a1h t02
                for (const objects::ObjectId id : ids) {
                    const objects::ObjectId one[] = {id};
                    if (b_->unit_by_id(id)->movable) b_->movement_->stop(one);
                    b_->combat_->clear_order(id);
                    b_->combat_->set_attack_goal(id, tc::kNoObject);  // refixf t06
                }
                b_->fighter_order_clear(ids);  // S7 t22: Stop leaves the formation [M]
                return R::None;
            }
            if (order == lua::LuaOrder::HyperspaceAway) {
                // ref2 t06: Hyperspace_Away of a VM without the hyperspace hook (with it the binding calls
                // TacticalScriptHooks::hyperspace_out_link itself): the battle's departure for each unit, delay 0, keep =
                // the order's argument. Refused when no unit can jump (land, no locomotor, already leaving).
                bool any = false;
                for (const objects::ObjectId id : ids) any = b_->hyperspace_out(id, 0, le32(b, 20) != 0) || any;
                return any ? R::None : R::NotAllowed;
            }
            // ref2 t06 [port] documented refusal: the battle's hyperspace departures have no cancel (Cancel_Hyperspace's
            // locomotor / reinforcement side is not modelled, bind_objects.cpp m_cancel_hyperspace).
            if (order == lua::LuaOrder::CancelHyperspace) return R::NotAllowed;
            if (order != lua::LuaOrder::MoveTo && order != lua::LuaOrder::AttackMove && order != lua::LuaOrder::Guard) return R::Unhandled;
            mv::MoveOrder group;
            for (const objects::ObjectId id : ids)
                if (b_->unit_by_id(id)->movable) group.units.push_back(id);
            if (group.units.empty()) return R::InvalidUnit;
            follow_drop(ids);  // a1h t02: the collector raises signal 0xb on each unit it takes (A1_READINGS §33.3)
            const TacticalBattle::Unit* u = b_->unit_by_id(group.units.front());
            float x = std::bit_cast<float>(le32(b, 8)), y = std::bit_cast<float>(le32(b, 12));
            const uint32_t tw = le32(b, 4);
            bool point_goal = false;  // S7 t29 rework: a land Attack_Move on a non-hostile object goes to its position
            bool nonhostile_block = false;  // bkt1 t05: a unit Attack_Move on a non-hostile object (land attack block, no lock)
            if (tw != cmd::kNoObject) {
                const objects::ObjectHandle th = tw <= 0x7FFFFFFFu ? st.find(static_cast<objects::ObjectId>(tw)) : objects::ObjectHandle{};
                if (!st.alive(th)) {
                    ++stale_;
                    return R::None;  // COMMANDS.md sec. 6.2: a missing / dying target is a silent no-op (stale script reference)
                }
                const Vec3 tp = st.position(th);
                x = tp.x;
                y = tp.y;
                const TacticalBattle::Unit* tu = b_->unit_by_id(static_cast<objects::ObjectId>(tw));
                const bool attackable = tu && tu->alive && !tu->retreated && b_->combat_->has_unit(tu->id);
                if (order == lua::LuaOrder::AttackMove && attackable && !hostile_owner(player, st.owner(tu->h))) {
                    // S7 t29 rework (TACTICAL_COMBAT §2.10 "Task-force Attack_Move on a non-hostile object"): not refused.
                    // Land (0x6e9440): the goal is the object's position, an attack-move to that point; space (0x6e4fc0): the
                    // object stays the goal. No Order_Attack, no formation attack target (path B needs relation 1): the units
                    // go there, auto-engage on the way and never attack the object itself.
                    for (const objects::ObjectId id : group.units) b_->combat_->set_attack_goal(id, tc::kNoObject);
                    // bkt1 t05 (b0075, BKT1_ATTACK_RE §3 [H]): an object's own Attack_Move (arg 0; Move_Internal 0x57da90) is
                    // the land attack move on the object itself - kind 3 with flag 0x01, the exact goal, slots, staging and
                    // the fire trim as for an enemy, no lock (the attack block below, kAttackBlockNoLock)
                    point_goal = !b_->space_ && (le32(b, 20) & at::kTfAttackMoveIssueAttack) != 0;
                    // (not on the caller itself or its container: BKT1_ATTACK_RE §3.1, those stay a move to the point). cmb3 t01 (b)
                    // (b0169): the caller is the order's object (payload word 0); a squadmate of the caller, or a member of a
                    // calling squad container, is neither, so it gets the block. tu is a battle unit, never a container, so
                    // the caller's container (+0x2b0) cannot be it.
                    // A task force's further units (after byte 24) are not callers: BKT1 §3.1 is the object's own Move_Internal.
                    // (review) A squadmate or own-member target is in group.units: the block's item-6 stop (0x40edd0, every
                    // member can fire from where it stands) ends it at once, so the formation does not chase its own member.
                    const bool self = tu->id == static_cast<objects::ObjectId>(le32(b, 0));
                    nonhostile_block = !b_->space_ && !point_goal && !self;
                    if (self) point_goal = !b_->space_;
                } else if (order == lua::LuaOrder::AttackMove && attackable) {
                    // refixf t06 (TACTICAL_COMBAT §2.8): an Attack_Move on an object. A: a task-force order (arg bit, header)
                    // issues Order_Attack (ordered 1) to every unit now; a refusing unit is dropped from the move, none left
                    // -> the order fails. B: the object is each moving unit's formation attack goal (the combat system locks
                    // it in weapon range). This replaces the a1f t07 neutral-target stand-in (§2.8: an enemy Is_Neutral
                    // player's object qualifies for B). An object's own Lua Attack_Move (arg 0) gets B only.
                    if (le32(b, 20) & at::kTfAttackMoveIssueAttack) {
                        std::vector<objects::ObjectId> kept;
                        for (const objects::ObjectId id : group.units)
                            if (!b_->combat_->has_unit(id) || b_->combat_->order_attack_issue(id, tu->id, -1)) kept.push_back(id);
                            else facts_.order_refused.push_back(static_cast<int32_t>(id));  // q6 t19: not moved, not pending
                        for (const objects::ObjectId id : ids)  // members that do not move still get the order
                            if (std::find(group.units.begin(), group.units.end(), id) == group.units.end() && b_->combat_->has_unit(id))
                                b_->combat_->order_attack_issue(id, tu->id, -1);
                        if (kept.empty()) return R::InvalidTarget;
                        group.units = std::move(kept);
                        u = b_->unit_by_id(group.units.front());
                    }
                    for (const objects::ObjectId id : group.units) b_->combat_->set_attack_goal(id, tu->id, -1);
                    // Staging (0x40e260 [M], §2.8): land formations (type 1) only - a single space Attack_Move is not staged.
                    // s78n t25.3: on land it is TacticalBattle::attack_stage_goal, inside the attack order below.
                } else if (order == lua::LuaOrder::AttackMove) {
                    for (const objects::ObjectId id : group.units) b_->combat_->set_attack_goal(id, tc::kNoObject);
                    // cmb2 t06 (b0075, BKT1_ATTACK_RE §3.1 / §3.5 [H]): an object's own Attack_Move (arg 0) on an object that is
                    // not a combat unit (a story marker) is the land attack move too - kind 3 with flag 0x01 on the object, no
                    // lock (TARGETING by geometry: TacticalBattle::attack_can_hit_at); not on the caller or its container
                    // (review: not a TEAM container - its members are the combat units - and not a retreated unit)
                    const auto obj = static_cast<objects::ObjectId>(tw);
                    // cmb3 t01 (b) review: the same caller-only rule as the combat-unit branch (payload word 0 or its container)
                    const auto caller = static_cast<objects::ObjectId>(le32(b, 0));
                    const bool self = obj == caller || b_->attack_mover(caller) == obj;
                    const bool combat_object = (tu && (tu->retreated || !tu->alive || b_->combat_->has_unit(tu->id))) ||
                                               b_->team_target(obj) != nullptr || b_->movement_->is_land_team_container(obj);
                    nonhostile_block = !b_->space_ && !self && !combat_object && (le32(b, 20) & at::kTfAttackMoveIssueAttack) == 0;
                }
            }
            if (order != lua::LuaOrder::AttackMove || tw == cmd::kNoObject)  // refixf t06: position / guard / move blocks
                for (const objects::ObjectId id : group.units) b_->combat_->set_attack_goal(id, tc::kNoObject);
            if (!std::isfinite(x) || !std::isfinite(y)) return R::InvalidPosition;
            // S7 t22: the squadrons' formation - block flags 0x01 attack-move / 0x02 guard; an Attack_Move / Guard of an
            // object keeps it as the slot goal (Move_To(object) is a position snapshot, kind 1).
            const uint8_t fflags = order == lua::LuaOrder::AttackMove ? mv::kMoveFlagAttackMove
                                   : order == lua::LuaOrder::Guard    ? mv::kMoveFlagGuardMove
                                                                      : uint8_t{0};
            const objects::ObjectId fobj = tw != cmd::kNoObject && order != lua::LuaOrder::MoveTo && !point_goal
                                               ? static_cast<objects::ObjectId>(tw)
                                               : objects::kNoObjectId;
            const uint32_t targ = le32(b, 20);  // REMAINING_GAPS §4.2: Move_To threat threshold (float bits, 0 = off)
            const float tt = order == lua::LuaOrder::MoveTo && targ != 0u ? std::bit_cast<float>(targ) : mv::kThreatOff;
            // s78n t20 fix (MOVEMENT 4.3.1 item 2; LUA_API "Move_To / Attack_Move / Guard_Target"): Attack_Move and
            // Guard_Target on an object build a kind-3 block, which never takes the position destination check;
            // Move_To(object) is a kind-1 position snapshot and takes it (the same split as `fobj` above)
            const bool object_block = fobj != objects::kNoObjectId;
            // s78n t25.4: a land Guard_Target on an object is a guard block of the movement (land_attack_order, guard)
            const bool land_guard = !b_->space_ && order == lua::LuaOrder::Guard && object_block;
            // s78n t20 stage A (FIGHTER_FLYING §4.11 A / E, MOVEMENT §4.3.2 item 6): a land Attack_Move on an attackable
            // object (bkt1 t05: a non-hostile one too, no lock) is an attack block of each mover (TacticalBattle::AttackBlock, whose 30-frame checks re-path it: the
            // object follow below leaves it out), and a unit that can already hit it from where it stands takes no path.
            const TacticalBattle::Unit* atu = tw != cmd::kNoObject && tw <= 0x7FFFFFFFu ? b_->unit_by_id(static_cast<objects::ObjectId>(tw)) : nullptr;
            const bool combat_target = atu && atu->alive && !atu->retreated && b_->combat_->has_unit(atu->id);
            // cmb2 t06: a non-combat object (nonhostile_block set above) is a no-lock attack block as well
            const bool attack_block = !b_->space_ && order == lua::LuaOrder::AttackMove && !point_goal && tw != cmd::kNoObject &&
                                      (combat_target ? (nonhostile_block || hostile_owner(player, st.owner(atu->h))) : nonhostile_block);
            if (attack_block) {
                // s78n t25.3 (MOVEMENT §4.3.2 items 2 / 6 / 7): one movement order with the attack block (flags 0x01) for
                // the movers; the staging 0x40e260 is the builder's (attack_stage_goal); a formation whose members can all
                // fire already is stopped by the plan (item 6), the others take the slot and the fire trim
                const auto aid = static_cast<objects::ObjectId>(tw);
                const Vec3 tp = st.position(st.find(aid));
                const uint8_t bf = static_cast<uint8_t>(mv::kMoveFlagAttackMove | (nonhostile_block ? TacticalBattle::kAttackBlockNoLock : 0));
                for (const objects::ObjectId id : group.units) b_->attack_block_set(id, aid, bf, tp);
                b_->land_order(group.units, mv::kMoveFlagAttackMove, tp, aid);  // cmb1 t01: the formation's block
                (void)b_->land_attack_order(*mode_, group.units, aid, tp, mv::kMoveFlagAttackMove, false, host_->random().synced());
                return R::None;
            }
            // a1h t02: an object goal (kind 3) is followed by the movement layer (§33.4). s78n t26 review round 2: Move_To on an
            // object is a kind-1 position snapshot (LUA_API Move_Internal flags 0 [H]): no follow
            if (tw != cmd::kNoObject && !point_goal && !attack_block && order != lua::LuaOrder::MoveTo) {
                const Vec3 tp = st.position(st.find(static_cast<objects::ObjectId>(tw)));
                // s78f t10: fighters given an object block follow it in their own formation (0x4192b0 steps 3 / 4,
                // FIGHTER_FLYING §4.11); the follow's re-issued plain move would replace that block with a flags-0 position
                // block (and drop its pushed auto blocks), so they stay out of the follow.
                std::vector<objects::ObjectId> fu;
                for (const objects::ObjectId id : group.units)
                    if (fobj == objects::kNoObjectId || !is_fighter(id)) fu.push_back(id);
                follow_add(fu, static_cast<objects::ObjectId>(tw), tp.x, tp.y, tt, land_guard);
            }
            if (land_guard) {
                // s78n t25.4 (MOVEMENT §4.3.2 items 1 / 2 / 6 / 7): a land Guard_Target on an object is one movement order
                // with the guard block (flags 0x02): the guard L rule gives the formations farther than min(range, 150)
                // a slot, the guard trim stops them within Land_Guard_Range of the object with a clear line of fire.
                // The follow record above re-plans it (follow_service). Locked units are dropped by the builder.
                for (const objects::ObjectId id : group.units) b_->combat_->clear_order(id);
                const Vec3 gp = st.position(st.find(fobj));
                b_->land_order(group.units, fflags, gp, fobj);  // cmb1 t01: the formation's guard block
                // cmb2 t03 (review): a failed first plan waits 2 x FPS before the follow tries again
                if (b_->land_attack_order(*mode_, group.units, fobj, gp, fflags, false, host_->random().synced(), true) ==
                        TacticalBattle::AttackPlan::Failed &&
                    !follows_.empty() && follows_.back().target == fobj)
                    follows_.back().next = b_->frame_ + 61;
                return R::None;
            }
            // refixf t02 / t06 (MOVEMENT §6.3 [H]): Lua Attack_Move / Move_To on a task force build one parameter block and ONE
            // coordinator for all its units, so the formation slots and the destination reservations (§2.4 step 4) give each
            // unit its own spot. The tactical AI now sends that as one group order (the ids above); the t02 stand-in that
            // merged one frame's per-unit orders here is gone. An object's own Lua order stays a one-unit move.
            // mcx4 (MCX4_T10_RE_ANSWERS §13): a Lua Move_To / Attack_Move is a move order (0x4f6b60): Terminate_Effect_On_Move_Command
            for (const objects::ObjectId mid : group.units)
                if (b_->abilities_) b_->abilities_->move_ordered(mid, b_->frame_);
            if (group.units.size() == 1) {  // a single unit: unchanged path
                if (order != lua::LuaOrder::AttackMove) b_->combat_->clear_order(u->id);
                if (!b_->movement_locked(u->id)) b_->fighter_order(group.units, fflags, Vec3{x, y, 0.0f}, fobj);
                return move_unit(u->id, x, y, tt, fflags, fobj, object_block) ? R::None : R::InvalidUnit;  // s78f t10: the same block
            }
            group.threat_threshold = tt;
            group.flags = fflags;  // s78f t10: the movement block = the host record's (flags, object)
            group.goal_object = fobj == objects::kNoObjectId ? -1 : fobj;
            group.skip_destination_check = object_block;  // s78n t20 fix (MOVEMENT 4.3.1 item 2)
            const tactical::Aabb& gb = mode_->bounds();
            group.destination = {std::clamp(x, gb.min.x, gb.max.x), std::clamp(y, gb.min.y, gb.max.y)};
            b_->drop_locked(group.units);  // OI follow-up t03: locked members keep their order [M: group-move reading]
            if (group.units.empty()) return R::None;
            b_->movement_->move(group, host_->random().synced(), b_->frame_);  // one formation move of the team
            b_->fighter_order(group.units, fflags, Vec3{group.destination.x, group.destination.y, 0.0f}, fobj);  // S7 t22
            if (order != lua::LuaOrder::AttackMove)
                for (const objects::ObjectId id : group.units) b_->combat_->clear_order(id);
            return R::None;
        }
        case T::Reinforce: {
            // --- A2 tactical: the human reinforcement path (TacticalBattle::order_reinforce) + the tactical
            // population cap (population_cap: the cap the battle itself does not test on land). ---
            const auto key = static_cast<objects::ObjectId>(ev.u("unit"));
            if (!b_->space_) {
                const float cap = population_cap(player);
                for (const TacticalBattle::Participant& p : b_->parts_)
                    if (p.galactic_id == key && p.owner == static_cast<int32_t>(player) && p.reserve && !p.deployed && cap >= 0.0f &&
                        population_used(player) + p.pop > cap)
                        return R::QueueFull;
            }
            return b_->order_reinforce(*mode_, player, key, ev.f("x"), ev.f("y"), host_->random().synced());
            // --- end A2 tactical ---
        }
        case T::Retreat: return b_->request_retreat(player) ? R::None : R::NotAllowed;
        case T::Production:  // skirmish station production (Produce_Force of non-UC types): the battle's Production handler
            return b_->hg_production_command(*mode_, player, ev);
        case T::TacticalBuild: {
            // --- A2 tactical: TacticalBuildEvent 0x42edf0 for an AI player (L1_S4_REMAINING §3.6): refusals in order,
            // then the build starts at once (pad_build, check_rules false). ---
            // battle host: the shared TacticalBuild hook (TacticalBattle::tactical_build, ai = true: same refusal order; no
            // charge in a campaign battle, the full Tactical_Build_Cost_Multiplayer in land skirmish, skai t09; the UC /
            // constructed objects become battle combat units).
            const uint32_t pw = ev.u("pad");
            if (pw == commands::kNoObject || pw > 0x7FFFFFFFu) return R::InvalidTarget;
            return b_->tactical_build(*mode_, player, static_cast<objects::ObjectId>(pw), static_cast<objects::TypeIndex>(ev.u("type_index")),
                                      host_->random().synced(), true);
            // --- end A2 tactical ---
        }
        // ref2 t06: every other event goes through the battle's validate-and-apply entry, as a human's would (Turn_To_Face
        // -> order_facing, the Garrison family -> s4_command, Sell -> tactical_sell, the ability events -> handle_command);
        // no AI order is dropped as Unhandled for want of a branch here. The AI issues no selection-based event.
        default: return b_->apply_order(*mode_, player, ev, {}, host_->random().synced());
    }
}

std::vector<at::TacticalReinforcement> BattleAi::reinforcement_pool(int32_t player) {
    lane_live_read("reinforcement_pool");
    std::vector<at::TacticalReinforcement> out;
    for (const TacticalBattle::Participant& p : b_->parts_) {
        if (p.owner != player || !p.reserve || p.deployed) continue;
        at::TacticalReinforcement r;
        r.key = p.galactic_id;
        r.type = static_cast<int32_t>(p.type);
        r.population = p.pop;
        r.units = static_cast<uint32_t>(b_->spawn_types(p).size());
        // --- A2 tactical: count(E) and the member match facts (AI_GALACTIC_3 §1.3) ---
        const objects::GameObjectTypes& types = mode_->types();
        const auto ti = static_cast<objects::TypeIndex>(p.type);
        if (ti < types.size()) {
            const auto members = types.get(ti, objects::tags::Company_Units);
            const auto n = static_cast<int32_t>(members.size());
            if (n > 0) {
                const objects::TypeIndex m = types.resolve_name(members[0]);
                if (m != objects::kNone && m < types.size()) {
                    r.member_type = static_cast<int32_t>(m);
                    r.member_create_team = types.get(m, objects::tags::Create_Team);
                }
                const int32_t squad = std::max(1, types.get(ti, objects::tags::Max_Squad_Size));
                r.count = r.member_create_team ? (n + squad - 1) / squad : n;
            }
        }
        // --- end A2 tactical ---
        out.push_back(r);
    }
    return out;
}

bool BattleAi::can_reinforce(int32_t player) {
    lane_live_read("can_reinforce");
    if (b_->finished_) return false;
    const int32_t side = player == b_->req_.info.defender ? 0 : player == b_->req_.info.attacker ? 1 : -1;
    return side >= 0 && b_->retreat_side_ != side;
}

// --- A2 tactical: tactical population caps (AI_GALACTIC_3 §3.3) [H]: space = the faction's Space_Tactical_Unit_Cap;
// land campaign battle = Max_Ground_Forces_On_Planet for the defender, min(that, capacity) for any other player, where
// capacity = Base_Population_Capacity + Σ Additional_Population_Capacity of the player's live objects in the battle
// (the reinforcement points it holds). t50: a land skirmish is min(capacity, Land_Skirmish_Unit_Cap_By_Player_Count at the player
// count), the capacity counting the player's and its allies' objects (§3.3 / §3.3 "plus, in skirmish, those of allied players"). ---
float BattleAi::population_cap(int32_t player) {
    lane_live_read("population_cap");
    ensure_type_facts();
    const auto& ps = b_->galaxy_->players();
    const uint32_t f = player >= 0 && static_cast<size_t>(player) < ps.size() ? ps[static_cast<size_t>(player)].faction : galactic::kNoIndex;
    if (b_->space_) return b_->rules_->faction(f).space_unit_cap;
    if (b_->skirmish_land()) {
        const float curve = b_->skirmish_land_unit_cap(player);
        const objects::ObjectStore& st = mode_->primary();
        const objects::GameObjectTypes& types = mode_->types();
        int32_t capacity = f < b_->galaxy_->rules().factions.size() ? b_->galaxy_->rules().factions[f].base_population_capacity : 0;
        for (int32_t q = 0; q < static_cast<int32_t>(ps.size()); ++q) {
            if (q != player && b_->combat_->is_enemy(player, q)) continue;
            for (const objects::ObjectHandle h : st.owned_by(q))
                if (st.active(h) && st.type(h) < types.size()) capacity += type_facts(st.type(h)).additional_population_capacity;
        }
        const float cap = static_cast<float>(capacity);
        return curve < 0.0f ? cap : std::min(cap, curve);
    }
    const float planet_cap = static_cast<float>(b_->galaxy_->rules().constants.max_ground_forces_on_planet);
    if (player == b_->req_.info.defender) return planet_cap;
    const objects::ObjectStore& st = mode_->primary();
    const objects::GameObjectTypes& types = mode_->types();
    int32_t capacity = f < b_->galaxy_->rules().factions.size() ? b_->galaxy_->rules().factions[f].base_population_capacity : 0;
    for (const objects::ObjectHandle h : st.owned_by(player))
        if (st.active(h) && st.type(h) < types.size()) capacity += type_facts(st.type(h)).additional_population_capacity;
    return std::min(planet_cap, static_cast<float>(capacity));
}

// §3.2: Σ population shares of the player's live battle objects [port, space: per galactic participant, charged once], then
// (int)sum + 1 when the fraction exceeds Allow_Reinforcement_Percentage_Normalized (shipped 0.0: any fraction rounds up).
float BattleAi::population_used(int32_t player) {
    lane_live_read("population_used");
    if (!b_->space_) return b_->population_used(player);  // trl2 t02: the land list sum (BKG1_RE_ANSWERS §7.1), rounded
    float pop = 0.0f;
    std::vector<uint8_t> counted(b_->parts_.size(), 0);
    for (const TacticalBattle::Unit& u : b_->units_)
        if (u.alive && !u.retreated && b_->parts_[u.participant].owner == player && !counted[u.participant]) {
            counted[u.participant] = 1;
            pop += b_->parts_[u.participant].pop;
        }
    const float whole = std::floor(pop);
    return pop - whole > allow_reinforcement_fraction_ ? whole + 1.0f : whole;
}

float BattleAi::type_population(int32_t type) const {  // pop(T, mode) 0x373500
    const objects::GameObjectTypes& types = mode_->types();
    if (type < 0 || static_cast<size_t>(type) >= types.size()) return 0.0f;
    const auto ti = static_cast<objects::TypeIndex>(type);
    const objects::IntTag& tag = b_->space_ ? objects::tags::Space_Override_Population_Value : objects::tags::Land_Override_Population_Value;
    if (!types.get_string(ti, tag).empty() && types.get(ti, tag) >= 0) return static_cast<float>(types.get(ti, tag));
    return static_cast<float>(types.get(ti, objects::tags::Population_Value));
}

// Skirmish station production: the player's live objects whose builder list (their faction row of
// Tactical_Buildable_Objects_Multiplayer) holds `type`, ascending id; {} in campaign / land battles (§1.11).
// skai t17 (STATION_HANGARS_SOUND §1.2 steps 7-8): the build queue of an upgraded station goes on at its successor; follow
// the battle's upgrade records (battle order) from `id` to the last station, -1 when `id` was never upgraded or the last
// one is gone.
int32_t BattleAi::replaced_builder_in(const FactEnv& env, int32_t id) const {
    int32_t cur = id;
    bool moved = false;
    env.each_upgrade([&](objects::ObjectId from, objects::ObjectId to) {
        if (static_cast<int32_t>(from) == cur) {
            cur = static_cast<int32_t>(to);
            moved = true;
        }
    });
    return moved && env.st.alive(env.st.find(static_cast<objects::ObjectId>(cur))) ? cur : -1;
}

std::vector<int32_t> BattleAi::tactical_builders(int32_t player, int32_t type) {
    std::vector<int32_t> out;
    lane_live_read("tactical_builders");
    if (!b_->production_enabled() || type < 0) return out;
    const objects::ObjectStore& st = mode_->primary();
    for (const objects::ObjectHandle h : st.owned_by(player)) {
        if (!st.active(h)) continue;
        const objects::ObjectId id = st.id(h);
        if (b_->hg_build_check(st, player, id, static_cast<objects::TypeIndex>(type)) != 1) out.push_back(static_cast<int32_t>(id));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// True only in skirmish space battles with production: not Cinematic_Object_Only, a price, and some builder passes the
// Production checks now (list / faction row, prerequisites, Build_Limit_Current_For_All_Allies). Credits are the
// PotentialPlan's own check.
bool BattleAi::can_build_now(int32_t player, int32_t type) {
    lane_live_read("can_build_now");
    if (!b_->production_enabled() || type < 0) return false;
    const objects::GameObjectTypes& types = b_->catalog_->types();
    const auto t = static_cast<objects::TypeIndex>(type);
    if (t >= types.size() || types.get(t, objects::tags::Cinematic_Object_Only)) return false;
    if (types.get(t, objects::tags::Tactical_Build_Cost_Multiplayer) <= 0) return false;
    const objects::ObjectStore& st = mode_->primary();
    for (const int32_t id : tactical_builders(player, type))
        if (b_->hg_build_check(st, player, static_cast<objects::ObjectId>(id), t) == 0) return true;
    return false;
}
// --- end A2 tactical ---


// --- A2 tactical: build pads (L1_S4_REMAINING §3.6; TacticalBattle pads, tactical_battle_pads.cpp) ---------------

namespace {
bool pad_name_equal(std::string_view a, std::string_view b) {
    while (!a.empty() && (a.front() == ' ' || a.front() == '\t')) a.remove_prefix(1);
    while (!a.empty() && (a.back() == ' ' || a.back() == '\t')) a.remove_suffix(1);
    return ai::ai_iequal(a, b);
}
}  // namespace


// The pads `player` can build on: live TACTICAL_BUILD_OBJECTS objects owned by the player or an ally (0x39a2c0, relation
// 0: never a Neutral-owned pad, q6 t19), empty, unblocked, cooldown over, ascending id [M: the free store's pad table +0xE8
// order, a hash map in the original - AI_TACTICAL §7.5.1 asks a defined order]; accepts = the UC types of the pad type's Tactical_Buildable_Objects_Campaign
// row for the player's faction (S10 battles are campaign battles, as pad_build).
std::vector<at::TacticalPad> BattleAi::build_pads_in(const FactEnv& env, int32_t player) const {
    std::vector<at::TacticalPad> out;
    const objects::ObjectStore& st = env.st;
    const std::vector<CaptureInfluencer> influencers = capture_influencers(st);  // OPT2 t10: once for every pad
    const objects::GameObjectTypes& types = mode_->types();
    const std::string faction = env.faction(player);
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.alive(h) || !st.has_behavior(h, kBuildObjects)) continue;
        const objects::ObjectId id = st.id(h);
        // TacticalBuild Setup A (AI_GALACTIC_3 §1.7): friendly / visible, empty and unblocked (0x42e400), cooldown over.
        if (pad_check(env, player, static_cast<int32_t>(id), influencers) != at::PadCheck::Ok) continue;
        at::TacticalPad p;
        p.id = id;
        const Vec3 q = st.position(h);
        p.position = {q.x, q.y, q.z};
        bool in_row = false;
        for (const std::string_view tok : types.get_list(st.type(h), b_->pad_list_tag())) {
            const objects::TypeIndex t = types.resolve_name(tok);
            if (t == objects::kNone || t >= types.size()) {
                in_row = pad_name_equal(tok, faction);
                continue;
            }
            if (in_row && std::find(p.accepts.begin(), p.accepts.end(), static_cast<int32_t>(t)) == p.accepts.end())
                p.accepts.push_back(static_cast<int32_t>(t));
        }
        if (!p.accepts.empty()) out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const at::TacticalPad& a, const at::TacticalPad& b) { return a.id < b.id; });
    return out;
}

// Build / Build_All's three refusals (AI_TACTICAL §7.5): not friendly (0x39a2c0: relation 0 to the pad owner; no
// visibility test, corrected q6 t19), game rules (0x42e400: empty and no live non-allied Influences_Capture_Point object
// without BASE_SHIELD / DUMMY_GROUND_STRUCTURE within the pad type's Capture_Point_Radius, 3-D), rebuild cooldown (0x42daf0).
at::PadCheck BattleAi::build_pad_check_in(const FactEnv& env, int32_t player, int32_t pad) const {
    return pad_check(env, player, pad, capture_influencers(env.st));
}

// sp3 t02: TypeFacts (built before any pad query) instead of the lazily filled memo, so the AI lane can read it.
bool BattleAi::influences_capture(objects::TypeIndex t) const { return t < mode_->types().size() && type_facts(t).influences_capture; }


std::vector<BattleAi::CaptureInfluencer> BattleAi::capture_influencers(const objects::ObjectStore& st) const {
    std::vector<CaptureInfluencer> out;
    for (const objects::ObjectHandle h : st.all_objects())
        if (st.alive(h) && influences_capture(st.type(h))) out.push_back({h, st.owner(h), st.position(h)});
    return out;
}

at::PadCheck BattleAi::pad_check(const FactEnv& env, int32_t player, int32_t pad, const std::vector<CaptureInfluencer>& influencers) const {
    const objects::ObjectStore& st = env.st;
    const objects::GameObjectTypes& types = mode_->types();
    const objects::ObjectHandle ph = pad >= 0 ? st.find(static_cast<objects::ObjectId>(pad)) : objects::ObjectHandle{};
    if (!st.alive(ph) || !st.has_behavior(ph, kBuildObjects)) return at::PadCheck::NotFriendlyOrVisible;
    const int32_t owner = st.owner(ph);
    // q6 t19 (AI_TACTICAL §7.5 / §7.5.1): 0x39a2c0 = relation(player -> pad owner) == 0 ("location is no longer friendly");
    // a Neutral-owned pad (relation 2) is never reserved or built. There is no visibility test here (the player build path
    // 0x42edf0, TacticalBattle::tactical_build, still refuses a pad under the player's fog).
    if (owner < 0 || !env.allied(player, owner)) return at::PadCheck::NotFriendlyOrVisible;
    if (env.pad_contents(static_cast<objects::ObjectId>(pad)) != objects::kNoObjectId) return at::PadCheck::Blocked;
    const objects::TypeIndex pt = st.type(ph);
    const double r = pt < types.size() ? static_cast<double>(types.get(pt, objects::tags::Capture_Point_Radius)) : 0.0;
    const Vec3 pp = st.position(ph);
    for (const CaptureInfluencer& x : influencers) {
        if (x.h == ph) continue;
        // q6 t19 rule 0x42e400 (AI_TACTICAL §7.5.1): every live Influences_Capture_Point object not allied with the pad owner
        // blocks, Neutral-owned ones included, unless it has BASE_SHIELD or DUMMY_GROUND_STRUCTURE [port: battle units only,
        // see pad_rule_blocker].
        if (!env.rule_blocker(x.h, owner)) continue;
        const Vec3 q = x.position;
        const double dx = static_cast<double>(q.x) - pp.x, dy = static_cast<double>(q.y) - pp.y, dz = static_cast<double>(q.z) - pp.z;
        if (dx * dx + dy * dy + dz * dz <= r * r) return at::PadCheck::Blocked;
    }
    if (env.in_cooldown(static_cast<objects::ObjectId>(pad)))
        return at::PadCheck::RebuildCooldown;
    return at::PadCheck::Ok;
}
// --- end A2 tactical ---

// --- battle host follow-up: TEAM order expansion, opportunity targets, HIJACK -----------------------------------------

// ---- refixf t06: space TF.Reinforce ring search (header) ----
bool BattleAi::space_drop_frame(int32_t player, at::SpaceDropFrame& out) {
    lane_live_read("space_drop_frame");
    uint8_t side = 0;
    for (const TacticalBattle::Participant& p : b_->parts_)
        if (p.owner == player) {
            side = p.side;
            break;
        }
    const Vec3 f = b_->facing_[side];
    out.entry_facing_deg = static_cast<float>(dmath::atan2(static_cast<double>(f.y), static_cast<double>(f.x)) * (180.0 / dmath::kPi));
    const tactical::Aabb& bb = mode_->bounds();
    out.has_box = true;
    out.min_x = bb.min.x;
    out.min_y = bb.min.y;
    out.max_x = bb.max.x;
    out.max_y = bb.max.y;
    return true;
}

bool BattleAi::prevention_hit_in(const FactEnv& env, int32_t player, const at::Vec3f& p) const {
    // sp3 t02: with the lane view, the snapshot store and the captured relations (think() never reads the battle)
    const objects::ObjectStore& st = env.st;
    const objects::GameObjectTypes& types = mode_->types();
    for (const objects::ObjectHandle h : st.all_objects()) {  // GOM list +0x2f0 stand-in: GOM order
        if (!st.active(h)) continue;
        const objects::TypeIndex t = st.type(h);
        if (t >= types.size()) continue;
        const float r = type_facts(t).reinforcement_prevention_radius;
        if (!(r > 0.0f)) continue;
        const int32_t o = st.owner(h);
        if (o >= 0 && (o == player || !env.enemy(player, o))) continue;  // allied (0x39a230)
        const Vec3 q = st.position(h);
        const float dx = p.x - q.x, dy = p.y - q.y;
        if (dx * dx + dy * dy < r * r) return true;
    }
    return false;
}

bool BattleAi::base_alive_in(const FactEnv& env, int32_t player) const {
    if (!b_->cfg_.skirmish) return true;  // 0x28afb0: the rule is the skirmish's
    // sp3 t02: with the lane view, the snapshot store and the captured alliance table (think() never reads the battle)
    const objects::ObjectStore& st = env.st;
    const auto allied = [&](int32_t o) { return o == player || env.allied(player, o); };
    static const objects::BehaviorTypeId kStarBase = objects::behavior_type_id("DUMMY_STAR_BASE");
    if (kStarBase == objects::kNoBehavior) return true;
    for (const objects::ObjectHandle h : st.all_objects()) {
        if (!st.active(h) || !st.has_behavior(h, kStarBase)) continue;
        const int32_t o = st.owner(h);
        if (o >= 0 && allied(o)) return true;
    }
    return false;
}

bool BattleAi::space_drop_ok(int32_t player, int32_t key, const at::Vec3f& p, float facing_deg, float sweep) {
    lane_live_read("space_drop_ok");
    ensure_type_facts();  // the prevention test reads the type facts (a lane apply may come before any fill after a load)
    const tactical::Aabb& bb = mode_->bounds();
    if (!(p.x >= bb.min.x && p.x <= bb.max.x && p.y >= bb.min.y && p.y <= bb.max.y)) return false;
    if (prevention_hit_in(live_env(), player, p)) return false;
    if (!b_->fog_reinforce_point_ok(player, p.x, p.y)) return false;  // fogged for the player (force 1)
    const TacticalBattle::Participant* part = nullptr;
    for (const TacticalBattle::Participant& q : b_->parts_)
        if (q.galactic_id == static_cast<objects::ObjectId>(key) && q.owner == player && q.reserve && !q.deployed) {
            part = &q;
            break;
        }
    if (!part || !b_->movement_) return false;
    const objects::GameObjectTypes& types = mode_->types();
    const auto ti = static_cast<objects::TypeIndex>(part->type);
    if (ti >= types.size()) return false;
    const mv::UnitDesc ud = mv::unit_desc_from_type(types, ti);
    auto layer = static_cast<uint32_t>(types.get(ti, objects::tags::Space_Layer));
    if (layer == 0) layer = 4u;
    double sn = 0.0, cs = 1.0;
    dmath::sincos(static_cast<double>(facing_deg) * (dmath::kPi / 180.0), sn, cs);
    const mv::V2 to{p.x, p.y};
    const mv::V2 from{p.x - sweep * static_cast<float>(cs), p.y - sweep * static_cast<float>(sn)};
    const float now = static_cast<float>(b_->frame_);
    return b_->movement_->space().query_segment(from, to, now, now + 115.0f, ud.radius, layer | 8u, tc::kNoObject, true, b_->frame_) == 0;
}

// ---- a1g t04 follow-up: land TF.Reinforce drop validity (header; TACTICAL_GAPS §4.2.1) ----
bool BattleAi::land_drop_valid(int32_t player, int32_t key, const at::Vec3f& p, bool human) {
    return b_->land_drop_valid(*mode_, player, static_cast<objects::ObjectId>(key), p.x, p.y, human, use_reinforcement_points_);  // a1h t06
}

std::vector<objects::ObjectId> BattleAi::team_units(uint8_t player, uint32_t wire) const {
    std::vector<objects::ObjectId> out;
    if (wire == cmd::kNoObject || wire > 0x7FFFFFFFu) return out;
    const auto id = static_cast<objects::ObjectId>(wire);
    const objects::ObjectStore& st = mode_->primary();
    if (const TacticalBattle::Unit* u = b_->unit_by_id(id)) {
        if (u->alive && !u->retreated && st.owner(u->h) == static_cast<int32_t>(player)) out.push_back(u->id);
        return out;
    }
    for (size_t c = 0; c < b_->containers_.size(); ++c) {  // a team container: its live members, ascending id
        if (b_->containers_[c].id != id || b_->containers_[c].removed) continue;
        for (const TacticalBattle::Unit& m : b_->units_)
            if (m.container == c && m.alive && !m.retreated && st.owner(m.h) == static_cast<int32_t>(player)) out.push_back(m.id);
        break;
    }
    return out;
}

std::vector<at::OpportunityTargetEvent> BattleAi::opportunity_targets() {
    std::vector<at::OpportunityTargetEvent> out;
    std::vector<std::pair<uint64_t, int32_t>> now;
    const tc::CombatSystem& c = *b_->combat_;
    for (const TacticalBattle::Unit& u : b_->units_) {
        if (!u.alive || u.retreated || !c.has_unit(u.id)) continue;
        for (uint32_t k = 0; k < c.hardpoint_count(u.id) && k < 256; ++k) {
            const tc::ObjectId t = c.hardpoint_opportunity_target(u.id, k);
            if (t == tc::kNoObject) continue;
            const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(u.id)) << 8) | k;
            now.emplace_back(key, t);
            const auto it = std::lower_bound(opp_prev_.begin(), opp_prev_.end(), key,
                                             [](const std::pair<uint64_t, int32_t>& e, uint64_t v) { return e.first < v; });
            if (it != opp_prev_.end() && it->first == key && it->second == t) continue;  // same target as before
            if (!c.alive(t)) continue;
            at::OpportunityTargetEvent e;
            e.owner = u.container != 0xFFFFFFFFu ? b_->containers_[u.container].id : u.id;  // signal to the team object
            e.target = t;
            e.hardpoint_type = static_cast<int32_t>(c.hardpoint_kind(u.id, k));
            out.push_back(e);
        }
    }
    opp_prev_ = std::move(now);  // ascending (units ascending id, hard points ascending)
    return out;
}

std::vector<at::AttackStateSignal> BattleAi::attack_state_signals() {
    std::vector<at::AttackStateSignal> out;
    for (const tc::AttackStateEvent& e : b_->combat_->attack_state_events()) out.push_back({e.unit, e.target});
    return out;
}

// skai t15 (SKAI_T15_UNIT_DAMAGED §1.1-§1.5, §3 [H]): Take_Damage's tail hands each applied damage instance to the UNIT_AI
// behaviour of the victim, else of its container (0x3ab23b), which raises signal 0x18 {attacker, deliberate}. Gates: a
// resolvable live attacker; health and the pre-shield amount > 0 (a hit the shield takes whole still counts); a killing
// blow on the UNIT_AI holder raises nothing. No owner, structure, amount or cooldown filter. attacker = the shooter, a
// TEAM member replaced by its container (0x39b1a0); deliberate = its current attack target, formation target first
// (0x397a40), is the unit. [port] a target that names the hit member of the holder's team counts as the holder.
// [port] land only for now (skai t15, 2026-11-16): the space task forces get it with lane skai-b's space work.
std::vector<at::UnitDamagedSignal> BattleAi::unit_damaged_signals() {
    std::vector<at::UnitDamagedSignal> out;
    if (b_->space_ || !b_->combat_) return out;
    const objects::ObjectStore& st = mode_->primary();
    const tc::CombatSystem& c = *b_->combat_;
    for (const tc::DamageEvent& e : c.damage_events()) {
        if (e.source == tc::kNoObject || !(e.after_multipliers > 0.0f)) continue;
        const objects::ObjectHandle sh = st.find(static_cast<objects::ObjectId>(e.source));
        const objects::ObjectHandle vh = st.find(static_cast<objects::ObjectId>(e.victim));
        if (!st.alive(sh) || !st.alive(vh)) continue;
        // the UNIT_AI holder: the victim, else its container
        objects::ObjectId holder = objects::kNoObjectId;
        if (st.has_behavior(vh, kUnitAi)) {
            if (e.killed) continue;  // the killing blow on the holder
            holder = static_cast<objects::ObjectId>(e.victim);
        } else if (const TacticalBattle::Unit* vu = b_->unit_by_id(static_cast<objects::ObjectId>(e.victim));
                   vu && vu->container != 0xFFFFFFFFu) {
            const objects::ObjectId cid = b_->containers_[vu->container].id;
            const objects::ObjectHandle ch = st.find(cid);
            if (st.alive(ch) && st.has_behavior(ch, kUnitAi)) holder = cid;
        }
        if (holder == objects::kNoObjectId) continue;
        auto attacker = static_cast<objects::ObjectId>(e.source);
        if (const TacticalBattle::Unit* su = b_->unit_by_id(attacker); su && su->container != 0xFFFFFFFFu &&
                                                                       b_->containers_[su->container].team)
            attacker = b_->containers_[su->container].id;
        tc::ObjectId aim = tc::kNoObject;
        if (c.has_unit(e.source)) {
            aim = c.attack_goal(e.source);
            if (aim == tc::kNoObject) aim = c.current_target(e.source);
        }
        const bool deliberate = aim != tc::kNoObject && (static_cast<objects::ObjectId>(aim) == holder || aim == e.victim);
        out.push_back({static_cast<int32_t>(holder), static_cast<int32_t>(attacker), deliberate});
    }
    return out;
}

at::TacticalReinforcement BattleAi::hijack_unit(int32_t player, int32_t type, bool steal) {
    at::TacticalReinforcement fail;
    // ref5 t06 [port] tier 2, lane only: the spawn writes the parent galaxy and its answer is the new pool entry, so off the
    // critical lane HIJACK_UNIT fails (Production_Failed) instead.
    if (lane_view()) return fail;
    lane_live_read("hijack_unit");
    facts_.wrote();  // ref5 t07
    GalacticMode* gm = host_->galactic_mode();
    if (b_->finished_ || !gm || !gm->sim() || type < 0 || !b_->can_add_reserve(static_cast<uint32_t>(type))) return fail;
    galactic::GalacticSim& gs = *gm->sim();
    const PlanetIndex planet = b_->req_.info.planet;  // step 1: the destination is the battle planet
    const bool hero = b_->rules_->type(static_cast<uint32_t>(type)).hero;
    const objects::ObjectHandle h = gs.magic_spawn(player, planet, static_cast<uint32_t>(type), steal, hero);  // steps 2-4
    if (!gs.store().alive(h)) return fail;
    // Step 5: a unit created on the surface launches into the orbit fleet (op 4 + form; FAILED when blockaded).
    const auto& surface = gs.planets()[planet].surface;
    if (std::find(surface.begin(), surface.end(), h) != surface.end()) {
        const objects::ObjectHandle one[] = {h};
        if (gs.ai_blockaded(planet) || gs.launch_units(player, planet, one) != cmd::RejectReason::None) return fail;
    }
    if (!b_->add_reserve(h, player)) return fail;
    const auto key = static_cast<int32_t>(gs.store().id(h));
    for (const at::TacticalReinforcement& r : reinforcement_pool(player))
        if (r.key == key) return r;
    return fail;
}
// --- end battle host ---

// ref5 t06: with the lane on both writes are batch commands applied at the due tick (apply_host_release).
void BattleAi::unit_released(int32_t player, int32_t object_id, ai::tactical::TacticalAiHost::UnitRelease path) {
    if (lane_view()) return queue_host_release(kHostUnitReleased, player, object_id, static_cast<uint8_t>(path));
    lane_live_read("unit_released");
    facts_.wrote();  // ref5 t07
    unit_released_live(player, object_id, path);
}

void BattleAi::unit_left_task_force(int32_t object_id) {
    if (lane_view()) {  // the batch slot of the unit's owner (the snapshot store), as unit_released's
        const objects::ObjectStore& vs = world_->view_store();
        const objects::ObjectHandle h = vs.find(static_cast<objects::ObjectId>(object_id));
        return queue_host_release(kHostUnitLeftTaskForce, vs.exists(h) ? vs.owner(h) : -1, object_id, 0);
    }
    lane_live_read("unit_left_task_force");
    facts_.wrote();  // ref5 t07
    unit_left_task_force_live(object_id);
}

void BattleAi::unit_released_live(int32_t player, int32_t object_id, ai::tactical::TacticalAiHost::UnitRelease path) {
    if (b_->finished_ || !b_->abilities_ || object_id < 0) return;
    const objects::ObjectStore& st = mode_->primary();
    const objects::ObjectHandle h = st.find(static_cast<objects::ObjectId>(object_id));
    if (!st.alive(h) || st.owner(h) != player) return;
    alamo::tactical::abilities::AbilitySystem& ab = *b_->abilities_;
    if (!ab.has_object(object_id)) return;
    // 0x5ff610: no Active_By_Default while movement is locked (0x39ad40: the counter's shares and the forced-move term,
    // A1_READINGS §28.9; HUNT is not part of it).
    if (path == ai::tactical::TacticalAiHost::UnitRelease::TaskForce && b_->movement_locked(object_id)) return;
    ab.apply_active_by_default(object_id, b_->frame_);
}

void BattleAi::unit_left_task_force_live(int32_t object_id) {
    if (b_->finished_ || !b_->combat_ || object_id < 0) return;
    if (b_->combat_->priority_override(object_id) >= 0) b_->combat_->set_priority_override(object_id, -1);
    // cu8f t04a (CU8F_T04_T06_RE §1.1 writer 3, 0x5ff610): the stickiness threshold := the type's value, the unit first,
    // then each TEAM member (store order [port: the team list]; each member's own type).
    tc::CombatSystem& cs = *b_->combat_;
    if (cs.has_unit(object_id)) cs.reset_stickiness(object_id);
    const objects::ObjectStore& st = mode_->primary();
    const objects::ObjectHandle h = st.find(static_cast<objects::ObjectId>(object_id));
    if (!st.exists(h) || !st.has_behavior(h, objects::beh::Team)) return;
    for (const objects::ObjectHandle m : st.all_objects())
        if (st.alive(m) && st.container(m) == h && cs.has_unit(st.id(m))) cs.reset_stickiness(st.id(m));
}

// S7 t15 A1: the tick's signal 0x29 list (TacticalBattle::ability_finished_events).
std::vector<int32_t> BattleAi::ability_finished() {
    const std::span<const objects::ObjectId> ids = b_->ability_finished_events();
    return std::vector<int32_t>(ids.begin(), ids.end());
}

bool BattleAi::movement_locked(int32_t object_id) {
    lane_live_read("movement_locked");
    return object_id >= 0 && b_->movement_locked(object_id);
}

bool BattleAi::reinforce_in_flight(int32_t player, int32_t key) {
    lane_live_read("reinforce_in_flight");
    return landing_in_flight(player, key);
}

bool BattleAi::landing_in_flight(int32_t player, int32_t key) const {
    for (const TacticalBattle::Landing& l : b_->landings_) {
        const TacticalBattle::Participant& p = b_->parts_[l.participant];
        if (l.timed && l.state != 3 && p.owner == player && p.galactic_id == key) return true;
    }
    for (const TacticalBattle::ArrivalRec& r : b_->arrivals_) {  // aitac t05: a space reinforcement still arriving
        if (r.reinforce < 0 || static_cast<size_t>(r.reinforce) >= b_->parts_.size()) continue;
        const TacticalBattle::Participant& p = b_->parts_[static_cast<size_t>(r.reinforce)];
        if (p.owner == player && p.galactic_id == key) return true;
    }
    return false;
}

std::vector<ai::tactical::TacticalAiHost::ProductionCompleted> BattleAi::production_completed() {
    std::vector<ai::tactical::TacticalAiHost::ProductionCompleted> out;
    for (const TacticalBattle::ProductionDone& d : b_->production_done()) out.push_back({d.player, static_cast<int32_t>(d.type)});
    return out;
}

std::vector<ai::tactical::ReinforcementsUnloaded> BattleAi::reinforcements_unloaded() {
    std::vector<ai::tactical::ReinforcementsUnloaded> out;
    for (const TacticalBattle::LandingUnload& rec : b_->landings_unloaded()) {
        ai::tactical::ReinforcementsUnloaded r;
        r.player = rec.owner;
        r.key = rec.galactic_id;
        r.units.assign(rec.units.begin(), rec.units.end());
        out.push_back(std::move(r));
    }
    for (ai::tactical::ReinforcementsUnloaded& r : lane_unloaded_) out.push_back(std::move(r));  // sp3 slice 4d
    lane_unloaded_.clear();
    return out;
}
// The Weather_Category of every type, resolved once per type table: weather_category_[type] indexes weather_categories_.
void BattleAi::resolve_weather_categories() {
    const objects::GameObjectTypes& types = b_->catalog_->types();
    if (weather_category_.size() == types.size()) return;
    weather_category_.assign(types.size(), -1);
    weather_categories_.clear();
    for (size_t t = 0; t < types.size(); ++t) {
        const std::string_view c = types.get(static_cast<objects::TypeIndex>(t), objects::tags::Weather_Category);
        const auto it = std::find(weather_categories_.begin(), weather_categories_.end(), c);
        weather_category_[t] = static_cast<int32_t>(it - weather_categories_.begin());
        if (it == weather_categories_.end()) weather_categories_.emplace_back(c);
    }
}

float BattleAi::sight_weather_factor(int32_t type, int32_t) {
    lane_live_read("sight_weather_factor");
    if (b_->space_ || type < 0) return 1.0f;
    resolve_weather_categories();  // REF1 t04: the cached category, not a per-call type string lookup
    if (static_cast<size_t>(type) >= weather_category_.size()) return 1.0f;
    const int32_t c = weather_category_[static_cast<size_t>(type)];
    return c < 0 ? 1.0f : b_->weather().sight_modifier(weather_categories_[static_cast<size_t>(c)]);
}

// ref5 t07: the six signal lists in one call (the world takes each right before it handles it, or all at publish).
void BattleAi::frame_events(uint32_t kinds, at::FrameEvents& out) {
    if (kinds & at::FrameEvents::kOpportunity) out.opportunity = opportunity_targets();
    if (kinds & at::FrameEvents::kAttackStates) out.attack_states = attack_state_signals();
    if (kinds & at::FrameEvents::kDamaged) out.damaged = unit_damaged_signals();
    if (kinds & at::FrameEvents::kAbilityFinished) out.ability_finished = ability_finished();
    if (kinds & at::FrameEvents::kUnloaded) out.unloaded = reinforcements_unloaded();
    if (kinds & at::FrameEvents::kProduced) out.produced = production_completed();
}

// ref5 t07: one rule per fact family, read from the battle. A FactTable family's lane answer is the same rule's answer at
// the capture; a FactQuery family's lane answer reads the capture (FactEnv view, or the view_* tests: partial for the drop
// tests, whose rest runs at the apply, lane_drop_valid).
void BattleAi::wire_facts() {
    using Key2 = std::pair<int32_t, int32_t>;
    at::BattleFacts& f = facts_;
    f.pool.set_rule([this](const int32_t& p) { return reinforcement_pool(p); });
    f.reinforce_ok.set_rule([this](const int32_t& p) { return can_reinforce(p); });
    f.pop_cap.set_rule([this](const int32_t& p) { return population_cap(p); });
    f.pop_used.set_rule([this](const int32_t& p) { return population_used(p); });
    f.drop_frame.set_rule([this](const int32_t& p) -> std::optional<at::SpaceDropFrame> {
        at::SpaceDropFrame d;
        if (!space_drop_frame(p, d)) return std::nullopt;
        return d;
    });
    f.locked.set_rule([this](const int32_t& id) { return movement_locked(id); });
    f.in_flight.set_rule([this](const Key2& k) { return reinforce_in_flight(k.first, k.second); });
    f.builders.set_rule([this](const Key2& k) { return tactical_builders(k.first, k.second); });
    f.can_build.set_rule([this](const Key2& k) { return can_build_now(k.first, k.second); });
    f.pads.live = [this](int32_t p) {
        ensure_type_facts();
        lane_live_read("build_pads");
        return build_pads_in(live_env(), p);
    };
    f.pads.snapshot = [this](int32_t p) { return build_pads_in(view_env(), p); };
    f.pad_check.live = [this](int32_t p, int32_t pad, int32_t) {
        ensure_type_facts();
        lane_live_read("build_pad_check");
        return build_pad_check_in(live_env(), p, pad);
    };
    f.pad_check.snapshot = [this](int32_t p, int32_t pad, int32_t) { return build_pad_check_in(view_env(), p, pad); };
    f.prevention.live = [this](int32_t p, at::Vec3f pt) {
        ensure_type_facts();
        lane_live_read("space_prevention_hit");
        return prevention_hit_in(live_env(), p, pt);
    };
    f.prevention.snapshot = [this](int32_t p, at::Vec3f pt) { return prevention_hit_in(view_env(), p, pt); };
    f.base_alive.live = [this](int32_t p) {
        lane_live_read("space_reinforce_base_alive");
        return base_alive_in(live_env(), p);
    };
    f.base_alive.snapshot = [this](int32_t p) { return base_alive_in(view_env(), p); };
    f.replaced.live = [this](int32_t id) {
        lane_live_read("replaced_builder");
        return replaced_builder_in(live_env(), id);
    };
    f.replaced.snapshot = [this](int32_t id) { return replaced_builder_in(view_env(), id); };
    f.space_drop.live = [this](int32_t p, int32_t key, at::Vec3f pt, float facing, float sweep) { return space_drop_ok(p, key, pt, facing, sweep); };
    f.space_drop.snapshot = [this](int32_t p, int32_t key, at::Vec3f pt, float, float) { return view_space_drop_ok(p, key, pt); };
    f.land_drop.live = [this](int32_t p, int32_t key, at::Vec3f pt) {
        lane_live_read("land_drop_ok");
        return land_drop_valid(p, key, pt, false);
    };
    f.land_drop.snapshot = [this](int32_t p, int32_t key, at::Vec3f pt) { return view_land_drop_ok(p, key, pt); };
    f.line_clear.live = [this](int32_t id, at::Vec3f a, at::Vec3f b) { return attack_position_line_clear(id, a, b); };
    f.line_clear.snapshot = [this](int32_t id, at::Vec3f a, at::Vec3f b) { return b_->space_ || view_line_clear(id, a, b); };
    f.weather.live = [this](int32_t type, int32_t id) { return sight_weather_factor(type, id); };
    f.weather.snapshot = [this](int32_t type, int32_t) {  // the categories were resolved at the capture
        if (type < 0 || static_cast<size_t>(type) >= weather_category_.size()) return 1.0f;
        const int32_t c = weather_category_[static_cast<size_t>(type)];
        return c < 0 ? 1.0f : view_.weather_sight[static_cast<size_t>(c)];
    };
    f.damage_live = &b_->damage_tracking_;
    f.contrast_table = &b_->rules_->contrast;
}

// [port] The S10 stand-in (TacticalBattle::issue_orders, REF1 t02) for the sides that have no tactical AI, on its own
// cadence; the players this AI controls are left out.
void BattleAi::standin_orders() {
    if (b_->frame_ == 0 || (b_->frame_ - 1) % kStandinInterval != 0) return;
    b_->issue_orders(*mode_, host_->random().synced(), [this](int32_t owner) { return controls(owner); });
}

}  // namespace alamo::sim
#endif
