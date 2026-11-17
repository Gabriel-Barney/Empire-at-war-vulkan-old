# Audio (SFX events, music, speech, mixer) — behaviour spec for engine/audio (StarWarsG.exe FoC x64 v1.121)

Written 2026-10-27 by the RE agent for the engine/audio implementer (roadmap P3: "events play with the
original's selection rules"). Clean-room: prose and tables only, no code or pseudo-code from the
decompilation. Addresses are RVAs in StarWarsG.exe; names come from `docs/re/ghidra/` where they exist,
otherwise `FUN_<rva>`. Confidence: **[H]** read in the code, **[M]** read but role inferred, **[L]** inferred
from strings, data or surroundings.

Companions: `MAIN_LOOP.md` (frame order §1.1 step 14, RNG §4.1), `xml_field_tables.md` (SFXEvent,
MusicEvent, Audio, WeatherAudio segments; some offsets in the MusicEvent segment do not match the code and
are not used here), `SUBSYSTEMS.md` §audio, `docs/assets/CATALOG.md` (archives).

Global objects:

| global | what |
|---|---|
| 0x140a7d0a0 | Audio system (Miles wrapper): 2D/3D drivers, listener, four volume buses, 2D voice pool (+0x68), 3D voice pool (+0x490), music stream player (+0x10 = 0x140a7d0b0), speech stream player (+0xf40 = 0x140a7dfe0) |
| 0x140b27e60 | SFXEventManager (active-event list at +0x68/+0x70/+0x78, permission flags +0x80..+0x83, 3D scale +0x98) |
| 0x140b27dc0 | MusicEventManager (current music mode at +0xc = 0x140b27dcc, playlist queue +0x68) |
| 0x140b2b300 | SpeechEventManager (FIFO queue +0x60..+0x68, done-callbacks +0x80) |
| 0x140b2b3b0 | WeatherAudioManager (weather loops, weather intermittent sounds and the per-environment `Ambient_Map_Sounds`; update 0x325190 in the main loop; §9.3) [H] |
| 0x140b30160 | AudioConstants (Audio.xml); field offsets of `xml_field_tables.md` §Audio apply to this base [H] |

---

## 0. Summary for the implementer

1. **The original uses Miles Sound System 64-bit (MSS64.DLL)** with Bink for FMV. Everything the game does
   above Miles (event selection, state machine, voice stealing, buses, music playlists, speech queue) is
   game logic and must be replicated; Miles itself (mixing, MP3/WAV decode, 3D panning) is replaced by
   miniaudio. [H] (imports.csv; 0x21c2f0)
2. **All audio randomness uses the free (non-synchronized) RNG** at 0x140a13e20 (same LCG as the synced one,
   MAIN_LOOP §4.1). Audio never touches the synchronized RNG and never writes sim state. [H]
3. **Audio runs once per main-loop iteration, on wall time, only while the application is active**
   (MAIN_LOOP §1.1 step 14), not per sim tick. Several fades and countdowns are therefore stepped per
   iteration (frame-rate dependent) — listed in §7. [H]
4. **An SFX event instance is a small state machine** (predelay → pre-sample → primary sample → post-sample
   → postdelay → repeat/chain). Every repetition re-draws volume, pan, pitch and delays. [H]
5. **Two voice pools:** 16 2D voices and up to 32 3D voices. Lower `Priority` number = more important.
   2D stealing takes the least important, oldest voice; 3D stealing takes the least important, farthest
   voice, and only if the new sound is closer. [H]
6. **Four buses:** 0 master, 1 SFX, 2 speech/dialog, 3 music. Localized SFX events (unit responses, HUD
   VO) go to the speech bus, everything else to the SFX bus. While a speech stream plays, every SFX voice is
   capped at 0.3 linear volume (ducking). [H]
7. **Music is a mode-driven playlist** (galactic, tactical ambient, tactical battle, battle pending, Lua,
   victory/defeat); battle music starts from damage notifications involving the local player and falls back
   to ambient after `Music_*_Battle_To_Ambient_Peace_Seconds` of sim time without one. [H]
8. **Speech is a FIFO of streamed MP3s**, one at a time, centred, on the speech bus. [H]

---

## 1. SFX events

### 1.1 Data (SFXEvent XML) and defaults [H] (ctor 0x522010, parser 0x522910)

Files: `Data\XML\SFXEventFiles.xml` lists the event files; presets must come first (the file says so).
Events are looked up by the CRC of the upper-cased name [M] (0x2d4750). Duplicate names log
`Error: SFXEvent '%s' already exists.` and keep the first (0x2d5df0) [M].

| tag | default | meaning (as used by the code) |
|---|---|---|
| `Is_Preset` | no | preset: cannot be played (start logs an error and fails) |
| `Use_Preset` | — | copies **every** field of the named preset (including sample lists) into this event at that point of parsing, then later tags override. The event keeps its own name/index and is never a preset. Tags written before `Use_Preset` are overwritten by it. [H] (0x5226b0) |
| `Samples`, `Pre_Samples`, `Post_Samples` | empty | file name lists (leaf names, resolved in the SFX archives, §5) |
| `Text_ID` | — | list of text ids; not used by the playback path [L] (used by the SFX validation tool dialogs) |
| `Play_Sequentially` | no | sequential instead of random sample pick (§1.3) |
| `Priority` | 3 | voice priority; **1 = most important** (GUI/unit-response presets use 1, chatter 2, guns 4, ambient 5) |
| `Probability` | 100 | percent chance to start (§1.2) |
| `Play_Count` | 1 | number of full cycles; **-1 = loop forever** |
| `Loop_Fade_In_Seconds`, `Loop_Fade_Out_Seconds` | 0 | passed by the callers that start/stop engine and ambient loops (§1.6) [M] |
| `Max_Instances` | 1 | 0 = event never plays |
| `Min_Volume`, `Max_Volume` | 100, 100 | percent |
| `Min_Pitch`, `Max_Pitch` | 100, 100 | percent of the sample's native rate |
| `Min_Pan2D`, `Max_Pan2D` | 50, 50 | 0..100, 50 = centre (2D only) |
| `Min_Predelay`, `Max_Predelay`, `Min_Postdelay`, `Max_Postdelay` | 0 | milliseconds |
| `Volume_Saturation_Distance` | 300.0 | 3D minimum distance (full volume inside it), §1.7 |
| `Kills_Previous_Object_SFX` | no | §1.5 |
| `Overlap_Test` | — | group name; a 32-bit CRC of it is stored next to the string [M] |
| `Localize` | no | language substitution of file names and routing to the speech bus |
| `Is_3D` | **yes** | `Is_3D` wins when both are set |
| `Is_2D` | no | |
| `Is_GUI`, `Is_HUD_VO`, `Is_Unit_Response_VO`, `Is_Ambient_VO` | no | permission categories (§1.2) |
| `Chained_SFXEvent` | — | event started when this one ends (§1.4) |
| `Render_Mode` | — | unused in vanilla data |

`SFXEvent ... has no primary samples defined` is reported when an instance reaches the primary stage with
an empty `Samples` list; the instance then skips to the post stage. [H]

### 1.2 Start gates (`Start_SFX_Event_Internally` 0x2d5440) [H]

Entry points: by name, 2D (0x2d5010, GUI); by event, 2D (0x2d5290, HUD/announcements); by event or name
attached to an object (0x2d5320, 0x2d50e0, chooses 3D-attached if `Is_3D`, else 2D-attached); at a world
position (0x2d5240, 3D-positional). The instance type is one of: 0 = 2D, 1 = 2D attached to an object,
2 = 3D at a fixed position, 3 = 3D attached to an object (position followed every update).

Tests, in order; any failure means "not started" (no voice, no RNG draw after that point):

1. Event is a preset → error, fail. `Play_Count` = 0 → fail.
2. The event is `SFXEvent_GUI_Negative_Feedback` (AudioConstants +0x68) while a cinematic camera is active → fail.
3. Permission flags of the SFX manager (all default on; set from Lua `SFXManager.Allow_*`):
   `Localize` events need "allow localized" (+0x80); `Is_Unit_Response_VO` needs +0x81; `Is_HUD_VO` needs
   +0x82; `Is_Ambient_VO` needs +0x83. Lua `Allow_Localized_SFX` (land tactical only) writes +0x80.

The remaining tests are skipped when the caller passes "force":

4. GUI-only mode: when manager flag +0x94 is set and a modal GUI dialog is open (GUI dialog manager
   0x140a78510 +0x98), only `Is_GUI` events start, and never the negative-feedback event.
5. `Is_HUD_VO` events fail while a speech stream is playing; additionally, when the local player's flag
   +0x3c0 is set [L: tutorial/scripted mode; the HUD preset comment says "will not play during tutorial"],
   they fail in any tactical mode.
6. `Max_Instances`: count active instances of this event (any object). If the call is attached to an object
   that already has this event and the event loops forever → fail with `Skipping playing SFXEvent loop
   ... because already attached`. If the count ≥ `Max_Instances` and the instance found first is 2D → fail
   (2D events never replace their own older instance). 3D events are handled at voice start (§1.7).
7. `Overlap_Test`: if any active instance (of any event) has the same overlap CRC → fail. This is how one
   unit-response or HUD group (e.g. `Unit_EHD`, `Unit_Vader_Executor`) never talks over itself.
8. `Probability` < 100: one free-RNG draw `int(1, 100)`; fail if the draw is greater than `Probability`.
9. (not skippable) 3D-positional calls only: fail if the position is under the local player's fog of war
   (bypassed during cinematics) [H for the call, M for "fog"]. Object-attached calls never fail here; they
   are muted instead (§1.8).

On success: the per-event sequential counters are aligned (§1.3), a new instance id (incrementing, skips -1)
is returned, and the instance is put on the active list: 2D instances at the front, 3D at the back. The
instance does not play until the next SFX update (state 1); the GUI entry points take a "pump" count and
run that many SFX updates immediately so clicks play in the same frame. [H]

### 1.3 Sample selection [H] (0x2d7a40)

- **Random** (`Play_Sequentially` = no): each stage draws `int(0, n-1)` from the free RNG over that stage's
  list. Repeats are allowed (no "avoid last" rule). A list of one entry draws nothing (the RNG returns lo
  without stepping when lo = hi).
- **Sequential**: three counters per event (primary, pre, post), global across all instances and objects,
  start at 0 at load, persist for the session. A pick uses the counter and then increments it, wrapping
  to 0 at the list size. At instance start, if `Pre_Samples` has the same length as `Samples` the pre
  counter is set equal to the primary counter, and likewise for `Post_Samples`, so pre/primary/post lines
  stay aligned (VO lines with matching intro/outro beeps).
- **Localize**: when the event has `Localize` and the game language is not English, the chosen file name
  has its `_ENG.WAV` (or `_ENG.MP3`) suffix replaced by the language's suffix (`_GER`, `_FRE`, `_SPA`,
  `_ITA`, `_JAP`, `_KOR`, `_CHI`, `_RUS`, `_POL`, `_THA`), case-insensitive (0x200240). The language is
  captured when the instance is created.

### 1.4 Instance state machine (per active instance, per SFX update) [H] (0x2d7a40, 0x2d7450)

| state | action | next |
|---|---|---|
| 1 init | draw, in this order, from the free RNG: volume `int(Min_Volume, Max_Volume)`, pan `int(Min_Pan2D, Max_Pan2D)`, pitch `int(Min_Pitch, Max_Pitch)`, predelay `int(Min_Predelay, Max_Predelay)`, postdelay `int(Min_Postdelay, Max_Postdelay)` | 2 if predelay > 0, else 3 if pre-samples exist, else 5 |
| 2 predelay | subtract wall ms since the previous SFX update | 3/5 when ≤ 0 |
| 3 pre | pick and start a pre-sample (play once) | 4, or 11 if no voice |
| 4 | wait until that voice stops | 5 |
| 5 primary | pick and start a primary sample; loop count = infinite when `Play_Count` = -1, else once | 6, or 11 if no voice |
| 6 | wait (a forever-loop never leaves this state until stopped) | 7 |
| 7 post | pick and start a post-sample (once) | 8, or 9 if none |
| 8 | wait | 9 |
| 9 / 10 postdelay | countdown in wall ms | 11 |
| 11 end of cycle | cycle count += 1; if `Play_Count` = -1 or count < `Play_Count` → back to 1 (all values re-drawn) | else chain or 0 |
| 0 | instance removed at the end of the update | |

Chaining: at the end of the last cycle, a 2D instance (types 0/1) starts `Chained_SFXEvent` (or an override
chain set by the caller) **only if the chained event is 2D**; it is started through the normal gates (with
the same object for attached instances). 3D instances never chain. [H]

While waiting in states 4/6/8 a 3D-attached instance copies the object's current position to its voice
every update. If a voice cannot be obtained (pool full, see §1.7) the instance goes to state 11 — the
remaining stages of that cycle are skipped, it is not retried. [H]

Each update starts by sorting the active list: all 2D instances first, then 3D instances nearest to the
listener first (`qsort`, not stable) [H] (0x2d3d90). So when voices are scarce, near 3D sounds win.

### 1.5 Stopping and killing [H]

- Stop event on object (0x2d5c00) / stop event (0x2d5a30): with a fade time > 0 the instance enters a
  fade-out: level starts at 1.0 and decreases by `(1/30) / fade_seconds` **per SFX update**; at ≤ 0 the
  voice stops and the instance is removed. With fade 0 the voice stops immediately. Lua
  `GameObject.Stop_SFX_Event` uses this path.
- `Kills_Previous_Object_SFX`: before starting an object-attached event with this flag, **all** active
  instances attached to that object (any event) are stopped immediately, no fade (0x2d4250).
- Object death/deletion also kills every instance attached to the object (DeathBehavior 0x58cf90, object
  removal 0x2a6a00).
- Stop all (0x2d6f20) is called on mode push/pop, load and quit (MAIN_LOOP §2.2/2.3).

### 1.6 Volume of a playing instance (recomputed every update) [H] (0x2d7450, 0x2d4b00)

voice gain = bus gain × (volume% / 100) × fade level × visibility factor × (3D only) global 3D scale ×
(3D only) attract-mode factor, then capped at 0.3 while a speech stream plays and no modal dialog is open.
Miles applies its 3D distance rolloff (§1.7) on top of this capped gain [M: the cap is on the gain the game hands Miles;
the rolloff is Miles-internal], so the port multiplies its distance
attenuation in **after** the cap (aud2 t06): a distant 3D voice is ducked by the same factor as a near one.

- Bus gain: §4. Fade level: 1.0 unless fading out (§1.5).
- Visibility factor (3D-attached only): 0 when the object is hidden [L: flag +0x3a2 bit 0], when a
  stealth/garrison test on +0x2a0 holds [L], or when the object is under the local player's fog of war
  (skipped during cinematics); otherwise 1. Hidden objects keep their instance running silently.
- Global 3D scale (SFX manager +0x98): set to 0 when the battle-load dialog opens (0x8d930, 0x8cb50) and
  back to 1 when the battle starts [M], so 3D SFX are silent behind the load screen.
- Attract-mode factor: 0 for 3D voices while the AI-vs-AI attract demo runs (GMM +0x1e4, set by 0x22f10).
- The voice is started with an extra per-voice fade-in when the caller passes `Loop_Fade_In_Seconds`
  (initial level 0, raised by the voice pool's wall-time update) [M].

### 1.7 Voice pools, priorities, stealing, 3D distance [H] (0x228750, 0x2291f0, 0x22a500, 0x22afb0)

2D pool: 16 voices on a stereo 44.1 kHz 16-bit driver. Start: take a free voice; otherwise find the
victim = voice with the **largest** priority number, the **oldest** start time on ties; steal it (log
`Priority system killing SFX2D index ...`) if the new priority number ≤ victim's, else the start fails.
Pan = drawn pan / 100; playback rate = native rate × pitch / 100.

3D pool: up to 32 voices on the 3D driver (provider from the registry/`MSS_3D_Provider_Name`, default
"Miles Fast 2D Positional Audio"). Before taking a voice, per-event `Max_Instances` for 3D: if at least
`Max_Instances` other instances of the same event hold a 3D voice, the new one is refused when it is
**farther** from the listener than the farthest of them; otherwise the farthest one's voice is stopped and
the new one proceeds (0x2d3f80). Then the pool: free voice, else victim = largest priority number, the
**farthest from the listener** on ties; steal only if new priority ≤ victim's **and** the new position is
strictly closer than the victim. Decoded 3D sample buffers are cached by file name in a reference-counted
map so repeated sounds do not reload.

3D distance model: Miles is given min distance = `Volume_Saturation_Distance` × the mode's
`Audio_<Mode>_3D_Saturation_Distance_Mod` (clamped to ≥ 1.0) and **max distance 15000.0** (constant in the
exe) per voice; the driver-wide rolloff factor is the mode's `Audio_<Mode>_3D_Rolloff_Distance_Mod`.
Mode activation (vtable slot 41: galactic 0x4b2330, land 0x3b86c0, space 0x4d7ce0) sets the pair; mode
shutdown resets both to 1.0. Vanilla: galactic 1.0/1.0, space 1.5/2.0, land 1.0/10.0. The attenuation
curve itself is Miles' (DirectSound3D-style inverse distance: full volume inside min distance, then
min / (min + rolloff × (d − min)), nothing beyond max) [M — library behaviour, not visible in the exe].
3D voices are mono point sources; no Doppler or cone calls are imported. [H]

### 1.8 Listener [H] (main loop step 2; galactic 0x4b1600, land 0x3b5e20, space shares 0x4b1600)

Once per iteration, before the message pump, the top mode supplies position, forward and up; the audio
update pushes them to Miles. With no mode: origin, forward (0,0,1), up (0,1,0) as written by the code.

- Tactical: position = camera look-at point − camera forward × `Audio_<Mode>_3D_Listener_Z_Pullback_Dist`
  (vanilla 60), with the camera's orientation. Galactic: same idea with the galactic pullback (vanilla 0).
- During a cinematic camera (0x140b27a10 active) the pullback is 0 and the listener is the camera position.
- The listener position is also what §1.4 sorting, §1.7 stealing and the intermittent ambient manager use.

---

## 2. Music

### 2.1 MusicEvent data [H] (parser 0x521c40, update 0x2d2ab0, stream 0x22c030/0x22bc20)

| tag | meaning |
|---|---|
| `Files` | playlist of MP3 names under `Data\Audio\Music\` (archive `Music.meg`) |
| `Volume_Percent` | event volume |
| `Fade_In_Seconds` | linear fade-in of the new stream (0 = start at full volume) |
| `Fade_Out_Previous_Seconds` | linear fade-out of the stream being replaced (0 = cut) |
| `Loop` | when a file ends, continue with the next file of the playlist (forever) |
| `MSS_Internal_Loop` | the stream itself loops (infinite loop count, restarted if it reports done) |
| `Text_ID` | unused by playback [L] |

### 2.2 Player [H]

One music stream plus one fading-out "previous" stream. Starting a new file: any older previous stream is
closed at once; the current stream becomes the previous one and fades from its volume to 0 over
`Fade_Out_Previous_Seconds`; the new stream starts at 0 and ramps to `Volume_Percent` over
`Fade_In_Seconds` (or starts at full volume). Ramps are per wall-clock millisecond. Gain = music bus ×
master × ramp level. This is the whole crossfade model.

File choice: each event has a **sequential** file counter (starts at 0 at boot, persists, wraps); every
start of the event takes the next file. A free-RNG draw `int(0, n-1)` is made when an event is queued but
its result is not used for the file choice (the log calls the sequential index "random index"). [H]

Playlist manager states per queued entry: 1 start file → 2 playing → 3 ended. At "ended": if `Loop`, go to
the next file (state 1); otherwise the entry is removed, and if it was queued with "resume previous", the
previously playing event is restarted (fresh queue entry) in its previous mode. In music mode 14 a looping
playlist stops after one full pass and resumes the previous event. [H for the mechanics; L for who uses 14]

### 2.3 Music modes and triggers (manager +0xc) [H]

`Play(event, mode, resume_previous, force)` (0x2d21f0) refuses when music is disabled, when the requested
event is already the current one (unless forced), and in mode 13 with the manager's lock flag set (unless
forced). On success it replaces the queue and records the mode.

| mode | set by | event |
|---|---|---|
| 1 galactic | entering/resuming the galactic mode (0x28ed40, via 0x28e800) | local faction `Strategic_Map_Music_Event` |
| 5 battle pending | tactical battle pending/loading (0x291330) | faction `Music_Event_Tactical_Land/Space_Battle_Pending`, plus the matching `SFX_Event_..._Battle_Pending` |
| 6 tactical ambient | tactical start/resume and the peace timeout (0x28ee00) | random free-RNG pick from the local faction's `Music_Event_List_Ambient` entries for the map environment type (TED map-info mini `06`; space maps always use slot 7 "Space"; §9.1); `Music_Event_<Land/Space>_Ambient_Super_Weapon` instead when any player has its faction super weapon on the map |
| 7 tactical battle | combat involving the local player (0x28f020) | same, from `Music_Event_List_Battle` / `..._Battle_Super_Weapon`. Refusal rules: §9.2 |
| 9 / 10 | 0x28f250 (two callers, 0x343b90, 0x88fe0) | per-type music for the local player's own (9) or an enemy (10) object [L] |
| 12 | Lua `Play_Music(name)` (forced, no resume) | named event |
| 8, 11, 13, 14 | 8 TSW activation, 11 splash screen, 13 front-end menus, 14 movie music; plus 0 debug list, 4 build complete, 15 load screen, 16 battle end, 17 movie cues — STRATEGIC_GAPS §7.4 | H |

Automatic mode music (`Resume_Mode_Based_Music`, 0x28e800) does nothing while in modes 8, 11, 12 or during
the attract demo, unless forced (Lua `Resume_Mode_Based_Music` forces).

**Combat intensity switch** [H] (Land/Space vtable slot 76 = 0x3ba180 / 0x4d8ae0; tick 0x3bb440 /
0x4d95a0): the slot-76 notification receives two objects (attacker, victim). If either is owned by the
local player, or is a friendly (own or allied, 0x39a2c0) object whose type has `Is_Community_Property`
(type +0x27), the battle mode is requested (only if the current mode is not 7; 0x28f020 may still refuse,
§9.2) and the mode's peace counter is reset to 0. (Corrected 2026-11-15: earlier text said "an enemy the
local player can see".) The counter increments once per **sim tick**; while in mode 7, when it
reaches 30 × `Music_Land_Battle_To_Ambient_Peace_Seconds` (space: `Music_Space_...`; vanilla 15 s) the
ambient mode starts. Other resets of the counter: objective/AI "under attack" checks in the tick [M]. So
peace time is sim time and scales with game speed. The same notification starts the base-under-attack
announcement (local faction `SFXEvent_Land_Base_Under_Attack_Announcement` +0x6e0 on land,
`SFXEvent_Space_Base_Under_Attack_Announcement` +0x6d0 in space, plus a radar ping), rate-limited by
`Delay_Between_Land/Space_Base_Attack_Announcement_Seconds` × 30 ticks; who counts as a base: §9.8. [H]

Other triggers: main menu `Main_Menu_Music_Event`, splash `Splash_E3_Music_Event`, credits
`Game_Credits_Music_Event`, battle load screen `Music_Event_Battle_Load_Screen`, win/lose
(`Music_Event_Tactical_Win/Lose`, `_Vs_Faction` variants, battle-end dialogs 0x87770..0x88af0),
Hutt Palace / Cantina structure selection (`Structure_..._Music_Event`, 0x44890), production complete
(`Build_Music_Completed`), TSW activation (`TSW_MusicEvent_Activation_Start`). Lua: `Play_Music`,
`Stop_All_Music` (closes both streams), `Resume_Mode_Based_Music`. Application deactivation increments
the music manager's pause counter (WndProc 0x60c60), which stops its updates. [H] [port] (aud2 t04) The port's
`AudioSystem::pause_music` / `resume_music` keep the counter; the first pause also holds the playing music voices at
their position (miniaudio keeps mixing on its own thread, where Miles' streams starve once step 14 stops) and the
last resume releases them; play requests made meanwhile start with the first update after it. The game host raises
it on WM_ACTIVATEAPP (inactive) and skips its whole audio update while inactive (§6.1), so a speech's end (and the
story's speech-done) waits for reactivation in a live game whose sim keeps ticking; smoke, hidden and benchmark runs
never pause.

---

## 3. Speech and unit responses

### 3.1 SpeechEvents (Data\XML\SpeechEvents.xml) [H] (0x320f80, 0x321170, update 0x321dc0)

Fields: `Files` (list), `Text_ID`, `Volume_Percent`, `Overlap_Test` (29 uses) [field set from the data].
Queue: FIFO. Only the head entry plays. When the head has no stream yet, a file is chosen with
`int(0, n-1)` from the free RNG, localized like SFX if the language is not English, and streamed from
`./Data/Audio/Speech/<Language>/<file>` (archive `EnglishSpeech.meg` holds `DATA\AUDIO\SPEECH\ENGLISH\…`).
Gain = speech bus × master × `Volume_Percent`/100, pan centre. When the stream ends, the entry is popped and
every registered "speech done" callback runs (story mode uses this for `STORY_SPEECH_DONE` style waits);
the next entry starts on the next update. The stream player keeps at most one speech stream: starting
another closes the current one. `Stop_All_Speech` (Lua) and mode changes kill the stream and record the
killed event names for story callbacks (0x321830). Users: story/tutorial scripts, tactical intros
(`SpeechEvent_Tactical_Intro_*`), super-weapon range warnings, corruption missions, hero clash, production,
the external hologram (below).

### 3.2 Speech priority over SFX [H]

- While any speech stream plays, all SFX voices are capped at 0.3 (§1.6), unless a modal dialog is open.
- `Is_HUD_VO` SFX events cannot start while a speech stream plays (§1.2 test 5).
- Music is not ducked by speech. [H — no music path reads the speech state]

### 3.3 Unit responses [H for the play path, M for the selection of the unit]

Responses are ordinary SFXEvents referenced from GameObject types (`SFXEvent_Select`, `_Move`,
`_Fleet_Move`, `_Attack`, `_Guard`, `_Stop`, `_Deploy`, `_Group_Move`, `_Group_Attack`, `_Assist_Move`,
`_Assist_Attack`, `_Unit_Lost`, `_..._With_Non_Hero_Ability` variants, …). On a local selection (click 0x3a310,
drag/select-all 0x33b30, 0x44280) or order the UI picks one representative unit of the selection [M]
(0x38290), resolves the type's response (0x3739a0 handles the non-hero-ability override and team/garrison
containers) and starts it attached to that unit as a 2D event. Nothing is played for units in an
excluded set of the current mode [L]. The responses run in the UI path, before the command reaches the
event queue, so they are local-only.

There is no dedicated response timer. Rate limiting comes from the event data: the unit-response presets
are 2D with `Max_Instances` 1 (a second response of the same event while one plays is dropped, §1.2 test 6),
`Overlap_Test` groups per unit/faction stop different lines of the same speaker overlapping, chatter presets
use `Probability` 50–60, and `Allow_Unit_Reponse_VO` (Lua) can switch them off. Unit responses are
`Localize`d, so they play on the **speech/dialog bus** but through the SFX voice pool (priority 1).

### 3.4 Subtitles [M]

Speech `Text_ID` is shown as on-screen text by the story/hologram paths: the tactical "external hologram"
(common tick 0x3639d0) queues a speech event, plays a Bink talking-head movie, displays the speech's
subtitle text through the HUD while it plays and removes both when the speech ends; it aborts a pending
hologram if another speech is active. Story speech uses `TEXT_SPEECH_%s` ids (0x32e290). SFX
`Text_ID` lists (unit responses) are not displayed in game [L].

**Movie subtitles** [H unless marked] (ui5 t12): the audio-options box `IDC_USE_MOVIE_SUBTITLES` is the profile flag
`Subtitles` (options +0x212, global 0x140a7c442). It gates two things: the hologram subtitle above (0x2dff30 shows a
subtitle-kind line only with it on) and the **full-screen Bink player's captions**. Their source is Movies.xml:
`<Caption_Frame> TEXT_ID, frame </Caption_Frame>` and `<Caption_Duration> TEXT_ID, frames </Caption_Duration>` per
`<Movie>` (several of each; both kept in maps keyed by the text id), e.g. the FoC win movies `Empire_Land_Win` (26
captions in the FoC data). Per Bink frame (0x44dea0, after `BinkNextFrame`; frames numbered from 1), with the box on,
0x44f420 looks for a caption whose frame lies in (previous frame, current frame] (0x5dba50; the map's first match in
text-id order). Found: the text id is looked up in the game text; a text equal to `[TBL]` shows nothing; else the
caption on screen is replaced: font = GameConstants `Bink_Player_Caption_Font_Name` / `_Font_Size` (FoC
EmpireAtWar-Medium, 10), white (255,255,255,255), split into lines (line breaks, wrapped [M: to the screen width]),
each line centred horizontally, the block's last line at 0.95 of the screen height from the top and the lines above it
one font line height apart; shown from the current frame for `Caption_Duration` of that id (0 → 120 frames; a missing
duration ends it on the next frame). No caption starts that frame: the shown one is removed once the current frame
≥ start + duration. A new caption replaces the old at once. The advisor / hologram talking-head loops (texture
movies) have no captions.

---

## 4. Mixer [H] (0x21c140, 0x21cf20, 0x21ce30, 0x21d2f0)

| bus | used by | registry value | slider |
|---|---|---|---|
| 0 master | multiplies the other three | `VolumeMaster` | `IDC_SLIDER_MASTER_VOLUME` |
| 1 SFX | non-localized SFX (2D and 3D) | `VolumeSFX` | `IDC_SLIDER_SOUND_EFFECTS_VOLUME` |
| 2 speech | speech streams and **localized** SFX events | `VolumeSpeech` | `IDC_SLIDER_DIALOG_VOLUME` |
| 3 music | music streams | `VolumeMusic` | `IDC_SLIDER_MUSIC_VOLUME` |

- Effective gain of bus 1..3 = bus level × master level; bus levels are linear 0..1, default 0.8, stored
  per profile under `Software\Petroglyph\StarWars FOC\Profiles` (0x2029d0).
- Each bus can ramp to a target over a time (rate per wall millisecond, clamped at the target); every
  audio update advances the ramps and re-applies gains to the affected voices and streams. A saved level
  supports Lua `Master_Volume_Restore` (0x21cf60/0x21cfa0).
- Ducking: only the 0.3 SFX cap while speech plays (§3.2). There is no ambient bus: ambient beds, weather
  and `Is_Ambient_VO` are ordinary SFX events on bus 1 (or bus 2 when localized).
- `Default_/Sustained_IO_MSS_DIG_DS_*_FRAGMENT_CNT` (Audio.xml) are Miles mix-ahead preferences raised
  during heavy disk I/O; the port can ignore them.

---

## 5. Files, archives and decoding

| content | where | format (scanned from the installed archives) |
|---|---|---|
| 2D SFX, localized | `Data\Audio\SFX\SFX2D_<Language>.meg` (English, German, French, Spanish, Italian, Japanese, Korean, Chinese, Russian, Polish, Thai) | WAV PCM 16-bit mono, mostly 22050 Hz (some 11025/44100) |
| 2D SFX, not localized | `Data\Audio\SFX\SFX2D_Non_Localized.meg` | WAV PCM 16-bit, 44100 Hz mono; EaW also has 27 stereo files |
| 3D SFX | `Data\Audio\SFX\SFX3D_Non_Localized.Meg` | WAV PCM 16-bit mono, 44100/22050 Hz |
| speech | `<Language>Speech.meg` (via MegaFiles.xml), paths `DATA\AUDIO\SPEECH\<LANGUAGE>\*.MP3` (a few WAV) | MP3 (and PCM WAV) |
| music | `Music.meg`, `DATA\AUDIO\MUSIC\*.MP3` | MP3 |
| event data | `Data\XML\SFXEventFiles.xml` (+ listed files), `MusicEvents.xml`, `SpeechEvents.xml`, `Audio.xml`, `WeatherAudio.xml`, `AnimationSFXMaps.txt` | XML / tab text |

- The three SFX archives are mounted into the global MEG manager by the audio system itself at init
  (0x227b90, 0x2299a0; also tried relative to the mod/install prefix), not through MegaFiles.xml.
  SFX archives store bare leaf names, so samples are resolved by leaf name through the normal VFS
  (loose files override) [H]. A missing sample logs `File %s not in SFX2D megafile.` and the stage is skipped.
  Read with REA 2026-11-17 (aud2 t05): 0x227b90 mounts each archive twice, once as the prefix string
  (0x140a15320) + `Data\Audio\SFX\SFX2D_Non_Localized.meg` and once by the plain relative name through the normal
  file open; the localized one is built from the language name the same way; init fails when any mount fails. So a
  mod's own copy (found first by the plain open) is mounted next to the install copy and **layers over it** rather
  than replacing it [M: which string the prefix holds, read as the install folder]. [port] `default_sfx_megs`: per
  archive name every mod search dir's copy, then FoC's, then EaW's; the loose `Data\Audio\SFX\<name>` (mod, then
  FoC) is tried before any archive.
- SFX samples are read whole into memory and handed to Miles; music and speech are **streamed** through
  Miles file callbacks that go to the game's file layer, so they also come from MEGs [H] (0x21c2f0).
- No ADPCM, Ogg or Miles-specific compression was found in the vanilla archives (FoC and EaW scanned):
  the port needs a PCM WAV reader and an MP3 decoder (both in miniaudio). EaW fallback archives are used
  for EaW-only names (MAIN_LOOP §5.1, SUBSYSTEMS mount order).
- `AnimationSFXMaps.txt` maps animation name + frame time to `SOUND <event>` (play SFX) or `SURFACE <fx>
  <bone>` (surface-dependent footstep effect); loaded by AnimSFXMapper (0x4770a0); timing and play rules §9.4 [H].
- Replicate: event selection and state machine, pools/stealing, buses and ducking, music playlist and
  crossfade, speech queue, listener rules, archive lookup and localization suffixes.
  Ignore: Miles provider selection (`MSS_3D_Provider_Name`, EAX), fragment-count preferences,
  `AIL_serve` cadence (every ≥ 125 ms in the original), `_SFX2DLog.txt`/`_Audio_Drivers.txt` logs,
  the SFX/speech validation and audition dialogs (`IDD_SFX_VALIDATION_DIALOG`, `IDD_SFXEVENT_AUDITION_DIALOG`).

---

## 6. Where audio runs and what triggers it

### 6.1 Frame placement [H] (MAIN_LOOP §1.1)

Step 2 asks the mode for the listener; step 14, only when the application is active, runs in this order:
audio system update (listener to Miles, voice-pool updates, bus ramps, stream fades, `AIL_serve`), SFX
event manager update (sort, poll/fade, state machine, remove finished), music manager update, speech
manager update, two further managers via indirect calls (0x140b2b3a8, 0x140b2b2f0: SFX/speech conversation
managers [L]), WeatherAudioManager update (0x325190, the weather / map-ambient intermittent sounds). All receive wall ms; the SFX update also gets the
global frame number. Sim-side audio requests (below) are made **during the tick** (inside GOM service,
behaviours, story) and only queue an instance; nothing is heard until the next step 14.

Port rule (determinism contract): sim code may emit audio requests but must not read anything back from
audio. When sim jobs run in parallel, collect requests per index and submit them in index order on the main
thread, so the free-RNG consumption order stays stable (not required for sync, only for repeatability).

### 6.2 Triggers (callers of the start functions in `docs/re/ghidra/callgraph.csv`)

| source | examples |
|---|---|
| object behaviours, in the tick (object-attached 3D/2D, or 3D at a position) | weapon fire (hard point `Fire_SFXEvent` else the parent's `SFXEvent_Fire`, 3D **at the muzzle position**, §9.5; WeaponBehavior 0x54d090), `Projectile_SFXEvent_Detonate`, death (`Death_SFXEvent_Start_Die`, DeathBehavior 0x58cf90, also kills the object's other sounds), engines (idle/moving loops started and stopped with `Loop_Fade_In/Out_Seconds`, 0x3a7cf0/0x3a7e30), hyperspace, shields, abilities (`SFXEvent_Activate`, `SFXEvent_Target_Affected`; tags and moments §9.6), production/building, garrison, asteroid damage, ambient behaviour (`AmbientSFXBehaviorClass`), self-destruct, mines, telekinesis |
| animation events | AnimationSFXMaps `SOUND` entries (§9.4) |
| UI (2D, by name) | every GUI gadget class (`Click_SFX`, `Mouse_Over_SFX`, `Right_Click_SFX`), command bar (`GUI_Cancel_SFX`, `SFXEvent_Command_Bar_*`), dialogs, options test (`GUI_Audio_Options_SFX_Test`, `Test_Speech_ENG.MP3`) |
| local-player feedback | unit responses (§3.3), `SFXEvent_GUI_Negative_Feedback`, Audio.xml `SFXEvent_*` constants, faction HUD VO (`SFXEvent_HUD_*`, base under attack, reinforcements, bombing run, victory near 0x364920) |
| tactical mode | battle pending, weather (WeatherAudioManager 0x140b2b3b0: intensity-selected loops with fade in/out and intermittent events with min/max seconds; Lua `Weather_Audio_Pause`), ambient map sounds (`Ambient_Map_Sounds` per map environment type, used as the intermittent event while the weather is CLEAR or at intensity 0), faction weather announcements (§9.3), tactical intro speech (§9.7) |
| weather / map-ambient intermittent (WeatherAudioManager update 0x325190) | when its countdown expires (and the manager is not paused), plays its current intermittent event 3D 50 units from the listener at a free-RNG angle `float(0, 2π)`, then re-arms with a free-RNG float × 30 counted down **per update** |
| Lua | `GameObject.Play_SFX_Event` / `Stop_SFX_Event`, `SFXManager.Allow_*`, `Allow_Localized_SFX`, `Play_Music`, `Stop_All_Music`, `Resume_Mode_Based_Music`, `Stop_All_Speech`, `Master_Volume_Restore`, `Weather_Audio_Pause`; story speech |

aud2 port hookups (2026-11-17, `GameAudioHost`; [port] unless tagged): galactic production plays the built type's
`SFXEvent_Build_Started` / `_Build_Cancelled` / `_Build_Complete` 2D for the local player's ProductionBegin / Canceled /
Completed events, every cancel including the queue wipes of an owner change or a sabotage [guess: which retail paths
voice a cancel is not traced], and a completion also starts `Build_Music_Completed` in mode 4; a fleet's move-order
departure (not its pass-through hops) plays `Galactic_Fleet_Hyperspace` when the local player owns it or can see fleet
presence at the origin and it is not all stealthed ("sees" is [M]); the campaign-end dialog plays the local faction's
`Music_Event_Strategic_Win` / `_Lose` (`_Vs_Faction` row first, the winner on a loss) as music mode 16 [M: STRATEGIC_GAPS
§7.4 names mode 16 for the battle-end dialog only].

`Test_Disable_Weapon_Fire_SFX` (Audio.xml, AudioConstants +0x1d8) suppresses weapon and hard-point fire SFX
[H] (0x3825b0, 0x54d090).

---

## 7. Quirks to decide on (frame-rate dependence) [H]

| item | original behaviour |
|---|---|
| SFX stop fade-out | step (1/30)/seconds per SFX update (per render iteration), so a 1 s fade lasts 30/FPS s |
| intermittent ambient countdown | decremented by 1 per update, armed in units of 1/30 s |
| predelay/postdelay, music and speech ramps, bus ramps | wall-clock ms (frame-rate independent) |
| music peace timer, base-attack announcement delay | sim ticks (scale with game speed) |

A faithful port keeps the per-update steps; a port that wants frame-rate independence should convert
them to wall time with the same nominal duration at 30 updates/s.

---

## 8. Open items

- ~~Who uses music modes 8, 11, 13, 14 and sets the music lock flag~~: 8 TSW activation, 11 splash, 13 front-end
  menus (refused while the attract-loop lock is set), 14 movie music; also 0, 4, 15, 16, 17 (STRATEGIC_GAPS §7.4).
  Who sets SFX manager +0x94 (GUI-only): still open.
- The exact visibility tests in the 3D mute factor (object flags +0x3a2, +0x2a0). The "excluded set" (mode +0x30,
  0x3400c0) is the retreat coordinator's list of retreating units (STRATEGIC_GAPS §7.4) [M].
- Per-voice fade-in implementation inside the 2D/3D pool updates (0x227f20, 0x229da0) — not read.
- The two indirectly called managers in step 14 (0x140b2b3a8, 0x140b2b2f0) and the SFX/speech
  conversation data (`TheSpeechConversationManager`, `TheSFXConversationManager`) — not read.
- Miles' exact rolloff law for this MSS version (needs a capture or the MSS docs); the inverse-distance
  model in §1.7 is the documented DS3D-compatible default.
- Whether `Subtitles` gates speech text or only movie text.

---

## 9. Tactical hookups (AUD1)

Written 2026-11-15 for roadmap group AUD1 (battle audio hookups). Same rules as above: behaviour in words,
RVAs, **[H]** read in the code, **[M]** read but role inferred, **[L]** inferred. "Local player" = 0x294a40 on
the player list 0x140a16fd0; "GMM" = GameModeManager 0x140b153e0; FPS = the int at 0x140b0a340 (30).

### 9.1 Map environment type: source, music key, ambient key [H unless tagged]

- **Source: the TED map file.** `Load_Map` (0x56480) reads top-level chunk `0` (map info) as mini chunks;
  mini `01` is the map type (1 land, 2 space) and mini `06` is the **map environment type** (u32). When the
  world chunk `1` starts the mode, 0x364f00(mode, type, 1) stores it at **mode +0x08** and, when the type is
  not 7, sets the weather audio to CLEAR at intensity 0 for that environment (0x323110(0, 0.0, type)). It is
  not read from the planet type's `Terrain`, nor from skirmish setup. Shipped FoC maps: land 0 ×26, 1 ×7,
  2 ×23, 3 ×15, 4 ×9, 5 ×6, 6 ×20; space 7 ×105 and one space map with 2 (`_SPACE_PLANET_DATHOMIR_01`).
- **Enum** `MapEnvironmentType` (0x5e8770): 0 Temperate, 1 Arctic, 2 Desert, 3 Forest, 4 Swamp,
  5 Volcanic, 6 Urban, 7 Space. These are the keys of the `Music_Event_List_Ambient` /
  `Music_Event_List_Battle` pairs in Factions.xml (`Temperate, Temperate_Land_Rebel_Ambient_Music_Event`)
  and of `Ambient_Map_Sounds` in Audio.xml (`Temperate, Ambient_Map_Sounds_Temperate`). The faction keeps
  one event list per environment index (ambient at faction +0x2b58 + 0x18 × env, battle at +0x2c18 +
  0x18 × env); several pairs with the same key append to that list [M for the append; the pick below
  reads a counted list].
- **Music pick** (0x28ee00 ambient, 0x28f020 battle): log `Tactical Mode %s: map environment type = %d`;
  if the type is **≥ 8 nothing plays** (no fallback, no error). If GMM +0x1c is 2 (the started map type is
  space [M]) the index is forced to 7, so the Dathomir space map still uses "Space". Then: if any player's
  faction `Faction_Super_Weapon` (faction +0x2ae8) has at least one object in the mode, the local faction's
  `Music_Event_Space_*_Super_Weapon` / `Music_Event_Land_*_Super_Weapon` is played instead (mode type 2 =
  space, 1 = land; if that field is empty the search continues and falls back to the list). Otherwise, if
  the local faction's list for that index is non-empty, one free-RNG `int(0, n − 1)` picks the entry; **an
  empty list plays nothing** (the current music continues).
- **No / unknown terrain type:** a map without mini `06` keeps the loader's default 0, i.e. **Temperate**
  music and Temperate map ambience. A value above 7 silences tactical music changes and the weather audio
  (0x323110 refuses types ≥ 8).
- **Ambient_Map_Sounds uses the same index.** The WeatherAudioManager keeps one intermittent event per
  environment (manager +0x198 + 0x10 × env, with its delay values next to it). In 0x323110, when the
  weather type is CLEAR or the intensity is exactly 0, the intermittent event is the map-ambient event of
  the current environment; otherwise the weather's own intermittent event (0x323880). Space (7) is never
  set (0x364f00 skips it; Audio.xml has no Space entry).

### 9.2 Refusal rules of the tactical music modes [H]

All three functions take (GMM, force) and first require a current mode that is tactical (mode vf +0xf0
true), not galactic (vf +0xf8 false); the two tactical pickers also need the mode's scene (mode +0x18).
With force = 0 they refuse in:

| function | refuses while the music mode is | also refuses |
|---|---|---|
| 0x28f020 → mode 7 battle | 7, 8, 11, 12, 13 | attract demo (GMM +0x1e4) |
| 0x28ee00 → mode 6 ambient | 6, 8, 11, 12 | attract demo |
| 0x28e800 `Resume_Mode_Based_Music` (→ 0x28ed40 galactic / 0x28ee00 tactical) | 8, 11, 12 | attract demo |

- The combat notification (slot 76, 0x3ba180 land / 0x4d8ae0 space) calls 0x28f020 with **force = 0**
  and only when the music mode is not 7; it resets the peace counter whether or not the music changed.
  0x28f020 does not go through 0x28e800, but its own list covers 8 (TSW activation), 12 (Lua `Play_Music`),
  11 and 13. **Mode 16 (battle end) is not in the list**: a combat notification during mode 16 switches
  to battle music. Modes 5 (battle pending), 6 and 9/10 are not refused either.
- The peace timeout (land tick 0x3bb440, space 0x4d95a0) calls 0x28ee00 with force = 0 **only while the
  music mode is 7**, so mode 8 or 12 never time out to ambient; 0x28ee00 would refuse them anyway.
- Calls through 0x28e800 pass their force flag to 0x28ee00, so the tactical-start resume (common tick
  0x3639d0 when mode +0x9a is set and GMM +0x1e1 is clear [M: start flag]) is refused in 6, 8, 11, 12 and
  during the attract demo. Lua `Resume_Mode_Based_Music` forces and bypasses both lists.
- The play itself (0x2d21f0) is called with resume = 0, force = 0: the same event as the current one is
  not restarted (§2.3).

### 9.3 Weather audio and the faction weather announcement [H]

- 0x323110(type, intensity, env) (callers: weather init 0x468a50, advance 0x4690c0, environment change
  0x360970, map load 0x364f00) refuses type ≥ 5, intensity outside 0..1 or env ≥ 8. Same type as the
  current one: only an intensity change, handled by 0x323340. New type: the old loop is faded out
  (0x323800), the loop for (type, intensity) is started 2D-unattached with a 3 s fade-in (0x2d5290, value
  3.0), the intermittent event is chosen (the weather's, or the map ambient of env when the type is CLEAR
  or the intensity is 0), and the intermittent countdown is armed with a free-RNG float × FPS. **No
  faction announcement on a type change.**
- Loop choice (0x323940): the first loop entry of the type whose intensity threshold is ≤ the intensity
  and whose event has samples.
- 0x323340(intensity) (weather service 0x46a0c0 while a phase blends, and 0x323110): ignores values
  outside 0..1, an unchanged value, or a current type > 4. When the current type is not CLEAR and the
  **local** player is weather-affected (0x282290: false while its `Weatherproof_Ability` timed flag is
  active), it plays one tag of the **local player's faction**, 2D, not forced (0x2d5290):
  - intensity goes from exactly 0 to > 0: the `SFXEvent_Weather_Begin` entry for the current weather type
    (faction +0x1ae0 + 0x10 × type);
  - intensity goes from > 0 to exactly 0: the `SFXEvent_Weather_End` entry (faction +0x1b30 + 0x10 × type).
  The pair lists are keyed by weather type (CLEAR 0, RAIN 1, SNOW 2, SANDSTORM 3, ASH 4; vanilla Begin
  entries are unit chatter such as `Unit_Rain_Rebel_Soldier`, End entries are empty). Then the loop is
  re-chosen for the new intensity (kept if it is the same event), the intermittent event re-chosen and
  re-armed.
- Consequence [M]: the announcement needs the blended intensity to touch exactly 0 within one weather
  type, i.e. a scenario phase whose `Emitter_Intensity` is 0 next to one above 0; a type change (phase of
  another weather type) is silent. There is no test beyond "local player, weather-affected".

### 9.4 AnimationSFXMaps.txt rows [H]

- Load (0x4770a0): tab / space separated. `SOUND <anim> <time> <event>`: anim and event names are
  upper-cased and CRC'd; time = integer ÷ 100 (so `1032` = 10.32 animation time units [M: frames]); rows
  whose SFX event does not exist or whose time is negative are dropped. `SURFACE <anim> <time> <fx> <bone>`
  rows go to 0x476e10. Rows are stored per animation (CRC map, in file order).
- Trigger (0x4775d0), from the object service 0x3a6b80 once per sim tick for objects with a model and not
  frozen (obj +0x3a7 = 0): the model's animation is advanced to the sim time (frame ÷ FPS); prev = time
  before, cur = time after. Nothing when either is negative or they are equal. If cur > prev, every row
  with prev < t ≤ cur fires; if cur < prev (the clip looped), every row with t > prev or t ≤ cur fires.
  So a row at exactly 0 fires only on a wrap, not on the first play.
- Also from `Set_Animation` (0x3a92f0) when it starts a clip at a start frame > 0 with its "fire skipped
  events" argument set: rows in (0, start] fire at once.
- SOUND rows start the event **attached to the animating object** (0x2d5320: 3D-attached if the event is
  `Is_3D`, else 2D-attached; the position-only path 0x2d5240 is used only when no object is passed, which
  these two callers never do). So the normal §1.2 gates apply (`Max_Instances`, `Overlap_Test`,
  `Probability`), there is **no fog or owner test at start** (object-attached), and fogged or hidden
  objects are muted by the §1.6 visibility factor. No per-object limit beyond the event's `Max_Instances`.
- SURFACE rows spawn the surface effect at the named bone (bone index resolved once and cached; surface
  FX manager 0x470700).

### 9.5 Hard point and weapon fire sounds [H]

- Hard point fire (`HP_Fire_At` 0x3825b0), right after the projectile is spawned, unless
  `Test_Disable_Weapon_Fire_SFX`: if the hard point type has a resolved `Fire_SFXEvent` (hard point type
  +0x240 [M: the resolved slot of the +0x140 name]) it is played; **otherwise** the parent object's type
  `SFXEvent_Fire` (+0x1240). Only one of the two plays; with neither, nothing. Either is skipped here when
  the hard point's `Projectile_Appearance_Delay_Frames` (+0x4a4) is > 0 (delayed shot; the sound then
  follows the delayed appearance, TACTICAL_COMBAT §2 step 11 [M]).
- It is **3D at a fixed position**: the new projectile's position (the fire bone / muzzle), through
  0x2d5240 with the parent object passed along — not attached to the ship or the hard point. So the §1.2
  fog test applies (a fogged muzzle position = not started).
- Unit weapons (WeaponBehavior 0x54d090): the shooter type's `SFXEvent_Fire` (a variant's `SFXEvent_Fire`
  when type +0xc48 > 1, via 0x373670 [M: variant lookup]), same Test flag, also 3D at the projectile
  position.

### 9.6 Ability, hyperspace and shield SFX [H unless tagged]

| tag | lives on | when | how |
|---|---|---|---|
| `SFXEvent_Activate` | the ability entry inside the unit's `<Abilities>` block (e.g. `Redirect_Blaster_Ability`, `Stealth_Ability`, `Drain_Life_Ability`; ability data +0x148) | activation presentation 0x48e4d0 (light, particle, sound), called from the ability apply paths (73 references, e.g. after `Begin_Effect` 0x48cca0) | event has `Is_2D` (+0x2a): 2D, not attached (0x2d5290); else attached to the **user** (0x2d5320: 3D-attached when `Is_3D`) |
| `SFXEvent_Target_Affected` | same ability entry (+0x158; mostly `Force_Healing_Ability`) | same call, when there is a target | same rule, attached to the **target** |
| `SFXEvent_Deactivate` | **does not exist** (no parser field, no data) | — | — |
| `SFXEvent_GUI_Unit_Ability_Activated` / `_Deactivated`, `SFXEvent_Target_Ability`, `SFXEvent_Special_Ability_Loop` | the unit type's `<Unit_Abilities_Data><Unit_Ability>` (+0x180, +0x190, +0x170, +0x1a0) | GUI toggle feedback and targeting [L: by name]; a 2D play of a +0x190 slot in 0x307190 and an object-attached play of a +0x1a0 slot in an ability class (0x7003d0) look like `_Deactivated` and the loop [L: struct not confirmed] | not traced further |
| `SFXEvent_Arrive_From_Hyperspace` | **faction** of the arriving object's owner (+0x6b0) | arrival timer 35 (reinforcement arrival 0x626c80, state 4) or 45 (`Cinematic_Hyperspace_In`, state 0x2c, 0x6269f0); also 0x5cbc10 / 0x5cc970 | attached to the arriving object (0x2d5320) |
| `SFXEvent_Retreat_Engines_Jump` (unit type +0x1270), else `SFXEvent_Exit_Into_Hyperspace` (owner faction +0x6c0) | type, then faction | departure state 0x28 at timer 90 (0x5cd680; `Hyperspace_Away`, cinematic retreat); battle retreat jump-out 0x533ac0 per unit, skipped for objects with obj +0x3a0 bit 0x40 or 0x80 | **3D at the object's position** (0x2d5240), not attached |
| `SFXEvent_Shields_Go_Down` (type +0x1290) | unit type | shield state change to down (2) in 0x549810, on the transition only | attached to the object (0x2d5320) |
| `SFXEvent_Shields_Go_Back_Up` (type +0x12a0) | unit type | shield 2 → 1 (first regen tick above 0) in 0x549810 | attached to the object |

There are no `SFXEvent_Shield_*` or `SFXEvent_Hyperspace_*` tags. The vanilla data sets neither shield
tag (no samples in the field table), so shields are silent in vanilla. HARDPOINT_DISABLED.md calls them
`SFXEvent_Shield_Off` / `_On` [M]; the parser names are the ones above.

### 9.7 Tactical intro speech (0x364b20, from the common tactical tick 0x3639d0) [H unless tagged]

The tags are **Faction** fields (Factions.xml), not planet fields. Every tick of a tactical mode until a
one-shot flag (mode +0x2d0) is set:

1. Stop for good (flag set, nothing played) when GMM +0x1e3 is 0 or the mode has no galactic parent
   (0x28aa00): **no intro in skirmish / multiplayer**, and none in a story linked battle (GMM +0x1e3 is 1
   from the GMM constructor 0x16fa0, cleared by the linked-battle setup 0x28f590 and set back to 1 when a
   state-9 linked battle ends, 0x2a7580).
2. Wait (retry next tick) while a cinematic camera is active, while the mode frame (mode +0x10) is below
   10 × FPS (**10 s of sim time after the battle starts**, i.e. after the load screen), or while the speech
   queue is not empty.
3. Set the flag. Need a local player with a faction that is the battle's attacker (0x28adf0) or defender
   (0x28aec0), the conflict record (0x28a7a0; +4 attacker id, +8 defender id) and both factions; else
   nothing.
4. Pick the first that exists, for the **local player's faction and side** (only the local player hears
   it):
   1. Conditional lists (0x35db50): land `..._Land_Attacker/Defender_Conditional_Or` then `_And`, space
      the `_Space_` ones. Each entry has three object types A, B, C and a speech event. **Or**: A or B
      present and C absent; **And**: A and B present and C absent. "Present" = an object of that type is
      at the battle's galactic planet (0x35d930: fleets in orbit and their contents; 0x35d800: that, then
      the ground forces) [M for the orbit / ground split], any owner. First matching entry wins, Or list
      before And list.
   2. Land only, the parent mode is galactic and the local player owns fewer than 2 planets (0x4b2080):
      `SpeechEvent_Tactical_Intro_Land_Attacker_Last_Location` / `_Defender_Last_Location`.
   3. Land only, the conflict record is a raid (record +0x50): `..._Land_Raid_Attacker` / `_Defender`.
   4. Default: land attacker `..._Land_Attacker` (**only when the defender's faction `Is_Playable`**,
      faction +0x20a8; otherwise nothing at all), land defender `..._Land_Defender`, space
      `..._Space_Attacker` / `_Space_Defender`.
5. Queue it (0x321170, FIFO). If the faction's `Tactical_Intro_Command_Bar_Movie_Name` (+0x2f90) is set and
   the HUD accepts the movie (0x2fc7a0), mode +0x2d4 := a value from the speech event (+0x20) [M].

**Corruption mission commencing** is not part of this: it plays at the **galactic** moment the corruption
battle is initiated (space conflict 0x2af7c0, land invasion 0x2b0520), when the initiating player is local
and is the pending corruption-mission player (0x140b155d4). The speech is the initiating unit type's
`Corruption_N_Mission_Commencing_Speech_Event` whose `Corruption_N_Type` equals the pending corruption type
(0x140b155d8; 0x371450 checks N = 1, 2, 3); before queuing it the SFX and speech conversations stop and all
speech is stopped. If the type has none, the local faction's `SFXEvent_Tactical_Corruption_Mission_Commencing`
(+0x650) plays 2D instead (on land it replaces `SFXEvent_Land_Invasion_Commencing`, +0x640).

### 9.8 Base-under-attack announcement (slot 76, 0x3ba180 land / 0x4d8ae0 space) [H]

Second half of the combat notification, (attacker, victim):

- Only when the countdown (land mode +0x414, space +0x3c4) is 0, both objects exist, and the attacker is
  **hostile** to the victim's owner (0x39a4e0).
- The victim must be a base object: land: it has the `DUMMY_GROUND_STRUCTURE` behaviour (obj +0x391) or
  `DUMMY_LAND_BASE_LEVEL_COMPONENT` (+0x394); space: `DUMMY_STAR_BASE` (+0x38e) or
  `DUMMY_ORBITAL_STRUCTURE` (+0x392). Units, and turrets without those behaviours, do not count.
- Owner: the local player, **or** an ally of the local player (0x2823e0) when the victim's type has
  `Is_Community_Property`.
- Then: countdown := FPS × `Delay_Between_Land_Base_Attack_Announcement_Seconds` (space: `..._Space_...`;
  vanilla 60 → 1800 ticks), set **before** the tag test. If the local faction has
  `SFXEvent_Land_Base_Under_Attack_Announcement` (+0x6e0; space `SFXEvent_Space_...` +0x6d0) it plays 2D,
  not forced (0x2d5290), and a radar ping (0x2de700, size 4.0) is placed at the victim's position.
- The countdown drops by 1 per mode tick (0x3bb440 / 0x4d95a0), so the delay counts from the last
  announcement in sim ticks and scales with game speed. Hits during the countdown are dropped, not queued.
