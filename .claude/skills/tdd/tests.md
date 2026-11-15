# Good and bad tests

Adapted from `mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`). Tests use `CHECK` / `ALAMO_CHECK` from
`alamo/test/support.hpp` and end with `return alamo::test::report("<exe>");`. The type and function names below are illustrative.

## Good: behaviour through the interface

```cpp
// A bribed unit changes owner and keeps its health (docs/re/S78M_T06_BRIBE_RUN.md).
void bribe_switches_owner() {
    test::section("bribe switches owner");
    World w = make_world(fixture_rules());
    const ObjectId trooper = w.spawn("Stormtrooper_Squad", kEmpire);
    const float health = w.health(trooper);
    CHECK(w.bribe(trooper, kUnderworld) == BribeResult::Accepted);
    CHECK(w.owner(trooper) == kUnderworld);
    CHECK(w.health(trooper) == health);
}
```

It checks what callers care about, uses only the interface, survives internal refactors, says WHAT not HOW, and asserts one
logical thing.

## Bad: implementation details

```cpp
// BAD: reaches into internals
CHECK(w.objects_[idx].owner_slot == 3);        // private layout, breaks on any refactor
CHECK(g_bribe_calls == 1);                     // counts calls instead of observing the result
```

Red flags: testing private functions or members, asserting call counts or order, a name that describes HOW, verifying through a
side channel instead of the interface.

```cpp
// BAD: verifies a save by reading its bytes
save_game(w, path);
CHECK(read_file(path).find("Underworld") != std::string::npos);
// GOOD: round trip through the interface, and the per-tick CRC matches
World loaded = load_game(path);
CHECK(loaded.owner(trooper) == kUnderworld);
CHECK(crc_of(loaded) == crc_of(w));
```

## Bad: tautological

```cpp
// BAD: the expected value restates the formula, so it passes by construction
CHECK(shield_damage(attack, target) == attack.damage * target.shield_factor);
// GOOD: an independent known value (worked example from the spec, or a retail probe)
CHECK(shield_damage(make_attack(40.0f), make_target(0.25f)) == 10.0f);
```

## Parity over many seeds (tier 2)

A random-driven outcome is checked as a distribution, not a single run: run N seeds through the interface and compare the rate
with the retail reference and its tolerance (`tools/parity`, `alamo_galactic_autoresolve_parity` is an example). One seed's
outcome matching retail proves nothing at tier 2.
