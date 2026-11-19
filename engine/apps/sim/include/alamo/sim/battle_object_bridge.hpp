#pragma once
// ref2 t04 (REF2, serves aitac t03): the battle lifecycle of a Lua ObjectBridge. Every VM that runs inside a tactical battle
// (the story / object-script VMs: CampaignBattles::story_bridge_begin; the tactical AI VMs: BattleAi) gets its create,
// damage, owner-change and removal hooks from make_object_bridge, so a script's object goes through the battle's adopt
// step and its systems instead of the bare ObjectStore:
//   spawn_unit             Spawn_Unit / Create_Generic_Object / attached effects -> TacticalBattle::script_spawn
//   spawn_company          Spawn_Unit of a company type                          -> TacticalBattle::script_spawn_company
//   take_damage            Take_Damage(amount)                                    -> TacticalBattle::script_damage
//   take_hardpoint_damage  Take_Damage(amount, hardpoint)                         -> TacticalBattle::script_hardpoint_damage
//   change_owner           Change_Owner                                           -> TacticalBattle::script_change_owner
//   destroy_object         Despawn / Destroy_Contained_Objects                    -> TacticalBattle::script_despawn
// and sets ObjectBridge::battle_lifecycle: a binding whose lifecycle hook declines refuses (no object, no damage, no
// removal) rather than writing the store. Only available with engine/lua (ALAMO_SIM_HAVE_STORY).
#include <functional>

namespace alamo::lua {
class ObjectBridge;
}

namespace alamo::sim {

class TacticalBattle;
class TacticalMode;

// `mode` resolves the battle's tactical mode at call time (the story bridge is built before the mode is the host's
// current one); null refuses. `allowed` (may be empty): false refuses every lifecycle call (the AI lane's VMs, whose
// handles are a snapshot store's while their scripts run off the critical lane). The battle must outlive the bridge's
// use (both bridges are dropped with their battle).
void make_object_bridge(TacticalBattle& battle, std::function<TacticalMode*()> mode, lua::ObjectBridge& out,
                        std::function<bool()> allowed = {});

}  // namespace alamo::sim
