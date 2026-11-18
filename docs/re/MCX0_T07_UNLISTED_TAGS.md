# Tags mods use that no retail table lists (mcx0 t07, 2026-11-17)

Question (roadmap `mcx0` t07, `docs/MOD_COMPAT_DESIGN.md` §7.3): is `docs/re/xml_field_tables.md` complete for the tags
that several unrelated mods use heavily but that no extracted descriptor table lists? If a tag's reader is a code path the
table extraction missed, the port must read it; if not, the tag is dead in retail too and the load report counts it as
retail-equivalent.

## Method

- **The candidates.** The 30 most used `unknown_xml_tags` from the mods' own files, ranked by how many of the four reference
  mods (1125571106, 1976399102, 1770851727, 2858787558) set them, then by uses. Source: `alamo_mod_check` report v4. All
  30 turn out to be object-type tags used by all four mods. The audit's other examples (`Autoresolve_Health`,
  `Number_per_Squadron`, `DPS_is`, `Era_Start`, `Use_Default_Forces`) were checked the same way.
- **Is the name in the binary?** Each name was searched, ASCII case-insensitive and as UTF-16, in the bytes of the Steam
  x64 `StarWarsG.exe`.
  - Retail matches an XML tag against the names in its descriptor tables. Those names are plain zero-terminated ASCII
    strings in the image. Controls: `Max_Speed`, `Tactical_Health` and `Special_Case_Production` are each found as
    `\0Name\0`.
  - So a name that is not in the image at all cannot be read by any table.
- **Where an exact string exists,** its references come from the Ghidra string export (`docs/re/ghidra/strings_xrefs.csv`).

## Result [H]

| Tag (object types) | Uses in the 4 mods | In the exe | Verdict |
|---|---:|---|---|
| `Facing_Adjust` | 2,310 | no | dead in retail |
| `Is_Bomber` | 1,916 | no | dead |
| `WaitsForFormationFormup` | 1,325 | no | dead |
| `Sort_Order_Adjust` | 1,236 | no | dead |
| `Surface_Bombardment_Capable` | 609 | no | dead |
| `SFXEvent_Barrage` | 550 | no | dead |
| `Sensor_Range` | 497 | no | dead |
| `Stealth_Capable` | 315 | no | dead |
| `GUI_Bracket_Height` | 300 | no | dead |
| `Custom_Footprint_Radius` | 292 | no | dead |
| `GUI_Bracket_Width` | 260 | no | dead |
| `Base_Level_Available` | 255 | no | dead |
| `Spawn_Planet` | 151 | no | dead |
| `Max_Ground_Base` | 123 | only inside `MAX_GROUND_BASE_LEVEL` (a story enum name) | dead as a tag (the port's legacy planet column reads it; 0 on every shipped planet) |
| `Special_Structures` | 123 | yes, referenced only by the VisibilityLevelType converter 0x510120 (a bit name, `GALACTIC_REVEAL_CORRUPTION.md` §2.2) | dead as an element |
| `Tactical_Sell_Credits` | 102 | no | dead |
| `FormationPriority` | 94 | no | dead |
| `Required_Tech_Structure` | 92 | no | dead |
| `GUI_Offset` | 92 | no | dead |
| `Is_Sprite` | 66 | no | dead |
| `Formation_Prority` | 64 | no | dead (a misspelling) |
| `IsDeathClone` | 44 | only inside a longer string | dead |
| `SFXEvent_Announce` | 38 | no | dead |
| `SFXEvent_Hardpoint` | 37 | only as the prefix `SFXEvent_Hardpoint_` of other tags | dead |
| `SFXEvent_Enemy_Health_Low_Warning` | 36 | no | dead |
| `SFXEvent_Enemy_Health_Critical_Warning` | 35 | no | dead |
| `TacticalBehavior` | 27 | no | dead |
| `SFXEvent_Apply_Damage` | 21 | no | dead |
| `Base_Power_Ability` | 16 | yes, referenced by the ability factory 0x68b500 and `BasePowerAbilityClass` vf2 | **read by retail**: an ability element |
| `Projectile_Instant_Heal_Duration_Frames` | 13 | no | dead |
| `Autoresolve_Health`, `Number_per_Squadron`, `DPS_is`, `Era_Start`, `Use_Default_Forces` (audit examples) | | no | dead |

## The table gap

The extraction missed no field-table tag of this set. It did miss three **ability element names**:
- The ability factory 0x68b500 compares an element's name with 95 strings (`strings_xrefs.csv`), the ability and action
  classes.
- `docs/re/xml_tags.md` (locality attribution) lists all but three of them under `AllyEventClass`. The missing three are
  `Base_Destruction_Ability`, `Base_Power_Ability` and `Bounty_On_Faction_Ability`.
- Base FoC uses `Base_Power_Ability` 4 times and `Bounty_On_Faction_Ability` 3 times.

The port already implements all three (`engine/tactical/abilities`). Only the coverage report's sub-object list
(`engine/objects/src/sub_object_names.inc`) lacked them, so `alamo_mod_check` reported them as unknown tags. They are now
added: `engine/objects/tools/gen_field_schema.py`, `FACTORY_SUB_OBJECTS`. The base-FoC load report therefore lists two
unknown tags fewer (73 → 71, warnings 141 → 139).

## Consequence for the load report

An unknown tag (no retail table lists it) is reported under `retail_equivalent` with kind `unread_tag`, and it never
lowers the grade (§7.3). That rule now has evidence: none of the 30 most used unlisted tags is a field-table tag in the
binary. A future unlisted tag can be checked the same way: search the exe for the name, then read the references.
