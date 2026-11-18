"""Ledger lint (docs/MOD_COMPAT_DESIGN.md §7.2 and §9.1, roadmap mcx0 t03 / t04).

Checks that the generated ledgers match the code, without writing anything:
  tag ledger      engine/objects/generated/tag_ledger.inc (engine/objects/tools/gen_tag_ledger.py)
                  - every row's state is what the code gives now (a changed consumer count alone is not a failure:
                    the generator refreshes it whenever the ledger is regenerated);
                  - no consumed / verified row without a consumer;
                  - no row moves from consumed / verified to stored / unread (a reader was removed);
                  - every "// ledger: consumes|verifies <Class>.<Tag>" annotation names a row and sits next to code
                    (consumes) or in a test that names the tag (verifies);
                  - every RETAIL_UNUSED entry (roadmap mcx4) names a row nothing reads and cites an existing docs/re
                    page that names the tag; such a row is retail_unused, settled like a consumed one.
                  - every OUT_OF_SCOPE entry (mcx4 t16) names a row nothing reads, is not also RETAIL_UNUSED and gives a
                    scope reason labelled in docs/DECISIONS.md "Out-of-scope tag-ledger rows"; such a row is out_of_scope,
                    not a gap.
  binding ledger  engine/lua/src/binding_table.inc (engine/lua/tools/gen_bindings.py): the effect and consumer
                  columns are what the code gives now.
A failure prints what differs and the command that regenerates the ledger. Runs in build_agent.cmd (before the build),
in build_gate.cmd (tools/lint/gate_summary.py) and as the ctest entry alamo_objects_tag_ledger_lint.

Usage: check_tag_ledger.py [--selftest | --gaps [--group NAME] [--counts]]
  --gaps    list the base-FoC honour gaps still open (tier 1, base_uses > 0, state unread / stored: the mcx4 exit test
            needs none) from the committed ledger, and exit 1 when there are any
  --group   only the rows of one work group (GAP_GROUPS below: the mcx4 task that owns them)
  --counts  one line per work group instead of the rows
Exit: 0 in step, 1 differences or annotation errors, 2 a ledger file is missing.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
GEN_TAGS = os.path.join(ROOT, "engine", "objects", "tools", "gen_tag_ledger.py")
GEN_BINDINGS = os.path.join(ROOT, "engine", "lua", "tools", "gen_bindings.py")


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def check_tags(gen):
    if not os.path.isfile(gen.OUT_INC):
        print(f"tag ledger: {os.path.relpath(gen.OUT_INC, ROOT)} is missing; run python engine/objects/tools/gen_tag_ledger.py")
        return 2
    with open(gen.OUT_INC, encoding="utf-8") as f:
        committed = gen.parse_inc(f.read())
    ledger, problems = gen.build(with_extras=False)
    errors = [f"annotation: {p}" for p in problems]
    errors += [f"regression: {b}" for b in gen.regressions(committed, ledger, set())]
    for key, (state, n, _) in sorted(committed.items()):
        if state in ("consumed", "verified") and n == 0:
            errors.append(f"{key[0]}.{key[1]}: {state} with no consumer")
    now = {(r["class"], r["tag"].lower()): r for r in ledger}
    stale = []
    for key in sorted(set(now) | set(committed)):
        r, c = now.get(key), committed.get(key)
        if r is None or c is None:
            stale.append(f"{key[0]}.{key[1]}: {'no longer a descriptor row' if r is None else 'new row'}")
        elif r["state"] != c[0]:
            stale.append(f"{r['class']}.{r['tag']}: ledger {c[0]} ({c[1]} files), code {r['state']} ({r['consumers']} files"
                         + (f", first {r['consumer']}" if r["consumer"] else "") + ")")
    for e in errors:
        print("tag ledger: " + e)
    for s in stale[:40]:
        print("tag ledger: stale: " + s)
    if len(stale) > 40:
        print(f"tag ledger: ... {len(stale) - 40} more stale rows")
    if stale and not errors:
        print("tag ledger: regenerate with: python engine/objects/tools/gen_tag_ledger.py")
    if errors or stale:
        return 1
    print(f"tag ledger: {len(ledger)} rows in step with the code")
    return 0


# mcx4 t08: the open gap rows split into work groups, one roadmap task each (first match on "Class.Tag" wins; the
# task text names the group: "rows: check_tag_ledger.py --gaps --group render"). A row that moves out of the gaps
# (consumed, verified, retail_unused) leaves its group by itself; a new row lands in the first group that matches.
GAP_GROUPS = [
    # t01-t07: the named tags of the group's first tasks.
    ("named", r"^Campaign\.(Special_Case_Production|Is_Listed|Is_Autoresolve_Allowed|Autoresolve_Exclusion_Locations"
              r"|Corruption_Level_Override)$"
              r"|^GameConstants\.(MaximumSpecialStructures\w*|MaximumStarbaseLevel|MaximumGroundbaseLevel|Progressive_Taxation"
              r"|Income_Redistribution|TradeRouteMovementFactor|Skirmish_Buy_Credits|Max_Remote_Bombs_Per_Player"
              r"|First_Strike_Extra_Damage_Percent)$"
              r"|^Faction\.(Credits_Accumulation_Factor|Corruption_Tactical_\w+_Multipliers)$"
              r"|^GameObjectType\.(Turret_Targets_\w+|\w+_Turret_Target|Fires_Forward|Projectile_Max_Scan_Range"
              r"|Projectile_Blast_Area_Dropoff(_Tiers)?|Size_Value|Tactical_Production_Queue|Required_Timeline|Can_Be_Only_One"
              r"|Build_Limit_Lifetime_For_All_Allies|Tactical_Build_Increments_Tech_Level"
              r"|Tactical_Destruction_Decrements_Tech_Level|Cheap_Bribe_Cost|Expensive_Bribe_Cost|Cash_Point_\w+"
              r"|Disallows_Hyperspace_Retreat|Respawn_Whole_Team_When_Killed|Is_Indigenous_Unit_Corruptible)$"
              r"|^(Income_Stream_Mod_Ability\.(Affects_All_Allied_Sources|Interval_Multiplier)|Absorb_Blaster_Ability\."
              r"Damage_Absorb_\w+|Ability_Base\.Unit_Strength_Category|Siphon_Credits_Ability\.Evade_Detection_Chance"
              r"|Income_Stream_Ability\.Split_Income_With_Allies|Spawn_Ability\.Recharge_Time_In_Seconds)$"),
    # Multiplayer-named rows that a skirmish or single player reads in retail (mcx4 t16: retail runs skirmish as a
    # multiplayer game type), and three rows docs/re/xml_field_tables.md files under HardwareConfiguration that are an
    # ability's fields (offsets 0x18 / 0x178 / 0x180): not out of scope, so they stay with the group that reads them.
    ("galactic", r"^(GameConstants\.Default_Bounty_By_Category_MP|GameObjectType\.Override_Bounty_MP)$"),
    ("tactical", r"^(Faction\.Create_Player_In_Multiplayer_Games"
                 r"|HardwareConfiguration\.(Type|Apply_To_All_Allies|Additional_Garrison_Units))$"),
    ("ui", r"^(Campaign\.Is_Multiplayer|GameObjectType\.MP_Encyclopedia_Text"
           r"|GameConstants\.(MP_Default_\w+|Multiplayer_Losing_Team_Bonus_Credit_Percentage))$"),
    # Out of the port's scope: the rows of OUT_OF_SCOPE in gen_tag_ledger.py (docs/DECISIONS.md "Out-of-scope tag-ledger
    # rows": multiplayer, hardware detection, graphic detail levels, debug keys, localised splash / credits). Rows in
    # that table are out_of_scope, not gaps; a new row matching here is a candidate for it.
    ("scope", r"(^|\.)(MP_|Multiplayer|Quickmatch|Debug_|Message_Of_The_Day|Create_Player_In_Multiplayer|Is_Multiplayer"
              r"|Is_Quickmatch|Default_Bounty_By_Category_MP|Override_Bounty_MP|MP_Encyclopedia|AutoResolveVote"
              r"|Distribute_Credit_Quantum|SyncedFrameInterval|ShouldDisplay|ShowUnitAIPlanAttachment|SFXEvent_Debug"
              r"|Japanese_|Localized_|Credits_Logo)|Player_Taunt$|^HardwareConfiguration\.|^GraphicDetailLevel\."),
    ("ai", r"^GameConstants\.(AI_|Saliency_)|Unit_Command_Rankings|^GameObjectType\.(Harass_Enemy_Exclusion_Range"
           r"|Avoid_Enemy_Exclusion_Range|Ignore_For_Reoptimization)$"),
    ("galactic", r"^GameConstants\.(Fleet_Maintenance_Update_Delay_Seconds|MaximumPoliticalControl"
                 r"|Indigenous_Spawn_Destruction_Reward|Bribery_Fleet_Reveal_Range|AutomaticAutoResolve|AlwaysBypassAutoResolve"
                 r"|Battle_Pending_Timeout_Seconds|Activated_\w+)$|^GameObjectType\.(Hyperspace_Fleet_Reveal_Range"
                 r"|Corruption_\d_Success_Credit_Bonus|Prevents_Story_Campaign_Autoresolve|SpaceAutoResolve\w+"
                 r"|Build_Countdown_Timer)$|^Difficulty_Adjustment\.Bribe_Cost_Multiplier$"),
    ("audio", r"SFX|Speech|Music_Event|^DynamicTrack\.|_Cough_"),
    ("render", r"^(Decal|Flare|LightSource|LightningEffect|TacticalCamera|Cable_Ability)\.|^TradeRoute\.(Color|Render_Mode"
               r"|Texture_Name|Width)$|Reflection|Refraction|Distance_Fade|Silhouette|Terrain_Texture|Wind_Disturbance"
               r"|Dynamic_Transform|Light_Effect|Lighting|Pre_Lit|Camera_Aligned|_Effect(_Small)?$|Effect_Z_Offset"
               r"|_Particle$|Particle_Attach|Bone_Name|Bone_Translation|Spawn_Bone|Tread_Scroll|Shield_\w*Color"
               r"|Shield_Sub_Mesh|Water_|Nebula_Effect|Earthquake_(Shake|Transition)|Death_Explosions_End|Explosion"
               r"|Model_Name|Blob|Telekinesis|Cinematic_Anim|Glory_Cinematics|Pause_During_Cinematic|In_Mid(fore)?ground"
               r"|Terrain_Resurface|Zoomed_Terrain|Land_Terrain_Model|Texture|Color|RGBA|Render|Flame_Emitter"
               r"|Should_Death_Clone_Play_Idle|Apply_\w_Turret_Rotate|Grid_Cull|SpaceFOWHeight|Camera_|Rotation_Base"
               r"|Start_Lightning_Frame"),
    ("movement", r"Blend|Crouch|Walk|Idle|Formation|Formup|^Move_Action\.|[Pp]ush|Obstacle|Locomotor|Facing|Reevaluation"
                 r"|CanCellStack|LateralAcceleration|Begin_Turn_Towards|Full_Stop|Snap_Movement|Gripper|Approximation"
                 r"|LandObjectTracking|LinearCollision|Animation|Needs_To_Rotate|Exit_Door|Max_Distance_From_Spawner"
                 r"|Attach_To_Flagship|Shoot_Anim"),
    ("ui", r"^(CommandBarComponent|MousePointer|RadarMapSettings|Movie)\.|^TradeRoute\.\w+_Position_LeftTop$"
           r"|^Campaign\.(Markup_Filename|Show_Completed_Tab|Is_Story_Campaign)$|Icon|Font|GUI_|Encyclopedia|Text|Tooltip"
           r"|Health_?[Bb]ar|Radar_|Highlight|Message|Name$|Name_Adjust|Show_|Coin_Stack|Queue_Tint|Splash|Overlay|Image"
           r"|Win_Lose|Countdown|Flash|Hilite|Darken|Cycle|Map_Preview|Include_In_UI|Describe_Wildlife|Build_Tab_"
           r"|Beacon|Planet_Ability|Zoom|Tint|Mouse_Over|Min_Accuracy_For_Icon|Min_Sight_Range|Briefing|Hologram"
           r"|Corruption_(Choice|Line)|Num_Structures_For|Battle_Load_Planet|Not_Really_Selectable|Waypoint|SetupPhase"
           r"|Secondary_Objective|Alternate_|GMC_|Animate_During|Display_Bink|Destroyed_Space_Tactical_Map"
           r"|Minimum_Visible_Base_Level|Use_Neutral_UI|Auto_Rotate_For_Space_Targeting|Tactical_Camera_Locked"
           r"|Hero_Ability$|Visible_To_Enemies_When_Empty|Hides_When_Built_On|Tactical_Build_Start_Lower_Z"
           r"|Use_Special_Submit_Rules|User_Bound|Easily_Bribed|Standalone_Space_Maps|Space_Retreat_Off_Map"
           r"|Health_Critical|Fade_|Transition_Time_In_Secs|Required_Corruption_Level|Apply_To_All_Allies"
           r"|Affects_All_Allies|Shield_Flash|TSW_|ReinforceFeedback|Reveal_Rate|Movie"),
    # Everything else: tactical rules (combat, abilities, actions, hard points, spawning).
    ("tactical", r"."),
]
GAP_GROUPS = [(name, re.compile(rx)) for name, rx in GAP_GROUPS]


def gap_group(cls, tag):
    name = f"{cls}.{tag}"
    return next(g for g, rx in GAP_GROUPS if rx.search(name))


def gaps(gen, group=None, counts=False):
    """The mcx4 exit test's open rows, from the committed ledger (tag_ledger.inc carries tier and base uses)."""
    if not os.path.isfile(gen.OUT_INC):
        print("tag ledger: tag_ledger.inc is missing")
        return 2
    row = re.compile(
        r'\{"([^"]*)", "([^"]*)", 0x[0-9a-f]+, LedgerState::(\w+), \d+, "[^"]*", "([^"]*)", (\d+), (\d+)\}')
    open_rows = []
    with open(gen.OUT_INC, encoding="utf-8") as f:
        for line in f:
            m = row.match(line)
            if m and m.group(3) in ("Unread", "Stored") and int(m.group(5)) > 0 and m.group(6) == "1":
                open_rows.append((-int(m.group(5)), m.group(1), m.group(2), m.group(3).lower(), m.group(4)))
    if group is not None:
        if group not in {g for g, _ in GAP_GROUPS}:
            print(f"tag ledger: no gap group '{group}' (groups: {', '.join(g for g, _ in GAP_GROUPS)})")
            return 2
        open_rows = [r for r in open_rows if gap_group(r[1], r[2]) == group]
    if counts:
        per = {g: [0, 0] for g, _ in GAP_GROUPS}  # a name listed twice (an exception above a pattern) counts once
        for uses, cls, tag, _, _ in open_rows:
            c = per[gap_group(cls, tag)]
            c[0] += 1
            c[1] -= uses
        for g, (n, u) in per.items():
            print(f"group {g}: {n} rows, {u} base uses")
    else:
        for uses, cls, tag, state, spec in sorted(open_rows):
            print(f"gap: {cls}.{tag} ({state}, base uses {-uses}, group {gap_group(cls, tag)}, spec {spec or '-'})")
    print(f"tag ledger: {len(open_rows)} base-FoC honour gaps open (tier 1, base_uses > 0, unread / stored)"
          + (f" in group {group}" if group else ""))
    return 1 if open_rows else 0


def check_bindings(gen):
    if not hasattr(gen, "render"):
        return 0
    if not os.path.isfile(gen.OUT):
        print("binding ledger: engine/lua/src/binding_table.inc is missing")
        return 2
    with open(gen.OUT, encoding="utf-8") as f:
        committed = f.read()
    text, problems = gen.render()
    for p in problems:
        print("binding ledger: annotation: " + p)
    if text != committed:
        old, new = committed.splitlines(), text.splitlines()
        diff = [f"- {a}\n  + {b}" for a, b in zip(old, new) if a != b][:20]
        for d in diff:
            print("binding ledger: stale:\n  " + d)
        print("binding ledger: regenerate with: python engine/lua/tools/gen_bindings.py")
        return 1
    if problems:
        return 1
    print("binding ledger: in step with the code")
    return 0


def selftest(gen):
    bad = 0
    lits, comments, code = gen.scan_source('x = get("Max_Speed"); // "Mass" in a comment\n/* "Tactical_Health" */ R"(\"Damage\")";\n'
                                           "// ledger: consumes GameObjectType.Mass\ny = c.mass;\n")
    names = [s for _, s in lits]
    if names != ["Max_Speed"]:
        print("selftest: literals", names)
        bad += 1
    if not any("ledger: consumes" in c for _, c in comments) or 4 not in code:
        print("selftest: comments / code lines", comments, sorted(code))
        bad += 1
    old = {("GameObjectType", "mass"): ("consumed", 1, "engine/x.cpp")}
    reg = gen.regressions(old, [{"class": "GameObjectType", "tag": "Mass", "state": "stored"}], set())
    if len(reg) != 1:
        print("selftest: regression not found", reg)
        bad += 1
    if gen.regressions(old, [{"class": "GameObjectType", "tag": "Mass", "state": "stored"}], {"gameobjecttype.mass"}):
        print("selftest: --allow-regress ignored")
        bad += 1
    # RETAIL_UNUSED (mcx4): a listed row nothing reads becomes retail_unused; a read one, an unknown row or a citation
    # that is no docs/re page naming the tag is an error.
    rows = [("GameConstants", "Progressive_Taxation", 0), ("GameConstants", "Income_Redistribution", 0)]
    page = "docs/re/GALACTIC_ECONOMY.md"
    st = gen.compute_states(rows, {}, {}, {("GameConstants", "progressive_taxation"): page})
    if st.get(("GameConstants", "progressive_taxation")) != "retail_unused" or \
            st.get(("GameConstants", "income_redistribution")) != "stored":
        print("selftest: retail_unused state", st)
        bad += 1
    if gen.retail_unused_problems(rows, {}, {("GameConstants", "Progressive_Taxation"): page + " Q2.2"}):
        print("selftest: a good RETAIL_UNUSED entry was refused")
        bad += 1
    probs = gen.retail_unused_problems(rows, {("GameConstants", "income_redistribution"): {"engine/x.cpp"}}, {
        ("GameConstants", "Income_Redistribution"): page,
        ("GameConstants", "No_Such_Tag"): page,
        ("GameConstants", "Progressive_Taxation"): "docs/re/NO_SUCH_PAGE.md",
    })
    if len(probs) != 3:
        print("selftest: RETAIL_UNUSED problems", probs)
        bad += 1
    # OUT_OF_SCOPE (mcx4 t16): a listed row nothing reads becomes out_of_scope; a read row, an unknown row, a row also
    # in RETAIL_UNUSED, a reason not in SCOPE_REASONS or one the record does not label is an error.
    record = "## " + gen.SCOPE_SECTION + " (Gabriel)\n- **multiplayer**: x\n## Next\n- **debug keys**: y\n"
    scope = {("GameConstants", "Income_Redistribution"): "multiplayer"}
    st = gen.compute_states(rows, {}, {}, {}, scope)
    if st.get(("GameConstants", "income_redistribution")) != "out_of_scope":
        print("selftest: out_of_scope state", st)
        bad += 1
    if gen.out_of_scope_problems(rows, {}, scope, {}, record):
        print("selftest: a good OUT_OF_SCOPE entry was refused")
        bad += 1
    probs = gen.out_of_scope_problems(rows, {("GameConstants", "income_redistribution"): {"engine/x.cpp"}}, {
        ("GameConstants", "Income_Redistribution"): "multiplayer",
        ("GameConstants", "No_Such_Tag"): "multiplayer",
        ("GameConstants", "Progressive_Taxation"): "debug keys",
    }, {("GameConstants", "Progressive_Taxation"): page}, record)
    if len(probs) != 4:  # read; no row; also retail-unused; "debug keys" labelled only outside the section
        print("selftest: OUT_OF_SCOPE problems", probs)
        bad += 1
    if len(gen.out_of_scope_problems(rows, {}, scope, {}, "no section\n")) != 1:
        print("selftest: a missing scope section was not reported")
        bad += 1
    for (cls, tag), want in {("Campaign", "Special_Case_Production"): "named", ("GameConstants", "Message_Of_The_Day_URL"):
                             "scope", ("Faction", "SFXEvent_Player_Taunt"): "scope", ("Decal", "Category"): "render",
                             ("GameObjectType", "SFXEvent_Attack_Hardpoint"): "audio", ("GameObjectType", "Is_Escort"):
                             "tactical", ("CommandBarComponent", "Stackable"): "ui",
                             ("GameObjectType", "Override_Bounty_MP"): "galactic", ("Campaign", "Is_Multiplayer"): "ui",
                             ("HardwareConfiguration", "CPUSpeed"): "scope"}.items():
        if gap_group(cls, tag) != want:
            print(f"selftest: {cls}.{tag} in gap group {gap_group(cls, tag)}, want {want}")
            bad += 1
    print("selftest:", "ok" if not bad else f"{bad} failed")
    return 1 if bad else 0


def main(argv):
    gen = load(GEN_TAGS, "gen_tag_ledger")
    if "--selftest" in argv:
        return selftest(gen)
    if "--gaps" in argv:
        group = None
        if "--group" in argv:
            k = argv.index("--group") + 1
            if k >= len(argv) or argv[k].startswith("--"):
                print("tag ledger: --group needs a group name (" + ", ".join(g for g, _ in GAP_GROUPS) + ")")
                return 2
            group = argv[k]
        return gaps(gen, group, "--counts" in argv)
    rc = check_tags(gen)
    rc_b = check_bindings(load(GEN_BINDINGS, "gen_bindings"))
    return max(rc, rc_b)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
