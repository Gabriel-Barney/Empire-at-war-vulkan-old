# Conformance mod `values` (mcx1 t07)

A synthetic FoC folder (`corruption/Data`) and one mod (`corruption/Mods/Values`) that exercise retail's XML value
semantics through the real `MODPATH=` load route, with no game data. Checked by `alamo_game_conformance_values_tests`
(`engine/apps/game/tests/test_conformance_values.cpp`); every expectation cites `docs/re/MCX1_RE_ANSWERS.md` or
`docs/re/OBJECT_TYPES.md`.

| File | Mechanism |
|---|---|
| `GameConstants.xml` | bools by code 0x0 (`On` false, `Yes please` true), atoi / atof prefixes, int overflow clamp, repeated tag last wins, nested element read, `TBD` and empty skipped |
| `ValuesFactions.xml` | repeated scalars last wins, `TBD` skipped, list tags append, nested fields, any entry tag is a faction |
| `ValuesCampaigns.xml` | any entry tag is a campaign, `Locations` append, last `Sort_Order` (atoi), `Planet_Auto_Reveal` `On` is false |
| `ValuesObjects.xml` | element tags carry no behaviour: a `<Moon>` with PLANET is a planet, a `<Planet>` without it is not, a `<Custom_Ship>` with DUMMY_STARSHIP is a starship, a `<SpaceUnit>` without a DUMMY_* id has no galactic role; last-wins planet fields; an ability with `TBD` as a value and a nested field it does not read |
| `ValuesTwoRoots.xml` | a second top-level element: the first root is read, the rest reported (`xml_extra_root`) |
| `ValuesUtf16.xml` | UTF-16 LE with BOM: refused, as the original does |

Regenerate with the script kept in the mcx1 lane notes; the files are small and edited by hand since.
