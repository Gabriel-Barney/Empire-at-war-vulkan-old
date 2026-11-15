# When to use a stand-in

Adapted from `mattpocock/skills` (MIT, `.claude/skills/LICENSE-mattpocock-skills.txt`).

Replace a dependency only at a **system seam**, where the project does not control what is on the other side:
- the GPU / Vulkan driver (headless host, no device);
- the file system and the game's MEG archives (VFS over a fixture folder under the test's own `%TEMP%` path);
- retail game data (small fixture XML, or the real data through `ALAMO_FOC_DATA` with exit 77 when unset);
- the OS, Steam, audio devices, wall-clock time.

Don't replace:
- the project's own modules or internal collaborators;
- the random-number generator in sim code: seed it (`alamo::SimRng` from `alamo/core/sim_rng.hpp`, fixed seed) so the run is deterministic, instead of faking it;
- the job system: run at worker count 0 and N and compare CRCs (`determinism-check`).

## Designing for stand-ins

**Pass the dependency in** rather than reaching for a global:

```cpp
// Easy to substitute
LoadReport load_mod(const vfs::FileSystem& fs, const ModPaths& paths);
// Hard to substitute
LoadReport load_mod() { auto& fs = vfs::global(); /* ... */ }
```

**Prefer specific operations over one generic entry point.** A narrow interface (`read_xml(path)`, `list_files(dir)`) gives each
fake one shape to return and shows which operations a test uses; a generic `request(kind, args)` makes every fake grow
conditional logic.
