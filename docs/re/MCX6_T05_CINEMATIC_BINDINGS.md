# MCX6 t05: the cinematic Lua bindings (retail behaviour and the port's consumers)

RE pass of 2026-11-17 for roadmap mcx6 t05 (binding ledger: no `recorded` binding without a consumer). Read from the
decompiled retail binary (StarWarsG.exe, FoC Steam build; `re_reference/`) and the existing specs it cites. Behaviour
only, no code. Tags: [H] read directly, [M] inferred from the code, [L] guess. RVAs relative to image base 0x140000000.
"Port" lines say where alamo_game consumes the binding (`engine/lua/src/binding_table.inc` consumer column).

## 1. `GameObjectWrapper.Cinematic_Hyperspace_In(delay_frames)` (0x574e50 -> 0x5c9a80)

Spec: L1_S4_REMAINING §3.4 "Hyperspace in" [H]; sound: AUDIO.md §9.6 [H].
- Space only, one number; the type needs `Hyperspace` and a locomotor that implements the call (a `STARSHIP_LOCOMOTOR`
  object ignores it). The locomotor takes the movement lock, leaves its coordinator, keeps its position as the
  destination, takes the hyperspace start point, hides the model and enters state 0x2c with timer := 25 - delay.
- State 0x2c is the reinforcement arrival (state 4) without its first-frame draw and without the hyperspace hold: one per
  frame, the model shows at timer 35, the owner faction's `SFXEvent_Arrive_From_Hyperspace` plays attached to the object
  at timer 45 (0x6269f0), the arrival finishes at 150. So, after the call: shown at delay + 10 frames, heard at
  delay + 20, landed at delay + 125. No random draw.
- Port: the binding clears a Lua `Hide` (modc t43). alamo_game (`App::script_cinematic_event`, `GameSession::
  script_cinematics`, `alamo/game/script_cinematics.hpp`) hides the object until delay + 10 frames (model, pick, sound
  mute through `GameSession::script_hidden`) and plays the arrival sound at delay + 20 (`GameAudioHost::
  cinematic_arrival`). [port] The ship is not flown in from its start point (the port's reinforcement arrivals are not
  either), the locomotor checks are not made, and the frame is the battle frame when the game drains the log (at most
  the ticks of one render frame after the call).

## 2. `Do_End_Cinematic_Cleanup()` (0x541e20 -> 0x38ed10) and `GameObjectWrapper.In_End_Cinematic(bool)` (0x57ab30)

- `In_End_Cinematic(b)` writes the object's end-cinematic byte (+0x3c9) := b ("Missing required boolean parameter."
  without one; "Object invalid" on a dead wrapper). Only 0x38ed10 reads it; the cinematic retreat and the clone path set
  it too (L1_S4_REMAINING §3.4). [H]
- `Do_End_Cinematic_Cleanup` resolves the **neutral player**: the first faction of the faction list (0x140a172d0) with
  `Is_Neutral` (+0x108, 0x331cc0), then that faction's player (0x294d30). It queries the tactical object manager's
  all-objects list (0x2a9ff0 with behaviour 1 = LOCOMOTOR, any player list, **excluding** objects whose owner (+0x58) is
  the neutral player; no radius, no count limit) and calls 0x38ed10 on each. [H; the query arguments read from
  0x2a9ff0's body: arg 2 = behaviour, arg 3 = player list (-1 = all), arg 4 = excluded owner]
- 0x38ed10, only in a tactical mode and only when +0x3c9 is clear: every behaviour's suspend virtual (+0xc0); the model
  hidden (0x264c80(model, 1)); removed from the land collision grid (0x508ec0 / 0x20abe0) when it was in it; teleported
  to (1, 1, 0) through 0x3abb80. Then the collision resolve pass 0x50a780 runs. Nothing shows the objects again. [H]
  (TACTICAL_GAPS §7.2.)
- Vanilla use: the story end cinematics mark their actors (`han.In_End_Cinematic(true)`, ...) and then clean up, so every
  other unit leaves the shot.
- Port: alamo_game hides the cleaned-up objects for the rest of the battle (`end_cinematic_cleanup_set`: locomotor
  objects of the battle store not owned by `GalacticSim::neutral_player()`, not marked in
  `lua::PresentationState::in_end_cinematic`). The sim part is the binding's (`bind_presentation.cpp`, mcx6): in a
  tactical mode the same objects get `ScriptObjectState::end_cinematic_suspended` and move to (1, 1, 0) through
  `TacticalScriptHooks::teleport`; the battle host reads the flag every tick. [port] The suspension is the port's per-tick
  unit controls (the locomotor does not run, the weapons do not fire), standing for every behaviour's suspend virtual.

## 3. `Promote_To_Space_Cinematic_Layer(object)` (0x5433f0)

- "Command only valid in a tactical game." outside a tactical mode; "... only valid in a land or space tactical game."
  for another tactical kind; the two argument errors of LUA_API.md. [H]
- Moves the object to its own position with z + **20000** through the reposition 0x3abb80 (the path `Teleport` uses:
  leave the coordinator, locomotor move, adopt; CU8K_T02_T06_RE row "container eject / reposition"). Vanilla calls it on
  cinematic markers (`space_cinematic_center`, `intro_camera_marker`, `cinematic_lua_shuttle_pos`), so the camera keys
  and the transports spawned there work far above the battlefield. [H]
- Port: the binding itself moves the object (`TacticalScriptHooks::teleport`, the store without a host); ledger state
  `full`. [port] The two mode refusals are not raised.

## 4. `GameObjectWrapper.Show_Emitter(name, bool)` (0x581120)

- Two arguments (string, boolean; the LUA_API.md errors). The name is converted by the `ModelEmitterType` enum table
  (EnumConversionClass<ModelClass::EmitterType>, built by 0x2641c0): `Engines` 0, `Turbo_Engines` 1, `Ion_Stun` 2,
  `Sprint_Ability_Effect` 4, `Power_To_Weapons_Effect` 5, `Missile_Shield` 7 (3 and 6 have no name). An unknown name logs
  "GameObjectWrapper::Show_Emitter -- unknown emitter type %s." and does nothing. [H] Case sensitivity of the lookup not
  read. [L: insensitive]
- A known name calls 0x265ae0(model wrapper, index, bool): the particle prefix group of BKX1_RENDER_RE §1.1 (`pe`, `pte`,
  `pi`, `pgw`, `pas`, `pptw`, `pem`, `prs`): every particle proxy of the model whose name starts with the prefix gets its
  hidden bit := !bool; the group state is saved. [H] For a SimpleSpace unit the locomotor service re-shows `pe` / `pte`
  every service (BKX1_RENDER_RE §1.2), so a script hide of those groups does not last there. [M]
- No shipped script calls it.
- Port: the binding keeps `PresentationState::emitters`; alamo_game turns it into per-object group bits
  (`game::emitter_overrides`) and the battle view's `SimInstanceBuilder::set_emitter_source` hides / shows those proxies
  (a hide wins for a proxy in two groups; pe / pte left to the glow rule on glow units). [port] The unknown-name error is
  not raised (unknown names are ignored by the game).

## 5. `Cancel_Fast_Forward()` (CancelFastForwardClass vf10 0x6aa1e0 -> HUD 0x2f4a80)

- Calls the command bar (0x140b27f60) method 0x2f4a80. Unless the game mode manager's test 0x28af60 holds [M:
  multiplayer], it takes the fast-forward button component (id 0x26b galactic / 0x26c tactical, by the tactical-mode
  test), sets its state 3 -> 1 (pressed -> normal) and its colour to white, and when the HUD speed state (+0x2968) is 2
  (fast) sets it to 1 and clears the global fast flag 0x140b153d5. [H code, M names]
- Vanilla: 48 calls, at the start of story cinematics.
- Port: the binding clears `PresentationState::fast_forward`; `GameSession::sync_rate_lock` turns the host's fast pacing
  off at the next tick (`binding-consumer` there). [port] The port's `b_fast_forward` button has no pressed state.

## 6. `Resume_Hyperspace_In()` (0x543570)

Spec: L1_S4_REMAINING §3.4, BATTLE_TRANSITIONS §2.9.1 [H]: clears the reinforcement hyperspace hold 0x140b27d5f that
LINK_TACTICAL Param9 = 2 sets. Port: the binding clears the hold through `TacticalScriptHooks::resume_hyperspace_in`; the
held arrivals' timers run again and their `Arrive` entries reach `GameAudioHost::object_events` (the arrival sound), which
is the ledger's consumer.

## 7. `GameObjectWrapper.Play_Cinematic_Engine_Flyby()` (0x57ea50)

- "GameObjectWrapper::Play_Cinematic_Engine_Flyby -- Object invalid" on a dead wrapper. Otherwise, when the object type's
  **`SFXEvent_Ambient_Moving`** (type +0x1520, STRUCTS.md) is set, plays it through the SFX manager (0x140b27e60) attached
  to the object (0x2d5320, no extra flags). Arguments are not read. [H]
- Vanilla: one call (`fett.Play_Cinematic_Engine_Flyby()`, STORY_EMPIRE_ACTI_M03_SPACE).
- Port: alamo_game plays the type's `SFXEvent_Ambient_Moving` attached to the object (`GameAudioHost::
  cinematic_engine_flyby`).

## 8. `Set_New_Environment(int)` / `Set_Cinematic_Environment(bool)`

Specs: CU8F_T03_T05_RE §2.2 and CU8G_T02_ENV_PICK_RE §5 [H]. The bindings change the battle's active environment
through `TacticalScriptHooks::set_new_environment` / `set_cinematic_environment`; alamo_game's battle view reads
`TacticalBattle::environment_index` every capture and re-applies lighting, fog, clouds and the particle wind
(`BattleView::apply_environment`), which is the ledger's consumer.
