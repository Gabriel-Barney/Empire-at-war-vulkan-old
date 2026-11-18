# Conformance mods `factions_80` and `levels_10` (mcx5 t06)

The limits policy's conformance mods (`docs/MOD_COMPAT_DESIGN.md` §8: no port limit below retail). Both layer on the
FoC install (`MODPATH=`), so they need `ALAMO_FOC_DATA`. `tools/conformance/limit_mods.py build --out DIR` writes them
under `DIR/Mods`; `run` also plays the G2 headless runs (space and land skirmish 3000 ticks, galactic Heart of the Maw
9000 ticks, CRC logs compared on 0 / 4 workers, and across two exes with `--exe2`). ctest (slow): one entry per mod, run
and worker count, `alamo_headless_limits_<mod>_<run>` (workers 0, writes the CRC log) and `..._w4` (compares).

| Mod | Files | Mechanism |
|---|---|---|
| `factions_80` | `Mods/Factions80` (committed): `FactionFiles.xml` lists `Factions80_Filler.xml` before FoC's two faction files | 72 inert factions first, so the stock eight take faction indices 72..79; every campaign creates one player per faction (BKC1_RE_ANSWERS Q2), so a galactic session has 80 players with the playing ones above the old 64-player cap |
| `levels_10` | built from FoC's `GameConstants.xml` (`extracted/foc_data_text.zip`; retail data is never committed) | `MaximumPoliticalControl`, `MaximumStarbaseLevel`, `MaximumGroundbaseLevel`, `MaximumSpecialStructures` = 10: the AI normalises by the data, not by a port 5 |

The filler file is written by hand (72 entries of `Is_Playable false`, `Is_Neutral No`).
