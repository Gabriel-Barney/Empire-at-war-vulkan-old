#include "alamo/tactical/movement/unit_params.hpp"

#include <array>

#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/type_fields.hpp"

namespace alamo::tactical::movement {

UnitDesc unit_desc_from_type(const objects::GameObjectTypes& t, uint32_t ti) {
    UnitDesc d;
    d.max_speed = t.get(ti, objects::tags::Max_Speed);
    d.turn_rate = t.get(ti, objects::tags::Max_Rate_Of_Turn);
    d.accel = t.get(ti, objects::tags::OverrideAcceleration);
    d.decel = t.get(ti, objects::tags::OverrideDeceleration);
    d.hover_height = t.get(ti, objects::tags::Hover_Height);  // s78n t18
    {  // trn1 t03 (MOVEMENT §3.6): x Scale_Factor (type +0x658)
        const float sc = t.scale_factor(ti);
        d.overall_length = t.get(ti, objects::tags::Overall_Length) * sc;
        d.overall_width = t.get(ti, objects::tags::Overall_Width) * sc;
    }
    d.close_enough_angle = t.get(ti, objects::tags::Close_Enough_Angle_For_Move_Start);
    // mcx4 t11 (MCX4_T11_T12_RE_ANSWERS §B2): the LANDBOMBER facing lookahead cap (unset: the type ctor's 30) and the BIKE
    // lateral acceleration (unset 0: the sideways speed never changes)
    if (t.is_set(ti, objects::tags::MaxFacingLookAheadFrames))
        d.max_facing_lookahead = t.get(ti, objects::tags::MaxFacingLookAheadFrames);
    d.lateral_accel = t.get(ti, objects::tags::LateralAcceleration);
    d.movement_class = t.get(ti, objects::tags::MovementClass);
    d.collision_class = t.get(ti, objects::tags::UnitCollisionClass);
    d.formation_spacing = t.get(ti, objects::tags::FormationSpacing);
    d.hero = (t.get(ti, objects::tags::CategoryMask) & 0x6400000u) != 0;  // LandHero | SpaceHero | NonCombatHero
    d.type_name = std::string(t.name(ti));
    d.grouping = t.get_string(ti, objects::tags::FormationGrouping).empty() ? 2 : t.get(ti, objects::tags::FormationGrouping);
    d.formation_order = t.get(ti, objects::tags::FormationOrder);
    // s78n t14 (FIGHTER_FLYING §7.2): ContainerArrangement (None -1 when unset)
    d.container_arrangement = t.get_string(ti, objects::tags::ContainerArrangement).empty() ? -1 : t.get(ti, objects::tags::ContainerArrangement);
    d.movement_prediction_interval = t.get(ti, objects::tags::MovementPredictionInterval);  // s78n t14 part B
    d.avoidance_disabled = t.get(ti, objects::tags::Avoidance_Disabled);
    const uint64_t layer = t.get(ti, objects::tags::Space_Layer);
    d.space_layer = layer != 0 ? static_cast<uint32_t>(layer) : static_cast<uint32_t>(kLayerFrigate);
    d.layer_z = t.get(ti, objects::tags::Layer_Z_Adjust);
    d.space_layer_type = static_cast<uint32_t>(layer);  // §4.2: threat mode needs 0
    d.stationary_space_layer = static_cast<uint32_t>(t.get(ti, objects::tags::Stationary_Space_Layer));  // cu8j t01 (§4.6.4)
    // REMAINING_GAPS §4.3-4.7: special-locomotor fields (the kind itself is the host's opt-in, see the header).
    d.hover_offset = t.get(ti, objects::tags::Hover_Offset);
    d.max_lift = t.get(ti, objects::tags::Max_Lift);
    d.max_rate_of_roll = t.get(ti, objects::tags::Max_Rate_Of_Roll);
    d.bank_turn_angle = t.get(ti, objects::tags::Bank_Turn_Angle);
    if (d.bank_turn_angle == 0.0f) d.bank_turn_angle = static_cast<float>(t.get_int(ti, objects::tags::Bank_Turn_Angle));  // int-typed row
    if (!t.is_set(ti, objects::tags::Bank_Turn_Angle)) d.bank_turn_angle = 70.0f;  // unset: 70 (FIGHTER_FLYING §1.4)
    d.min_speed = t.get(ti, objects::tags::Min_Speed);
    d.min_speed_fraction_for_turn = t.get(ti, objects::tags::Min_Speed_Fraction_For_Turn);  // s78n t26.3
    d.strafe_distance = t.get(ti, objects::tags::Strafe_Distance);
    d.prepare_strafe_height = t.get(ti, objects::tags::Prepare_Strafe_Height);
    d.has_pre_turn_anim = t.get(ti, objects::tags::Has_Pre_Turn_Anim);
    // S7 t06 / t14: Uses_Multiple_Locomotors (MOVEMENT §3.1.1). The primary is the behaviour-list locomotor the host
    // routes (the swap back builds Primary_Locomotor_Name; every FoC user names the WALK_LOCOMOTOR of its list).
    d.uses_multiple_locomotors = t.get(ti, objects::tags::Uses_Multiple_Locomotors);
    d.secondary_locomotor = locomotor_kind_from_token(t.get(ti, objects::tags::Secondary_Locomotor_Name));
    d.has_secondary_locomotor = d.secondary_locomotor == LocomotorKind::Jetpack &&  // [port] JETPACK secondaries only
                                locomotor_kind_from_token(t.get(ti, objects::tags::Primary_Locomotor_Name)) != LocomotorKind::Jetpack;
    d.alt_max_speed = t.get(ti, objects::tags::Alternate_Max_Speed);  // unconditional: the type defaults are 1.0 / 2.0
    d.alt_turn_rate = t.get(ti, objects::tags::Alternate_Max_Rate_Of_Turn);
    // S8 t11: Deployment_Anim_Rate, unset -> the type ctor's 1.0 (MOVEMENT §3.2).
    if (t.is_set(ti, objects::tags::Deployment_Anim_Rate)) d.deployment_anim_rate = t.get(ti, objects::tags::Deployment_Anim_Rate);
    // s78n t26.1 (MOVEMENT §3.2.1): walking while deployed (deploy_ability is the host's).
    d.deploys = t.get(ti, objects::tags::Deploys);
    d.auto_deploys = t.get(ti, objects::tags::Auto_Deploys);
    d.max_walk_deployed = t.get(ti, objects::tags::Max_Dist_Walk_While_Deployed);
    d.deployed_walk_speed = t.get(ti, objects::tags::Deployed_Walk_Speed);
    // s78n t26 closing review (MOVEMENT §3.4 "Units with Walk_Transition"): the flag and S = Scale_Factor x
    // Movement_Animation_Speed (unset: the type defaults 1.0 / 0.25)
    d.walk_transition = t.get(ti, objects::tags::Walk_Transition);
    {
        const float sc = t.scale_factor(ti);
        const float mas = t.is_set(ti, objects::tags::Movement_Animation_Speed)
                              ? t.get(ti, objects::tags::Movement_Animation_Speed)
                              : 0.25f;
        d.anim_scale_speed = sc * mas;
    }
    // s78k t01 (MOVEMENT §3.5.1): the arrival turn's stopped rate and the direction count (unset -> the type default 1).
    d.stopped_turn_rate = t.get(ti, objects::tags::Stopped_Rate_Of_Turn);
    if (t.is_set(ti, objects::tags::Movement_Directions_Count))
        d.movement_directions = t.get(ti, objects::tags::Movement_Directions_Count);
    d.rotation_animation_speed = t.get(ti, objects::tags::Rotation_Animation_Speed);  // s78l t02
    // bkm1 t08 (b0050, MOVEMENT §3.5.3 item 6): UseSecondaryFacing and its coefficients; an unset row keeps the type default
    d.use_secondary_facing = t.get(ti, objects::tags::UseSecondaryFacing);
    const auto sec = [&](const objects::FloatTag& k, float def) {
        if (!t.is_set(ti, k)) return def;
        const float f = t.get(ti, k);
        return f != 0.0f ? f : static_cast<float>(t.get_int(ti, k));  // the schema lists two rows as int
    };
    d.secondary_in_place = sec(objects::tags::SecondaryTurnInPlaceROTCoefficient, 0.5f);
    d.secondary_angle = sec(objects::tags::SecondaryTurnAngle, 90.0f);
    d.secondary_min = sec(objects::tags::MinSecondaryTurnROTCoefficient, 0.0f);
    d.secondary_max = sec(objects::tags::MaxSecondaryTurnROTCoefficient, 1.0f);
    d.secondary_lookahead = sec(objects::tags::SecondaryTurnLookaheadDistance, 0.0f);
    // S8 t24: the TURRET of a Deploys type (MOVEMENT §3.2 "Turret recentre around deploy"). s78f t08 (TACTICAL_COMBAT
    // §3.1.1 "s78f t08 answers" 6 [H]): every TURRET type slews the same way (0x632050: one Turret_Rotate_Speed step per
    // service, no shortcut for non-deployers; the deploy state only picks the deployed extents / rest angle), so the model
    // covers every TURRET type; the tick runs it on land only. [port] Turret_Rotate_Speed <= 0: no model (aimed at once).
    if (t.has_behavior(ti, objects::BehaviorSet::Union, 0x0B) && t.get(ti, objects::tags::Turret_Rotate_Speed) > 0.0f) {
        d.turret = true;
        d.turret_speed = t.get(ti, objects::tags::Turret_Rotate_Speed);
        if (t.is_set(ti, objects::tags::Turret_Rotate_Extent_Degrees)) d.turret_extent = t.get(ti, objects::tags::Turret_Rotate_Extent_Degrees);
        d.turret_deployed_extent = t.is_set(ti, objects::tags::Deployed_Turret_Rotate_Extent_Degrees)
                                       ? t.get(ti, objects::tags::Deployed_Turret_Rotate_Extent_Degrees)
                                       : d.turret_extent;
        const std::array<float, 4> r = t.get(ti, objects::tags::Turret_Rest_Angle), dr = t.get(ti, objects::tags::Turret_Deployed_Rest_Angle);
        for (size_t k = 0; k < 3; ++k) {
            d.turret_rest[k] = r[k];
            d.turret_deployed_rest[k] = dr[k];
        }
        if (t.is_set(ti, objects::tags::Turret_Rotate_To_Rest_Delay_Sec)) d.turret_rest_delay = t.get(ti, objects::tags::Turret_Rotate_To_Rest_Delay_Sec);
    }
    // OI tactical: stagger, jiggle and crush fields (MOVEMENT.md §1.6, §3.5 step 6, §5.5).
    d.path_frame_delay = t.get(ti, objects::tags::PathFrameDelay);
    d.formation_raggedness = t.get(ti, objects::tags::FormationRaggedness);
    d.max_jiggle_distance = t.get(ti, objects::tags::MaxJiggleDistance);
    d.crusher = t.has_behavior(ti, objects::BehaviorSet::Union, 0x26);  // SQUASH
    d.supercrusher = t.get(ti, objects::tags::Is_Supercrusher);
    d.squashable = t.get(ti, objects::tags::Is_Squashable);
    d.squashable_by_supercrusher = t.get(ti, objects::tags::Is_Squashable_By_Supercrusher);
    const float scale = t.scale_factor(ti);
    d.hard_x = t.get(ti, objects::tags::Custom_Hard_XExtent) * scale;
    d.hard_y = t.get(ti, objects::tags::Custom_Hard_YExtent) * scale;
    d.hard_x_deployed = t.get(ti, objects::tags::Custom_Hard_XExtent_Deployed) * scale;  // s78n t23
    d.hard_y_deployed = t.get(ti, objects::tags::Custom_Hard_YExtent_Deployed) * scale;
    d.box_expansion = t.get(ti, objects::tags::MovementBoxExpansionFactor);  // s78n t20
    const float soft = t.get(ti, objects::tags::Custom_Soft_Footprint_Radius);
    if (soft > 0.0f) {
        d.radius = soft * scale;
    } else {
        switch (d.collision_class) {
            case 5: d.radius = 6.0f; break;
            case 6: d.radius = 10.0f; break;
            case 7: d.radius = 20.0f; break;
            case 8: d.radius = 40.0f; break;
            case 9: d.radius = 80.0f; break;
            default: d.radius = 15.0f; break;
        }
    }
    return d;
}

}  // namespace alamo::tactical::movement
