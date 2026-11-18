"""Tag ledger generator (docs/MOD_COMPAT_DESIGN.md §7.2, roadmap mcx0 t02).

One row per (class, tag) of retail's XML descriptor tables (docs/re/xml_field_tables.md, through the generated
engine/objects/src/field_schema_table.inc; class = the row's segment owner), with the port's state:

  unread    the port's loader for the class does not keep the tag, and no engine code reads it
  stored    the port keeps the value (a generic store: the typed object view, its sub-object fields, GameConstants,
            the campaign's raw list) but no engine code reads it
  consumed  engine code reads it: a string literal naming the tag in engine source outside the descriptor tables,
            tests, tools and the load report (every typed read goes through an accessor taking the tag name:
            GameObjectTypes::get_*(t, "Tag"), GameConstants::get*("Tag"), XmlNode::child*("Tag"), CampaignDef::raw_value),
            a name the code builds from such literals ("Corruption_" + n + "_Type": built_tag_reads),
            or an annotation "// ledger: consumes <Class>.<Tag>" next to an indirect read (a column filled from the
            tag, a struct member set by a loader)
  verified  consumed, and a test carries "// ledger: verifies <Class>.<Tag>" (a test that checks the tag's effect
            against a docs/re spec or a probe)
  retail_unused
            no engine code reads it, and an RE answer found no reader in retail either: the row is in RETAIL_UNUSED
            with the docs/re section that says so (roadmap mcx4). Not an honour gap; the lint counts it as settled.
            A RETAIL_UNUSED row that the code does read, or whose citation is not a docs/re page naming the tag, is
            an error.
  out_of_scope
            no engine code reads it, and the port leaves it out on purpose: the row is in OUT_OF_SCOPE with one of
            the scope reasons of docs/DECISIONS.md "Out-of-scope tag-ledger rows" (multiplayer, hardware detection,
            graphic detail levels, debug keys, localised splash / credits; roadmap mcx4 t16). Not an honour gap; shown
            like retail_unused. A row a single-player or skirmish game reads in retail is never out of scope.

A literal naming a tag that several classes list (72 of the 2,764 tag names) counts for the object type and sub-object
classes (the shared typed readers) and for the other classes the file names (literal_classes).

Other columns: code (parser type code), consumers (how many files read it, and the first), spec (the first docs/re
page that names the tag in backticks), base_uses (how often base FoC's XML in extracted/foc_data_text.zip writes an
element of that name, any class, outside XML comments), tier (1 for every row until a row says otherwise: TIER_OVERRIDES).
corpus_uses / corpus_mods are filled by the corpus runner (mcx7), not here.

Outputs (both generated, never edited by hand):
  engine/objects/generated/tag_ledger.inc   rows compiled into alamo_objects (objects/tag_ledger.hpp)
  docs/status/tag-ledger.md                 the human view

    python engine/objects/tools/gen_tag_ledger.py            write both
    python engine/objects/tools/gen_tag_ledger.py --allow-regress Class.Tag ...
                                                             write although a row moves consumed -> stored / unread /
                                                             retail_unused / out_of_scope
tools/lint/check_tag_ledger.py imports this module and checks that the committed ledger matches the code.
"""
import argparse
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
ENGINE = os.path.join(ROOT, "engine")
SCHEMA = os.path.join(ENGINE, "objects", "src", "field_schema_table.inc")
OUT_INC = os.path.join(ENGINE, "objects", "generated", "tag_ledger.inc")
OUT_MD = os.path.join(ROOT, "docs", "status", "tag-ledger.md")
DOCS_RE = os.path.join(ROOT, "docs", "re")
FOC_ZIP = os.path.join(ROOT, "extracted", "foc_data_text.zip")

STATES = ("unread", "stored", "consumed", "verified", "retail_unused", "out_of_scope")
STATE_ENUM = {"unread": "Unread", "stored": "Stored", "consumed": "Consumed", "verified": "Verified",
              "retail_unused": "RetailUnused", "out_of_scope": "OutOfScope"}
SETTLED_UNREAD = ("retail_unused", "out_of_scope")  # states of rows nothing reads that are not honour gaps

# Classes whose port loader keeps every tag of an entry, read or not (so an unread tag there is "stored"):
#   * the typed object view keeps every descriptor field of GameObjectType / SpaceProp / HeroUnit, and the raw store
#     keeps every element below a type, so the ability / action sub-object fields are kept too;
#   * data::GameConstants keeps every child of <GameConstants>; CampaignDef::raw every child of a campaign.
# Every other class has a loader that reads named tags only.
GENERIC_STORE = {"GameObjectType", "SpaceProp", "HeroUnit", "GameConstants", "Campaign", "Ability_Base"}
SUB_OBJECT_OWNER = re.compile(r"(_Ability|_Action)$|^(Unit_Ability|Hero_Clash|Destroy_Unit)$")

TIER_OVERRIDES = {}  # (class, tag) -> tier, for rows that are distribution-level only (tier 2)

# Rows retail parses but never reads (roadmap mcx4, Gabriel 2026-11-18): (class, tag) -> the docs/re section whose RE
# answer found no reader, as "docs/re/<FILE>.md <section>". The file must exist and name the tag. Class and tag are
# spelled as in docs/re/xml_field_tables.md (matched case-insensitively). Keep the list sorted by class, then tag.
RETAIL_UNUSED = {
    ("Faction", "Can_Win_By_Destroying_Super_Weapon"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q7.5 [H]",
    ("Faction", "Corruption_Tactical_Build_Pad_Time_Multipliers"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q7.2 [H]",
    ("Faction", "Credits_Accumulation_Factor"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q7.1 [M]",
    ("Faction", "Land_Surrender_SFXEvent"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.Land_Surrender_SFXEvent family [H]",
    ("Faction", "Music_Event_Strategic_Lose"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.Music_Event_Strategic_Win family [H]",
    ("Faction", "Music_Event_Strategic_Win"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.Music_Event_Strategic_Win family [H]",
    ("Faction", "Reinforcements_Requesting_SFXEvent"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.Reinforcements_Selection_SFXEvent family [H]",
    ("Faction", "SFXEvent_Build_Impossible_Location_Blockaded"): "docs/re/P4_SIM_SYSTEMS.md §2.7.3 [M: byte scan]",
    ("Faction", "SFXEvent_HUD_Base_Shield_Penetrated"): "docs/re/TRU1_A_RE_ANSWERS.md Penetration SFX [M: byte scan]",
    ("Faction", "SFXEvent_HUD_Build_Pad_Captured"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Landing_Zone_Captured family [H]",
    ("Faction", "SFXEvent_HUD_Build_Pad_Lost"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Landing_Zone_Captured family [H]",
    ("Faction", "SFXEvent_HUD_Enemy_Base_Shield_Penetrated"): "docs/re/TRU1_A_RE_ANSWERS.md Penetration SFX [M: byte scan]",
    ("Faction", "SFXEvent_HUD_Enemy_Special_Weapon_Charging"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Special_Weapon_Ready family [H]",
    ("Faction", "SFXEvent_HUD_Landing_Zone_Captured"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Landing_Zone_Captured family [H]",
    ("Faction", "SFXEvent_HUD_Landing_Zone_Lost"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Landing_Zone_Captured family [H]",
    ("Faction", "SFXEvent_HUD_Last_Landing_Zone_Lost"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Landing_Zone_Captured family [H]",
    ("Faction", "SFXEvent_HUD_Special_Weapon_Charging"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SFXEvent_HUD_Special_Weapon_Ready family [H]",
    ("Faction", "SFXEvent_Max_Credits_Limit_Reached"): "docs/re/P4_SIM_SYSTEMS.md §2.7.3 [M: byte scan]",
    ("Faction", "Space_Retreat_Off_Map_Dest_Pos"): "docs/re/TRU1_C_RE.md §2.1 [H]",
    ("Faction", "Space_Surrender_SFXEvent"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.Land_Surrender_SFXEvent family [H]",
    ("Faction", "SpeechEvent_Super_Weapon_Enemy_Moving_Into_Range"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds family [H]",
    ("Faction", "SpeechEvent_Super_Weapon_Moving_Into_Range"): "docs/re/MCX4_T13_RE_ANSWERS.md Faction.SpeechEvent_Super_Weapon_Moving_Range_60_Seconds family [H]",
    ("GameConstants", "AutomaticAutoResolve"): "docs/re/MCX4_T09_RE_ANSWERS.md Q3 AutomaticAutoResolve [H]",
    ("GameConstants", "Camera_Stop_Left"): "docs/re/STRATEGIC_GAPS.md §7.1 [H]",
    ("GameConstants", "Camera_Stop_Right"): "docs/re/STRATEGIC_GAPS.md §7.1 [H]",
    ("GameConstants", "Camera_Z_Position"): "docs/re/STRATEGIC_GAPS.md §7.1 [H]",
    ("GameConstants", "First_Strike_Extra_Damage_Percent"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.4 [H]",
    ("GameConstants", "First_Strike_Particle"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.4 [H]",
    ("GameConstants", "Fleet_Maintenance_Update_Delay_Seconds"): "docs/re/STRATEGIC_GAPS.md §2.3 [H]",
    ("GameConstants", "Galactic_Zoom_Acceleration"): "docs/re/P1_CAMERA_INPUT.md §0 item 6 [M: no reader]",
    ("GameConstants", "Health_Bar_Scale"): "docs/re/STRATEGIC_GAPS.md §7.2 [H]",
    ("GameConstants", "Income_Redistribution"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.2 [H]",
    ("GameConstants", "Land_Health_Bar_Scale"): "docs/re/STRATEGIC_GAPS.md §7.2 [H]",
    ("GameConstants", "Max_Galactic_Zoom_Distance"): "docs/re/P1_CAMERA_INPUT.md §0 item 6 [M: no reader]",
    ("GameConstants", "Max_Galactic_Zoom_Speed"): "docs/re/P1_CAMERA_INPUT.md §0 item 6 [M: no reader]",
    ("GameConstants", "MaxCombatAccuracyAlignmentBonus"): "docs/re/MCX4_T10_RE_ANSWERS.md §16 [H]",
    ("GameConstants", "MaxCombatDamageAlignmentBonus"): "docs/re/MCX4_T10_RE_ANSWERS.md §16 [H]",
    ("GameConstants", "MaxCombatSensorRangeAlignmentBonus"): "docs/re/MCX4_T10_RE_ANSWERS.md §16 [H]",
    ("GameConstants", "MaximumSpecialStructuresLand"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.1 [H]",
    ("GameConstants", "MaximumSpecialStructuresSpace"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.1 [H]",
    ("GameConstants", "Min_Galactic_Zoom_Speed"): "docs/re/P1_CAMERA_INPUT.md §0 item 6 [M: no reader]",
    ("GameConstants", "Min_Health_Bar_Scale"): "docs/re/STRATEGIC_GAPS.md §7.2 [H]",
    ("GameConstants", "Progressive_Taxation"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.2 [H]",
    ("GameConstants", "RepushDistance"): "docs/re/MOVEMENT.md §5.4 [H]",
    ("GameConstants", "Skirmish_Buy_Credits"): "docs/re/MCX4_RE_ANSWERS_GALACTIC.md Q2.3 [H: read only in a pre-game branch single player never reaches]",
    ("GameObjectType", "Air_Vehicle_Turret_Target"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "Destroyed_Space_Tactical_Map"): "docs/re/TACTICAL_GAPS.md §4.10 [M: ctor / dtor / copy only]",
    ("GameObjectType", "Disallows_Hyperspace_Retreat"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §11 [H]",
    ("GameObjectType", "Ground_Infantry_Turret_Target"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "Ground_Vehicle_Turret_Target"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "MinimumPushReturnDistance"): "docs/re/MOVEMENT.md §5.4 [H]",
    ("GameObjectType", "Projectile_Acceleration_Per_Frame"): "docs/re/MCX4_T10_RE_ANSWERS.md §8 [H]",
    ("GameObjectType", "Required_Timeline"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "SFXEvent_Bombard_Select_Target"): "docs/re/MCX4_T13_RE_ANSWERS.md GameObjectType.SFXEvent_Bombard_Incoming family [H]",
    ("GameObjectType", "Size_Value"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "SpaceAutoResolveHitRate"): "docs/re/MCX4_T09_RE_ANSWERS.md Q4 [M: written, never read]",
    ("GameObjectType", "SpaceAutoResolveStunRate"): "docs/re/MCX4_T09_RE_ANSWERS.md Q4 [M: written, never read]",
    ("GameObjectType", "Turret_Targets_Air_Vehicles"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "Turret_Targets_Anything_Else"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "Turret_Targets_Ground_Infantry"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("GameObjectType", "Turret_Targets_Ground_Vehicles"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §8 [H]",
    ("Spawn_Ability", "Self_Destruct_Radius"): "docs/re/MCX4_RE_ANSWERS_TACTICAL.md §14 [H: only the load check reads it]",
}

# Rows the port leaves out on purpose (roadmap mcx4 t16, Gabriel 2026-11-18): (class, tag) -> scope reason. Each reason
# is a bold label in SCOPE_RECORD's SCOPE_SECTION, which ties it to the scope decision it follows from. Retail runs
# skirmish as a multiplayer game type, so an "_MP" row that a skirmish reads (bounties, extra players, the setup
# defaults) is not out of scope; neither is a row single player reads (Campaign.Is_Multiplayer filters the campaign
# lists). Keep the list sorted by class, then tag.
SCOPE_RECORD = "docs/DECISIONS.md"
SCOPE_SECTION = "Out-of-scope tag-ledger rows"
SCOPE_REASONS = ("multiplayer", "hardware detection", "graphic detail levels", "debug keys", "localised splash / credits")
_MP, _HW, _GFX, _DBG, _LOC = SCOPE_REASONS
OUT_OF_SCOPE = {
    ("Campaign", "Is_Quickmatch_Allowed"): _MP,
    ("Faction", "Debug_Ground_Structures"): _DBG,
    ("Faction", "Multiplayer_Beacon_Type"): _MP,
    ("Faction", "Multiplayer_Campaign_Heroes"): _MP,
    ("Faction", "Multiplayer_Map_Preview_Icon"): _MP,
    ("Faction", "SFXEvent_Player_Taunt"): _MP,
    ("GameConstants", "AutoResolveVoteDefaultTimeOut"): _MP,
    ("GameConstants", "AutoResolveVoteDefaultToTactical"): _MP,
    ("GameConstants", "Credits_Logo_1_Height"): _LOC,
    ("GameConstants", "Credits_Logo_1_Name"): _LOC,
    ("GameConstants", "Credits_Logo_1_Width"): _LOC,
    ("GameConstants", "Credits_Logo_1_Y_Offset"): _LOC,
    ("GameConstants", "Credits_Logo_2_Height"): _LOC,
    ("GameConstants", "Credits_Logo_2_Name"): _LOC,
    ("GameConstants", "Credits_Logo_2_Width"): _LOC,
    ("GameConstants", "Credits_Logo_2_Y_Offset"): _LOC,
    ("GameConstants", "Credits_Logo_3_Height"): _LOC,
    ("GameConstants", "Credits_Logo_3_Name"): _LOC,
    ("GameConstants", "Credits_Logo_3_Width"): _LOC,
    ("GameConstants", "Credits_Logo_3_Y_Offset"): _LOC,
    ("GameConstants", "Debug_Hot_Key_Load_Campaign"): _DBG,
    ("GameConstants", "Debug_Hot_Key_Load_Map"): _DBG,
    ("GameConstants", "Debug_Hot_Key_Load_Map_Script"): _DBG,
    ("GameConstants", "Distribute_Credit_Quantum"): _MP,
    ("GameConstants", "Japanese_ST_Line_Percent"): _LOC,
    ("GameConstants", "Localized_Menu_Overlay"): _LOC,
    ("GameConstants", "Localized_Splash_Screen"): _LOC,
    ("GameConstants", "Localized_UK_English_Splash_Screen"): _LOC,
    ("GameConstants", "Message_Of_The_Day_URL"): _MP,
    ("GameConstants", "Quickmatch_Map_Exclusion_List"): _MP,
    ("GameConstants", "ShouldDisplayPotentialPath"): _DBG,
    ("GameConstants", "ShouldDisplayPredictionPaths"): _DBG,
    ("GameConstants", "ShouldDisplaySyncedPaths"): _DBG,
    ("GameConstants", "ShowUnitAIPlanAttachment"): _DBG,
    ("GameConstants", "SyncedFrameInterval"): _MP,
    ("GameObjectType", "SFXEvent_Debug_Test_Loop"): _DBG,
    ("GraphicDetailLevel", "DynamicLighting"): _GFX,
    ("GraphicDetailLevel", "EnvironmentDetail"): _GFX,
    ("GraphicDetailLevel", "HeatDistortions"): _GFX,
    ("GraphicDetailLevel", "MeshDetail"): _GFX,
    ("GraphicDetailLevel", "ParticleDetail"): _GFX,
    ("GraphicDetailLevel", "ScreenAALevel"): _GFX,
    ("GraphicDetailLevel", "ScreenResolutionHeight"): _GFX,
    ("GraphicDetailLevel", "ScreenResolutionWidth"): _GFX,
    ("GraphicDetailLevel", "ShaderDetailLevel"): _GFX,
    ("GraphicDetailLevel", "ShadowDetail"): _GFX,
    ("GraphicDetailLevel", "ShadowVolumes"): _GFX,
    ("GraphicDetailLevel", "SoftShadows"): _GFX,
    ("GraphicDetailLevel", "TextureMipLevel"): _GFX,
    ("GraphicDetailLevel", "WaterDetailLevel"): _GFX,
    ("HardwareConfiguration", "CPUSpeed"): _HW,
    ("HardwareConfiguration", "DefaultGraphicDetailLevel"): _HW,
    ("HardwareConfiguration", "DeviceIDListHEX"): _HW,
    ("HardwareConfiguration", "FillRateMpsPS13"): _HW,
    ("HardwareConfiguration", "FillRateMpsPS14"): _HW,
    ("HardwareConfiguration", "FillRateMpsPS20"): _HW,
    ("HardwareConfiguration", "GraphicDetailSettingName"): _HW,
    ("HardwareConfiguration", "PixelShaderVersionHEX"): _HW,
    ("HardwareConfiguration", "TextureMemory"): _HW,
    ("HardwareConfiguration", "VendorIDHEX"): _HW,
    ("HardwareConfiguration", "VertexRateMvsVS11"): _HW,
    ("HardwareConfiguration", "VertexShaderVersionHEX"): _HW,
}

# Source files that name tags without reading them for the game: the descriptor tables themselves, reports, tests,
# tools, generated tables, vendored code.
EXCLUDED_PARTS = ("/tests/", "/tools/", "/third_party/", "/generated/", "/test_support/")
EXCLUDED_FILES = {
    "engine/objects/src/field_schema_table.inc",
    "engine/objects/src/sub_object_names.inc",
    "engine/objects/src/behavior_classes.inc",
    "engine/objects/src/coverage.cpp",
    "engine/lua/src/binding_table.inc",
    "engine/apps/game/src/mod_check.cpp",
}
SOURCE_EXT = (".cpp", ".hpp", ".h", ".inc")
ANNOTATION = re.compile(r"ledger:\s*(consumes|verifies)\s+([A-Za-z0-9_]+)\.([A-Za-z0-9_\-\.]+)")
ROW = re.compile(r'\{"([^"]+)", (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+), "([^"]*)", (\d+)')


def rel(path):
    return os.path.relpath(path, ROOT).replace("\\", "/")


def schema_rows():
    """[(class, tag, code)] in document order, first row of each (class, tag lower)."""
    out, seen = [], set()
    with open(SCHEMA, encoding="utf-8") as f:
        for line in f:
            m = ROW.match(line)
            if not m:
                continue
            tag, code, owner = m.group(1), int(m.group(2), 16), m.group(4)
            key = (owner, tag.lower())
            if key in seen:
                continue
            seen.add(key)
            out.append((owner, tag, code))
    return out


def scan_source(text, concat=None):
    """(literals, comments, code_lines): string literals (not raw strings), comment texts with their line, and the
    set of line numbers that hold code outside comments. `concat` (optional list) gets (literal, after '+', before '+')
    for every literal, for tag names the code builds by concatenation (built_tag_reads)."""
    if concat is None:
        concat = []
    lits, comments, code_lines = [], [], set()
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append((line, text[i:j]))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            comments.append((line, text[i:j]))
            line += text.count("\n", i, j)
            i = j
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")
                                                        or text[max(0, i - 2):i] in ("u8", "8") or text[i - 1] in "uUL"):
            k = text.find("(", i + 2)
            delim = text[i + 2:k] if k >= 0 else ""
            end = text.find(")" + delim + '"', k + 1) if k >= 0 else -1
            j = n if end < 0 else end + len(delim) + 2
            code_lines.add(line)
            line += text.count("\n", i, j)
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            if c == '"':
                lits.append((line, text[i + 1:j]))
                k = i - 1
                while k >= 0 and text[k] in " \t\r\n":
                    k -= 1
                m = j + 1
                while m < n and text[m] in " \t\r\n":
                    m += 1
                concat.append((text[i + 1:j], k >= 0 and text[k] == "+", m < n and text[m] == "+"))
            code_lines.add(line)
            i = j + 1
        else:
            if not c.isspace():
                code_lines.add(line)
            i += 1
    return lits, comments, code_lines


def built_tag_reads(concat, by_tag):
    """Tags (lower case) a file reads under a name it builds: "Corruption_" + n + "_Type", "Starting_Spawned_Units_Tech_"
    + n, faction + "_Victory_Conditions". A tag counts when it starts with a literal followed by '+' (4+ characters) and
    the rest is digits or ends with a literal of the file preceded by '+'; or ends with a literal preceded by '+' that
    starts with '_' and has 11+ characters; or is exactly a literal of the file followed by such a suffix literal
    (read("Tractor_Beam") ... p + "_Texture")."""
    prefixes = {s.lower() for s, _, after in concat if after and len(s) >= 4}
    suffixes = {s.lower() for s, before, _ in concat if before and len(s) >= 3}
    long_suffixes = {x for x in suffixes if x.startswith("_") and len(x) >= 11}
    if not prefixes and not suffixes:
        return set()
    out = set()
    words = {s.lower() for s, _, _ in concat if len(s) >= 4}
    for w in words:
        for x in suffixes:
            if x.startswith("_") and w + x in by_tag:
                out.add(w + x)
    for tag in by_tag:
        for p in prefixes:
            if tag.startswith(p) and len(tag) > len(p):
                rest = tag[len(p):]
                if rest.isdigit() or any(tag.endswith(x) and len(tag) >= len(p) + len(x) for x in suffixes):
                    out.add(tag)
                    break
        else:
            if any(tag.endswith(x) and len(tag) > len(x) for x in long_suffixes):
                out.add(tag)
    return out


def engine_sources():
    for dirpath, dirnames, filenames in os.walk(ENGINE):
        dirnames.sort()
        for name in sorted(filenames):
            if name.endswith(SOURCE_EXT):
                yield os.path.join(dirpath, name)


def shared_reader_class(cls):
    """The classes the shared typed readers serve: the object type view and its sub-objects (abilities, actions)."""
    return cls in GENERIC_STORE or bool(SUB_OBJECT_OWNER.search(cls))


def literal_classes(classes, text):
    """The classes a literal naming a tag counts for. One class lists the tag: that class. Several do (Type, Radius,
    Damage_Amount, ...): the shared-reader classes, plus the others the file names, so a "Type" in a perception file
    does not make CommandBarComponent.Type consumed. A standalone class read under a shared name without its class name
    in the file needs a "// ledger: consumes" note."""
    if len(classes) <= 1:
        return classes
    # The class name as a word in any case ("Faction", "factions", "faction_"), not inside a longer name.
    named = [c for c in classes
             if re.search(r"(?<![A-Za-z0-9])" + re.escape(c) + r"s?(?![A-Za-z0-9])", text, re.IGNORECASE)]
    return [c for c in classes if c in named or shared_reader_class(c)]


def find_consumers(rows, exact_only=frozenset()):
    """Returns (consumers, verifiers, problems): consumers[(class, tag lower)] = set of files; verifiers likewise from
    test annotations; problems = annotation errors (unknown row, no code next to it). Rows in `exact_only` (the
    RETAIL_UNUSED keys) count only literals and annotations, not names guessed from concatenation (built_tag_reads):
    an RE answer outweighs that heuristic ("Music_Event_" + ... + "_Super_Weapon" is no read of
    Can_Win_By_Destroying_Super_Weapon)."""
    by_tag = {}
    for cls, tag, _ in rows:
        by_tag.setdefault(tag.lower(), []).append(cls)
    known = {(cls, tag.lower()) for cls, tag, _ in rows}
    consumers, verifiers, problems = {}, {}, []
    for path in engine_sources():
        r = rel(path)
        is_test = "/tests/" in r
        excluded = r in EXCLUDED_FILES or any(p in r for p in EXCLUDED_PARTS)
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        if "ledger:" not in text and excluded:
            continue
        concat = []
        lits, comments, code_lines = scan_source(text, concat)
        if not excluded:
            for _, s in lits:
                for cls in literal_classes(by_tag.get(s.lower(), ()), text):
                    consumers.setdefault((cls, s.lower()), set()).add(r)
            for tag in built_tag_reads(concat, by_tag):
                for cls in literal_classes(by_tag[tag], text):
                    if (cls, tag) not in exact_only:
                        consumers.setdefault((cls, tag), set()).add(r)
        for ln, body in comments:
            for m in ANNOTATION.finditer(body):
                kind, cls, tag = m.group(1), m.group(2), m.group(3)
                key = (cls, tag.lower())
                where = f"{r}:{ln}"
                if key not in known:
                    problems.append(f"{where}: '{m.group(0)}' names no ledger row ({cls}.{tag})")
                    continue
                if kind == "consumes":
                    if excluded:
                        problems.append(f"{where}: a consumes annotation in a file the scan skips (tests, tools, tables)")
                        continue
                    if not any(l in code_lines for l in (ln, ln + 1, ln + 2)):
                        problems.append(f"{where}: no code on the annotation's line or the two after it")
                        continue
                    consumers.setdefault(key, set()).add(r)
                else:
                    if not is_test:
                        problems.append(f"{where}: a verifies annotation outside a tests/ folder")
                        continue
                    if tag.lower() not in text.lower().replace(m.group(0).lower(), ""):
                        problems.append(f"{where}: the test names {tag} only in the annotation")
                        continue
                    verifiers.setdefault(key, set()).add(r)
    return consumers, verifiers, problems


def retail_unused_keys(retail_unused=None):
    """{(class, tag lower): citation} of RETAIL_UNUSED (or the given table)."""
    table = RETAIL_UNUSED if retail_unused is None else retail_unused
    return {(cls, tag.lower()): cite for (cls, tag), cite in table.items()}


def retail_unused_problems(rows, consumers, retail_unused=None):
    """Errors in the RETAIL_UNUSED table (or the given one): no such row, the code reads the row, or the citation is
    not an existing docs/re page that names the tag."""
    table = RETAIL_UNUSED if retail_unused is None else retail_unused
    known = {(cls, tag.lower()) for cls, tag, _ in rows}
    problems = []
    for (cls, tag), cite in sorted(table.items()):
        key = (cls, tag.lower())
        name = f"RETAIL_UNUSED {cls}.{tag}"
        if key not in known:
            problems.append(f"{name}: names no ledger row")
            continue
        if key in consumers:
            problems.append(f"{name}: engine code reads it ({sorted(consumers[key])[0]}); drop the entry or the reader")
        page = cite.split()[0] if cite.split() else ""
        path = os.path.join(ROOT, *page.split("/"))
        if not page.startswith("docs/re/") or not page.endswith(".md") or not os.path.isfile(path):
            problems.append(f"{name}: citation '{cite}' is not an existing docs/re/*.md page")
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            if tag.lower() not in f.read().lower():
                problems.append(f"{name}: {page} does not name {tag}")
    return problems


def scope_citation(reason):
    """The spec column of an out_of_scope row: the record and the reason's label in it."""
    return f'{SCOPE_RECORD} "{SCOPE_SECTION}": {reason}'


def out_of_scope_keys(out_of_scope=None):
    """{(class, tag lower): citation} of OUT_OF_SCOPE (or the given table)."""
    table = OUT_OF_SCOPE if out_of_scope is None else out_of_scope
    return {(cls, tag.lower()): scope_citation(reason) for (cls, tag), reason in table.items()}


def out_of_scope_problems(rows, consumers, out_of_scope=None, retail_unused=None, record_text=None):
    """Errors in the OUT_OF_SCOPE table (or the given one): no such row, the code reads the row, the row is also in
    RETAIL_UNUSED, or its reason is not one of SCOPE_REASONS with a bold label in SCOPE_RECORD's SCOPE_SECTION."""
    table = OUT_OF_SCOPE if out_of_scope is None else out_of_scope
    unused = retail_unused_keys(retail_unused)
    known = {(cls, tag.lower()) for cls, tag, _ in rows}
    if record_text is None:
        path = os.path.join(ROOT, *SCOPE_RECORD.split("/"))
        record_text = ""
        if os.path.isfile(path):
            with open(path, encoding="utf-8", errors="replace") as f:
                record_text = f.read()
    section = ""
    m = re.search(r"^## " + re.escape(SCOPE_SECTION) + r".*?$(.*?)(?=^## |\Z)", record_text, re.MULTILINE | re.DOTALL)
    if m:
        section = m.group(1)
    problems = []
    if table and not section:
        problems.append(f'OUT_OF_SCOPE: {SCOPE_RECORD} has no "## {SCOPE_SECTION}" section')
    for (cls, tag), reason in sorted(table.items()):
        key = (cls, tag.lower())
        name = f"OUT_OF_SCOPE {cls}.{tag}"
        if key not in known:
            problems.append(f"{name}: names no ledger row")
            continue
        if key in consumers:
            problems.append(f"{name}: engine code reads it ({sorted(consumers[key])[0]}); drop the entry or the reader")
        if key in unused:
            problems.append(f"{name}: also in RETAIL_UNUSED; keep one")
        if reason not in SCOPE_REASONS:
            problems.append(f"{name}: reason '{reason}' is not one of {', '.join(SCOPE_REASONS)}")
        elif section and f"**{reason}**" not in section:
            problems.append(f'{name}: {SCOPE_RECORD} "{SCOPE_SECTION}" has no **{reason}** label')
    return problems


def compute_states(rows, consumers, verifiers, retail_unused=None, out_of_scope=None):
    unused = retail_unused_keys(retail_unused)
    scope = out_of_scope_keys(out_of_scope)
    out = {}
    for cls, tag, _ in rows:
        key = (cls, tag.lower())
        if key in consumers:
            st = "verified" if key in verifiers else "consumed"
        elif key in unused:
            st = "retail_unused"
        elif key in scope:
            st = "out_of_scope"
        elif cls in GENERIC_STORE or SUB_OBJECT_OWNER.search(cls):
            st = "stored"
        else:
            st = "unread"
        out[key] = st
    return out


def spec_index():
    idx = {}
    if not os.path.isdir(DOCS_RE):
        return idx
    tick = re.compile(r"`([A-Za-z][A-Za-z0-9_]*)`")
    for name in sorted(os.listdir(DOCS_RE)):
        # Question batches (*_RE_QUESTIONS.md) name tags without settling them: not a spec.
        if not name.endswith(".md") or name == "xml_field_tables.md" or name.endswith("_RE_QUESTIONS.md"):
            continue
        with open(os.path.join(DOCS_RE, name), encoding="utf-8", errors="replace") as f:
            for m in tick.finditer(f.read()):
                idx.setdefault(m.group(1).lower(), "docs/re/" + name)
    return idx


def base_uses():
    counts = {}
    if not os.path.isfile(FOC_ZIP):
        return counts
    el = re.compile(rb"<([A-Za-z_][A-Za-z0-9_]*)[\s>/]")
    comment = re.compile(rb"<!--.*?-->", re.DOTALL)
    with zipfile.ZipFile(FOC_ZIP) as z:
        for name in sorted(z.namelist()):
            if not name.lower().endswith(".xml"):
                continue
            # An element inside an XML comment is not a use (mcx4: RadarMap.xml's commented *_Position_LeftTop).
            for m in el.finditer(comment.sub(b"", z.read(name))):
                k = m.group(1).decode("ascii").lower()
                counts[k] = counts.get(k, 0) + 1
    return counts


def build(with_extras=True):
    rows = schema_rows()
    consumers, verifiers, problems = find_consumers(rows, frozenset(retail_unused_keys()) | frozenset(out_of_scope_keys()))
    problems += retail_unused_problems(rows, consumers)
    problems += out_of_scope_problems(rows, consumers)
    states = compute_states(rows, consumers, verifiers)
    unused = {**retail_unused_keys(), **out_of_scope_keys()}
    spec = spec_index() if with_extras else {}
    uses = base_uses() if with_extras else {}
    ledger = []
    for cls, tag, code in rows:
        key = (cls, tag.lower())
        files = sorted(consumers.get(key, ()))
        ledger.append({
            "class": cls, "tag": tag, "code": code, "state": states[key], "consumers": len(files),
            "consumer": files[0] if files else "",
            "spec": unused[key] if states[key] in SETTLED_UNREAD else spec.get(tag.lower(), ""),
            "base_uses": uses.get(tag.lower(), 0), "tier": TIER_OVERRIDES.get(key, 1),
        })
    return ledger, problems


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_inc(ledger):
    lines = [
        "// GENERATED by engine/objects/tools/gen_tag_ledger.py from docs/re/xml_field_tables.md and the engine source.",
        "// Do not edit; tools/lint/check_tag_ledger.py checks it. Row: {class, tag, code, state, consumer files, first",
        f"// consumer, spec, base FoC uses, tier}}. {len(ledger)} rows.",
    ]
    for r in ledger:
        lines.append("{%s, %s, 0x%x, LedgerState::%s, %d, %s, %s, %d, %d}," % (
            c_str(r["class"]), c_str(r["tag"]), r["code"], STATE_ENUM[r["state"]], r["consumers"], c_str(r["consumer"]),
            c_str(r["spec"]), r["base_uses"], r["tier"]))
    return "\n".join(lines) + "\n"


def parse_inc(text):
    """{(class, tag lower): (state, consumers, consumer)} of a written tag_ledger.inc."""
    row = re.compile(r'\{"([^"]*)", "([^"]*)", 0x[0-9a-f]+, LedgerState::(\w+), (\d+), "([^"]*)"')
    inv = {v: k for k, v in STATE_ENUM.items()}
    out = {}
    for line in text.splitlines():
        m = row.match(line)
        if m:
            out[(m.group(1), m.group(2).lower())] = (inv.get(m.group(3), m.group(3)), int(m.group(4)), m.group(5))
    return out


def render_md(ledger):
    classes = {}
    for r in ledger:
        c = classes.setdefault(r["class"], {s: 0 for s in STATES})
        c[r["state"]] += 1
    tot = {s: sum(c[s] for c in classes.values()) for s in STATES}
    out = [
        "# Tag ledger",
        "",
        "Generated by `engine/objects/tools/gen_tag_ledger.py` (roadmap mcx0 t02); do not edit. Design:",
        "`docs/MOD_COMPAT_DESIGN.md` §7.2. One row per (class, tag) of retail's descriptor tables",
        "(`docs/re/xml_field_tables.md`; class = segment owner). The compiled copy is",
        "`engine/objects/generated/tag_ledger.inc`; `tools/lint/check_tag_ledger.py` keeps both in step with the code.",
        "",
        "States: **unread** (the port does not keep it), **stored** (kept, nothing reads it), **consumed** (engine code",
        "reads it), **verified** (consumed, and a test checks its effect), **retail-unused** (retail never reads it",
        "either, by an RE answer cited below), **out-of-scope** (the port leaves it out on purpose, by a scope reason",
        "of `docs/DECISIONS.md` \"Out-of-scope tag-ledger rows\"). An `unread` or `stored` tag that a mod sets is an",
        "honour gap in `alamo_mod_check` report v4. Work order (§7.2): tier 1 with the most corpus mods, then base FoC uses.",
        "",
        f"Totals: {len(ledger)} rows; unread {tot['unread']}, stored {tot['stored']}, consumed {tot['consumed']}, "
        f"verified {tot['verified']}, retail-unused {tot['retail_unused']}, out-of-scope {tot['out_of_scope']}.",
        "",
        "## Per class",
        "",
        "| Class | Rows | Unread | Stored | Consumed | Verified | Retail-unused | Out-of-scope |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for cls in sorted(classes, key=lambda c: (-sum(classes[c].values()), c)):
        c = classes[cls]
        out.append(f"| {cls} | {sum(c.values())} | {c['unread']} | {c['stored']} | {c['consumed']} | {c['verified']} "
                   f"| {c['retail_unused']} | {c['out_of_scope']} |")
    gaps = [r for r in ledger if r["state"] in ("unread", "stored") and r["base_uses"] > 0]
    gaps.sort(key=lambda r: (-r["base_uses"], r["class"], r["tag"].lower()))
    out += [
        "",
        "## Honour gaps base FoC uses",
        "",
        "Rows not consumed whose tag base FoC's XML writes (base uses count the element name in any class). These are",
        "base-FoC parity gaps as well as mod gaps (`mcx4`).",
        "",
        "| Class | Tag | Code | State | Base uses | Spec |",
        "|---|---|---|---|---:|---|",
    ]
    for r in gaps:
        out.append(f"| {r['class']} | {r['tag']} | 0x{r['code']:x} | {r['state']} | {r['base_uses']} | {r['spec'] or '-'} |")
    unused_rows = sorted((r for r in ledger if r["state"] == "retail_unused"), key=lambda r: (r["class"], r["tag"].lower()))
    out += [
        "",
        "## Retail-unused rows",
        "",
        "Rows retail parses but never reads, by an RE answer (`RETAIL_UNUSED` in the generator, roadmap mcx4). The port",
        "ignores them too.",
        "",
    ]
    if unused_rows:
        out += ["| Class | Tag | Base uses | RE answer |", "|---|---|---:|---|"]
        out += [f"| {r['class']} | {r['tag']} | {r['base_uses']} | {r['spec']} |" for r in unused_rows]
    else:
        out.append("None yet.")
    scope_rows = sorted((r for r in ledger if r["state"] == "out_of_scope"), key=lambda r: (r["class"], r["tag"].lower()))
    out += [
        "",
        "## Out-of-scope rows",
        "",
        "Rows the port leaves out on purpose (`OUT_OF_SCOPE` in the generator, roadmap mcx4 t16): each cites its reason in",
        "`docs/DECISIONS.md` \"Out-of-scope tag-ledger rows\". Not honour gaps.",
        "",
    ]
    if scope_rows:
        out += ["| Class | Tag | Base uses | Reason |", "|---|---|---:|---|"]
        out += [f"| {r['class']} | {r['tag']} | {r['base_uses']} | {r['spec'].rsplit(': ', 1)[-1]} |" for r in scope_rows]
    else:
        out.append("None yet.")
    out += ["", "## Every row", "", "Per class: `Tag` (state, consumer files; x = retail-unused, o = out-of-scope).", ""]
    for cls in sorted(classes):
        items = [f"`{r['tag']}` ({ {'retail_unused': 'x', 'out_of_scope': 'o'}.get(r['state'], r['state'][0])}"
                 f"{'' if not r['consumers'] else ' ' + str(r['consumers'])})"
                 for r in ledger if r["class"] == cls]
        out.append(f"- **{cls}**: " + ", ".join(items))
    return "\n".join(out) + "\n"


def regressions(old, ledger, allowed):
    bad = []
    for r in ledger:
        key = (r["class"], r["tag"].lower())
        prev = old.get(key)
        if prev and prev[0] in ("consumed", "verified") and r["state"] in ("stored", "unread") + SETTLED_UNREAD:
            if f"{r['class']}.{r['tag']}".lower() not in allowed:
                bad.append(f"{r['class']}.{r['tag']}: {prev[0]} -> {r['state']} (was read in {prev[2]})")
    return bad


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--allow-regress", nargs="*", default=[], metavar="CLASS.TAG",
                    help="rows allowed to move from consumed / verified to stored / unread")
    a = ap.parse_args(argv)
    ledger, problems = build()
    for p in problems:
        print("annotation:", p)
    old = {}
    if os.path.isfile(OUT_INC):
        with open(OUT_INC, encoding="utf-8") as f:
            old = parse_inc(f.read())
    bad = regressions(old, ledger, {x.lower() for x in a.allow_regress})
    if bad:
        print("refused: these rows lose their consumer (pass --allow-regress Class.Tag if that is intended):")
        for b in bad:
            print("  " + b)
        return 1
    if problems:
        return 1
    os.makedirs(os.path.dirname(OUT_INC), exist_ok=True)
    with open(OUT_INC, "w", encoding="utf-8", newline="\n") as f:
        f.write(render_inc(ledger))
    with open(OUT_MD, "w", encoding="utf-8", newline="\n") as f:
        f.write(render_md(ledger))
    counts = {s: sum(1 for r in ledger if r["state"] == s) for s in STATES}
    print(f"wrote {rel(OUT_INC)} and {rel(OUT_MD)}: {len(ledger)} rows, " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
