#include "alamo/game/battle_orders.hpp"

#include "alamo/game/game_session.hpp"
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::game {

namespace cmd = alamo::commands;

// ref2 t06: the orders go through TacticalBattle::apply_order, the battle's one validate-and-apply entry (the AI's orders
// it does not shape itself go there too); this layer keeps the human-only parts: which events the order hook routes, and
// the diagnostics counters.
cmd::RejectReason route_battle_order(sim::CampaignBattles& battles, sim::SimHost& host, uint8_t player, const cmd::EventData& ev,
                                     const std::vector<objects::ObjectId>& selection, BattleOrderStats* stats) {
    using R = cmd::RejectReason;
    using T = cmd::EventType;
    sim::TacticalBattle* battle = battles.battle();
    sim::TacticalMode* tm = host.tactical();
    if (!battle || !tm || battle->finished()) return R::Unhandled;
    const T t = ev.type();
    const bool move = t == T::MoveToPosition || t == T::MoveToPositionFacing || t == T::MoveToRay || t == T::MoveToRayFacing ||
                      t == T::Facing || t == T::MoveToObject || t == T::Escort;
    if (!move && t != T::Attack && t != T::StopMovement && t != T::Reinforce && t != T::Retreat) return R::Unhandled;
    const R r = battle->apply_order(*tm, player, ev, selection, host.random().synced());
    if (stats) {
        if (r == R::None) ++stats->routed;
        else ++stats->rejected;
        if (r == R::None) {
            if (move) ++stats->moves;
            else if (t == T::Attack) ++stats->attacks;
            else if (t == T::StopMovement) ++stats->stops;
            else if (t == T::Reinforce) ++stats->reinforcements;
        }
    }
    return r;
}

}  // namespace alamo::game
