# Story reward handlers: presentation, GUI, camera and control rewards (retail FoC)

Behavioural spec of the story-event rewards that show text, flash or point at things, open dialogs, lock or unlock
controls, move the camera or drive the advisor. Recovered from the retail `StarWarsG.exe` (Ghidra and REA, 2026-11-16).
Behaviour only; no decompiled code is copied. RVAs and struct offsets are pointers for whoever re-checks the binary.

Confidence tags: **[H]** read directly in the code and consistent with shipped data; **[M]** read but one link is
inferred (a reader or a constant was not traced to the end); **[L]** guess or not traced.

Companion documents: `STORY_REWARDS_GALACTIC.md` (common frame, event gate 0xb30350 +0x40, DISABLE_EVENT / ENABLE_EVENT,
TUTORIAL_PLAYER), `STORY_PRESENTATION_LUA.md` (Lua side of the story presentation), `Q6_T27_ELIM_END_TIMING.md` (section 3:
hint queue and show flags).

## 0. Common frame

- The dispatcher is RVA 0x456970. It switches on the reward enum at event +0x3c and is called from RVA 0x455db0 when a
  story event fires. [H]
- Parameters P1..P10 are strings at event +0x50, +0x70, +0x90, +0xb0, +0xd0, +0xf0, +0x110, +0x130, +0x150, +0x170
  (length of the given text 0x10 further on). An empty P means "not given". Event +0x40..+0x48 is the event's own
  position vector. Event +0x228 is the plot, plot +0x638 the plot player, plot +0x630 the story-mode object. [H]
- **The dispatcher itself has no player test and no mode test.** Every presentation reward below acts on the machine's
  own HUD / camera regardless of which player the plot belongs to. A reward that needs a player reads the plot player
  (plot +0x638). Whether a reward does anything is decided only by whether the HUD mode is set and by the handler's own
  tests. [M: negative finding from the dispatcher; the plot set only decides when the event can fire]
- HUD mode (HUD manager `DAT_140b27f60`, +0x211c): 0 = not set, 1 = galactic, 2 = space, 3 = land. Nearly every handler
  returns without effect when it is 0. Per-mode data inside the HUD is keyed 0..3. [H]
- Game mode (`DAT_140b15418`, vtable +0xe0): 0 galactic, 1 space, 2 land. Used for the per-mode advisor flag. [H]
- **Which plot set fires which reward** (galactic campaign plot versus tactical battle plot): the handlers do not care.
  Which shipped XML uses which reward was not tabulated in this pass. Each section below therefore states the GUI mode
  the handler works in ("works in") and the player it uses, and leaves the shipped-plot distribution open. [L]
- Story-manager flags used below live in the object at `DAT_140b30350`; globals named by their address are single bytes
  or words in the exe's data section.

Reward enum values (records at RVA 0xa29140, read from the exe) for the rewards in this file:

| value | name | | value | name |
|---|---|---|---|---|
| 9 | SFX | | 0x50 | MULTIMEDIA |
| 10 | SPEECH | | 0x52 | PAUSE_GALACTIC_GAME |
| 11 | FLASH_GUI | | 0x5a | ENABLE_OBJECTIVE_DISPLAY |
| 12 | FLASH_PLANET_GUI | | 0x5b | FLASH_FLEET_WITH_UNIT |
| 13 | FLASH_PLANET | | 0x5c | SELECT_PLANET |
| 14 | HIDE_TUTORIAL_CURSOR | | 0x5d | FORCE_CLICK_GUI |
| 15 | SCREEN_TEXT | | 0x5e | FLASH_PRODUCTION_CHOICE |
| 20 / 21 | ZOOM_IN / ZOOM_OUT | | 0x61 / 0x62 | PAUSE_GALACTIC / UNPAUSE_GALACTIC |
| 22 | PAUSE_GAME | | 0x64 | FLASH_SPECIAL_ABILITY |
| 23 | TUTORIAL_DIALOG | | 0x67 | SCROLL_LOCK |
| 24 | POSITION_CAMERA | | 0x6a | SET_ADVISOR |
| 25 | LOCK_CONTROLS | | 0x6b / 0x6c | START / STOP_CINEMATIC_MODE |
| 26 | SCROLL_CAMERA | | 0x6d | SHOW_COMMAND_BAR |
| 27 | FLASH_OBJECT | | 0x6e | HIDE_AUTORESOLVE |
| 0x24 | FLASH_TERRAIN | | 0x71 | HIDE_CURSOR_ON_CLICK |
| 0x2b | DISABLE_MOVIES | | 0x72 | LOCK_PLANET_SELECTION |
| 0x31 | DISABLE_RETREAT | | 0x73..0x78 | SHOW/HIDE SMUGGLE / RAID / STEAL SLOT |
| 0x32 | FLASH_UNIT | | 0x7f | SHOW_SPECIAL_SLOT |
| 0x34 | DUAL_FLASH | | 0x83 | FLASH_ADVANCED_MAP_OBJECT |
| 0x37 | HIGHLIGHT_OBJECT | | 0x8c | ENABLE_GALACTIC_CORRUPTION_HOLOGRAM |
| 0x45 / 0x46 | ACTIVATE / USE_RETRY_DIALOG | | 0x47 | START_MOVIE |
| 0x49 / 0x4a | COMMANDBAR_MOVIE / STOP_COMMANDBAR_MOVIE | | | |

[H] (table read from the exe).

HUD component ids used below come from the component table (base 0xa174e8, stride 0x48; the id is the table index + 1,
the name is the first string of the entry). Checked against Text_Credits 6 / Text_Credits_Tactical 0xf2,
b_reinforcement 0xe7, Movie_galactic 0x27a. [H]

## 1. SCREEN_TEXT, the shared text queue, MULTIMEDIA, Lua Game_Message, Remove_All_Text, SPEECH

### 1.1 SCREEN_TEXT (0xf, handler RVA 0x4614a0) [H]

Works in: any HUD mode (galactic, space, land). Player: none (local HUD).

| P | meaning |
|---|---|
| P1 | text id (localized string key). Required; if empty the reward does nothing. |
| P2 | seconds the line stays (atof), default 5.0. **-1 means persistent**: no countdown, stays until removed. |
| P3 | optional object type name. The localized text is used as a format string and the type's display name is substituted. |
| P4 | if **any** non-empty value is given, the handler does not add text. It **removes** lines (see below). |
| P5 | typewriter flag (atoi), default 1. 1 = the line is revealed character by character, 0 = shown at once. |
| P6 | optional "r,g,b" colour, split on commas, overrides the channel colour. |
| P7 | channel, upper-cased: HINT, SYSTEM, TASK, ALIEN_SPEECH. Empty or anything else = subtitle behaviour. |

Channels:

| channel | colour (GameConstants) | extra |
|---|---|---|
| HINT | Hint_Text_Color 0 193 91 | plays SFX event "GUI_Text_Hint_SFX" |
| SYSTEM | System_Text_Color 255 24 39 | |
| TASK | Task_Text_Color 109 247 255 | |
| ALIEN_SPEECH | Speech_Text_Color 255 255 255 | |
| none / unknown | Speech_Text_Color | treated as a subtitle: **displayed only when the option IDC_USE_MOVIE_SUBTITLES is on** (option record 0xa7c230, value byte 0xa7c442) |

The four named channels are always displayed. [H] Colour constants resolved from the GameConstants XML and the
(name, type, offset) table at 0xa357b0 (GameConstants struct 0xb15800, offsets 0x1250 / 0x1260 / 0x1270 / 0x1280). [H]
Whether the unknown-channel colour is exactly the Speech colour and whether anything else differs for an unknown name
was not traced beyond the subtitle gate. [L]

Remove (P4 given): walks the whole line list (RVA 0x2ff290) and deletes **every line whose stored text id equals P1**.
In shipped data about 2500 of 2936 SCREEN_TEXT uses are removals. P1 is compared as the text id, not the displayed
text. [H]

Add (RVA 0x2dff30):
1. Removes any existing line with the same text id first, so a repeated id never stacks. [H]
2. Looks the id up in the localized-text manager (`DAT_140a7bc58`; a missing id returns whatever the lookup returns;
   not examined, the engine should show the id itself or nothing). [L]
3. Word-wraps the text to the width of component 0x20e (tutorial_text_back) minus 9 px, using the font of 0x20d
   (tutorial_text). One line object (0x70 bytes) is created per wrapped line. Every wrapped line of one SCREEN_TEXT
   shares the text id, the remaining time, the colour and the persistent flag, so removal by id deletes all of them. [H]
4. Logs "SCREEN TEXT - %s". [H]

Line fields (pointers): +0 text render object, +8 seconds remaining, +0x10 text id, +0x30 wide text, +0x50 revealed
characters (float), +0x54 line index, +0x58 timestamp, +0x5c persistent, +0x5d typewriter, +0x60 colour rgba. [M]

The list is a vector of line pointers on the HUD (+0x1170, count +0x1178). No cap was found. [M]

Per frame (RVA 0x31d750): [H]
- every non-persistent line loses the frame time; at <= 0 it is deleted. There is no fade; alpha is fixed.
- lines are laid out as a column starting at x = HUD +0x10dc + 5, y = HUD +0x10e0 - 10, going down by line height;
  the background panel (component tutorial_text_back, HUD +0x1180) is sized to the block.
- typewriter: the revealed-character count grows at a global rate (`DAT_140b15ab4`, characters per second; value not
  read) [M]; a continuation line starts only after the line above it has finished. A tick sound plays through the
  SFX manager (`DAT_140b27e60`) while revealing, except for a tutorial player (player +0x3c0) in cinematic mode on
  non-skirmish modes. [M]
- while cinematic mode is on, the text width differs (HUD +0xff8). [M]

Persistence: the line list, the arrow slots and the commandbar movie name are written and rebuilt by the HUD save and
load (RVA 0x2fd330 area). [M]

Clear all: RVA 0x2fefc0 empties the list; it is also what the Lua Remove_All_Text calls. [H]

### 1.2 The "shared text queue" is two things

There are two different text areas. Do not merge them. [H]

1. **Screen-text lines** (SCREEN_TEXT, MULTIMEDIA's text, Lua Remove_All_Text): the column described above, with the
   background panel `tutorial_text_back`. Timed per line, no queue, no order guarantee beyond creation order.
2. **Advisor hint queue** (section 6.3): one line group at a time inside the command-bar help component, fed by
   Lua Game_Message, the periodic advisor hint and a few code paths ("enemy fleet approaching", planet changed hands).

SCREEN_TEXT never goes through the hint queue. Lua Game_Message never goes through the screen-text list.

### 1.3 MULTIMEDIA (0x50, RVA 0x4608c0) [H]

Works in: any HUD mode. Player: none. It runs three independent parts in this order:
1. If P1 is given: the SCREEN_TEXT handler on the same event (P1..P7 read as in 1.1).
2. If P8 (speech event name) is given: the SPEECH behaviour with that name (log "STORY REWARD - Speech %s").
3. If P9 (talking-head movie, shipped names end in `_Loop`) is given: COMMANDBAR_MOVIE with movie P9 and loop flag
   `atoi(P10) != 0`; an empty P10 means 0 (not looping).

Each part is optional and independent; none depends on another having run.

### 1.4 SPEECH (0xa, RVA 0x4623e0) [H]

P1 = speech event name. Flushes the speech manager (`DAT_140b2b300`) then queues the new event, so the previous line
is cut off. Works in any mode; no player test. (The SFX reward 9 plays an SFX event by name through the SFX manager,
RVA 0x2d5010. [M])

### 1.5 Lua side [H]

- `Game_Message(text)` (RVA 0x6a33e0): one string, localized, **pushed to the front of the advisor hint queue** with
  white (255,255,255,255) and the faction's default message sound. It is **not** SCREEN_TEXT. It requires
  `DAT_140b31410` non-zero; otherwise Lua reports "no method for text display found".
- `Remove_All_Text()` (RVA 0x543550): clears the screen-text list (1.1, "Clear all").
- `Lock_Controls` (Lua) sets a different flag (`DAT_140a28c91`, a float, locked when > 0) from the story LOCK_CONTROLS
  (section 5).
- `PlayerWrapper:Enable_Advisor_Hints(mode name, bool)` writes the same per-player per-mode byte as SET_ADVISOR (6.1).

## 2. HIGHLIGHT_OBJECT, FLASH_OBJECT, FLASH_UNIT, FLASH_TERRAIN, DUAL_FLASH

### 2.1 HIGHLIGHT_OBJECT (0x37, RVA 0x45f3f0) [H]

Works in: any mode that has an object manager (space, land, galactic). Player: none, but the effect is created for the
local human faction.

- P1 = object type name; the first live instance in the current mode's object manager is used. P2 = atoi, nonzero
  highlights, 0 removes the highlight. P1 and P2 are both required.
- P3 optional integer pointer id (default -1).
- Highlight: spawns the special-effect object `Object_Highlight` (SPECIALEFFECTS.XML, model W_HILIGHT_FX.ALO,
  decoration, no gameplay effect; a `_Small` variant exists in data but this reward never uses it) attached to the
  target. The reference is stored on the object (object +0x1a8 -> +0x30). (RVA 0x398860.)
- If P3 is given and not -1: also creates a 2D tutorial pointer record (RVA 0x2de700) at the object's position
  (object +0x78), duration -1 (persistent), id = P3 (record +0x1c), pointer component id 0x20f (radar_blip, used as the
  pointer quad). [M for what 0x20f draws on screen]
- Un-highlight: removes the effect object silently (RVA 0x3ac0c0, plain removal 0x391310). If P3 is given it also
  removes the pointer record with that id (RVA 0x2ff240).
- No error text when the type has no instance (nothing happens). [M]

### 2.2 FLASH_OBJECT (0x1b) and FLASH_PLANET (0xd) [H]

Both: P1 = type name, first instance, then the object colour-flash mode 2 (RVA 0x392ba0, state block of 0x38 bytes at
object +0x1b0). Mode 2 uses GUI_Flash_Level (3.0, GameConstants +0x11fc) and GUI_Flash_Duration (0.4, +0x1200). Other
modes of the same routine: 3 rapid duration (GUI_Rapid_Flash_Duration 0.1, +0x1204), 4 cycle colour, 5 / 6 darken /
hilite, 7 planet fade duration (GUI_Planet_Flash_Level 10.0, +0x1220). [M for modes 3..7]

- FLASH_OBJECT logs and reports an error when the type is not found ("Unable to find object %s").
- FLASH_PLANET is silent when not found. Works in galactic mode in practice (planets); no player test.

### 2.3 FLASH_TERRAIN (0x24) [H]

P1 = duration (atof). Position = the event's own position vector (event +0x40). Calls the pointer helper (RVA 0x2de700)
with the 3D-arrow flag set, which also places the 3D arrow model (HUD +0x2278 object) at the world position. Works in
tactical modes; no player test.

Port (ref5 t02): StoryPresenter registers the reward but places nothing: the fired reward log (`lua::StoryFired`) does not
carry the event's position [port]. No shipped story uses the reward.

### 2.4 FLASH_UNIT (0x32, RVA 0x2f5a50) [H]

P1 = region name (enum REGION_NONE 0 / ORGANIZE 1 / PRODUCTION 2 / SELECTION 3; unknown name logs "Can't find
region"); P2 = unit type (unknown logs "Can't find unit type"). Finds the HUD button that holds that unit type and
flashes it with an arrow (slot mechanism of 3.2):
- ORGANIZE: component ids 0x79..0xe0, then the pad buttons 0x60..0x69, then structure buttons 0x6a..0x72.
- PRODUCTION: build buttons id 10 .. (count + 9).
- SELECTION: s_select icons starting at id 0x109.
Works in tactical modes (command bar); local HUD only.

Port (ref5 t02): StoryPresenter takes an arrow slot of kind Unit (region and type kept; an unknown region does nothing); the
host does not look the button up yet, so no arrow is drawn [port]. No shipped story uses the reward.

### 2.5 DUAL_FLASH (0x34) [H]

P1 = time, P2 = rate (atof). Sets story manager +0x1d0 := 1 (armed), +0x1c8 := 0 (queued count), +0x1d8 := time,
+0x1d4 and +0x1dc := rate. While armed (`DAT_140b30520`), FLASH_GUI and FLASH_PLANET_GUI **do not flash at once**: they
are stored in the manager's two entries (RVA 0x446180); when the second arrives, RVA 0x447000 alternates the two flashes
at the given rate for the given time. HIDE_TUTORIAL_CURSOR clears the queue (+0x1c8 := 0, +0x1d0 := 0). Galactic and
tactical HUD alike (it wraps the two flash rewards). [M for the alternation timing details]

Port (ref5 t02): StoryPresenter holds the flash part of the next two FLASH_GUI / FLASH_PLANET_GUI rewards (their arrows are
added at once [M]); the second starts the pair: the first, then the second, each for `rate` seconds, alternating until `time`
seconds have passed, then the reward is disarmed [M]. A zero rate flashes the first for the whole time [port]. HIDE_TUTORIAL_CURSOR
drops the held flashes and disarms. The state is saved with the presenter (v2). No shipped story uses the reward.

## 3. GUI flash and pointer rewards, and the "flash id"

### 3.1 FLASH_GUI (0xb, RVA 0x2f50c0 via lookup 0x2f5bf0) [H]

P1 = HUD component name (looked up by CRC through the HUD name map at +0x640); P2 = optional flash id string. The
component is flashed through its vtable (+0x120): 0.5 s per flash, 3 flashes, infinite only when the duration value is
-1. If the component has a screen position (+0xa4 non-zero) an arrow slot is added (3.2). The special component id
0x232 is replaced by the first element of the HUD list at +0x1138. Logs "Flashing command bar component %s". Works in
every HUD mode; the component must exist in that mode's layout. Local HUD only.

### 3.2 Arrow slots and the pointer model [H]

- There are 10 slots at HUD +0x2288 + n * 0x68. Each is a clone of the 3D model `data/art/models/i_tutorial_arrow.alo`,
  loaded once (RVA 0x2fab80, flag +0x2698).
- Fields: +0x10 component, +0x18 target object, +0x28 flash id string, +0x9 / +0xa flags (+0xa = the "arrow" flag, P5
  of FLASH_PLANET_GUI), +0xc HUD mode, +0x20 / +0x24 region and sub-index.
- The arrow is placed beside the component with an offset of -100 (float constant 0xc2c80000).
- **All 10 used: the new request silently gets no arrow** (the flash still happens).
- Slots are saved with the HUD and rebuilt on load.

### 3.3 The "flash id" strings [H]

Strings such as `Han_Solo_Team_Flash`, `CLICK_ZOOM`, `SPACE_PROD_TAB` are **labels only**. They are stored in the arrow
slot and compared (case-sensitive) by HIDE_TUTORIAL_CURSOR. They select no sprite and no model. The retail pointer is
always the 3D model `i_tutorial_arrow.alo`. Separately there is a 2D pointer record (component 0x20f) created by
HIGHLIGHT_OBJECT / FLASH_TERRAIN with a **numeric** id; it is a different list (HIDE_TUTORIAL_CURSOR does not touch it;
HIGHLIGHT_OBJECT with 0 removes it by id).

### 3.4 FLASH_PLANET_GUI (0xc, RVA 0x2f55a0) [H]

Works in galactic HUD mode. P1 = planet type; P2 = region name; P3 = atoi sub-index; P4 = flash id; P5 = any non-empty
value sets the arrow flag. The planet must be in the HUD's displayed planet vector (+0xde0, stride 0x188, planet pointer
at entry +0). **A planet that is not on screen is silently ignored.**

Region enum and effect: [H]

| value | region | effect |
|---|---|---|
| 1 | AFFILIATION | no-op |
| 2 | FLEET | fleet slot, only if P3 < 3 (entry index 4 + P3) |
| 3 | TROOPS | entry 0x26 |
| 4 | PLANET_NAME | entry 9 (flash mode 1) |
| 5 | BASE_BARS | entries 0xb and 0x12 |
| 6 | CREDITS | no-op |
| 7 | PRODUCTION | entry 0x23 |
| 8 | COINS | no-op |
| 9 | SMUGGLER | entry 0x24 |
| 10 | SMUGGLED | entry 0x28 |
| 11 | PLANET_VALUE | entry 10 |
| 12 | WEATHER | entry 10 |
| 13 | PLANET | arrow on the planet object only, no component flash |
| 14 | ABILITY_SLOT | entry 0x29 |

Before flashing it checks the galaxy view state (RVA 0x3d6b70) and may first reset or zoom the view (RVA 0x448c20,
0x3ea1b0). [L for the exact conditions]

### 3.5 FLASH_FLEET_WITH_UNIT (0x5b, RVA 0x45eed0) [H]

Galactic only. P1 = unit type, P2 = optional flash id. Scans the galactic object list for the first object, or unit
inside a fleet or planet garrison, of that type, then walks up to the container: a fleet flashes its fleet icon
(RVA 0x2f5490); a planet gets FLASH_PLANET_GUI region 3 (TROOPS). Not found: nothing.

### 3.6 FLASH_SPECIAL_ABILITY (0x64, RVA 0x2f5990) [H]

Tactical command bar. P1 = ability type name (resolved through vtable +0x20 of `DAT_140b310c0`), P2 = flash id. Finds
the special_button_NN component (first id 0x1a5) bound to that ability, flashes it with an arrow.

### 3.7 FLASH_PRODUCTION_CHOICE (0x5e, RVA 0x2f58d0) [H]

Galactic production panel. P1 = unit type, P2 = flash id. Finds the build button (b_Create_NN, ids from 10) whose bound
type id (type +0xf0) matches and flashes it with an arrow.

### 3.8 FLASH_ADVANCED_MAP_OBJECT (0x83, RVA 0x2f5000) [H]

P1 = object type. Works only while the advanced planetary map is shown (HUD +0x2a34). Scans icon ids 0x2df..0x2ff for
the one bound to the type and flashes it with count 6, **without** an arrow.

### 3.9 FORCE_CLICK_GUI (0x5d) [H]

P1 = component name (CRC lookup). Calls the component's vtable +0x208 with 1: the component runs its own click action.
It **does not lock** other controls. Errors: "incomplete parameters", "invalid component name".

### 3.10 HIDE_TUTORIAL_CURSOR (0xe, RVA 0x2f8dd0) [H]

P1 = flash id. Hides the **first** active arrow slot whose stored name equals P1 (model hidden, slot cleared). With P1
empty it hides the first active slot whose name is empty. Also clears the dual-flash queue (2.5). It does not stop the
component's own flashing (that runs out by itself) and does not touch 2D pointer records.

### 3.11 HIDE_CURSOR_ON_CLICK (0x71) [M]

Writes `DAT_140b2a992 := (P1 != 0)`. **No reader was found in the whole binary**: apparently dead. The port can store
the flag and ignore it.

## 4. TUTORIAL_DIALOG (0x17) [H]

P1 = text id, P2 = atoi (show the Continue button when nonzero). The reward calls story-manager callback 0
(RVA 0x123d70, registered by the story manager init "TheTutorial.Init" at RVA 0x447190 together with the
quit-to-menu routine at RVA 0x5f6b0).

Callback: [H]
1. If the dialog is already up (`DAT_140a62618 == 1`) nothing happens.
2. If cinematic mode is active it ends it first.
3. Creates the modal dialog `IDD_TUTORIAL_DIALOG` (dialog proc RVA 0x1243f0), fills `IDC_TUTORIAL_TEXT` with the
   localized text wrapped into lines, shows `IDC_CONTINUE` only if P2 != 0.

Dialog proc:
- `IDC_CONTINUE`: closes the dialog and **fires the story's generic string trigger "Continue_Tutorial"** (RVA 0x32ed60),
  which is how a plot waits for the click.
- `IDC_DIALOG_CLOSE_BUTTON`: closes the dialog, no trigger.
- `IDC_QUIT_TO_MAIN`: asks `TEXT_QUIT_SURE` (yes / no); yes runs the quit-to-main-menu routine.

No explicit game pause was found in the handler. Whether the simulation keeps running behind the modal dialog was not
confirmed. [M] Works in any mode; local machine only; no player test.

## 5. Control, camera and mode rewards

### 5.1 LOCK_CONTROLS (0x19) [H write, M effect]

Writes `DAT_140b30392 := (atoi(P1) != 0)` only when P1 is given. Readers: the global input dispatcher (RVA 0x30fc0
swallows input), the cinematic-camera trigger (RVA 0x2cc080, refuses), the HUD input handlers (RVA 0x2f5c80 and
0x2f6c30) and the HUD update (RVA 0x30c3b0); the last two clear drag, selection and hover state and the selection
display. Effect: all UI and world input is ignored while set. The flag is also saved and restored by one more routine that
was not identified [L]. Works in every mode. Distinct from the Lua `Lock_Controls`.

### 5.2 DISABLE_RETREAT (0x31) [H]

Tactical. P1 = faction name, P2 = atoi (both required). 0 removes the faction from the list at story manager +0xc8,
nonzero adds it. The reader (RVA 0x446480, called from the player retreat test at RVA 0x2825e0) returns "not allowed"
for listed factions: **that faction cannot retreat**. The list is by faction, not by plot player.

### 5.3 HIDE_AUTORESOLVE (0x6e) [M]

Sets `DAT_140b2a991 := 1`; there is no reader and nothing clears it. Dead in retail.

### 5.4 USE_RETRY_DIALOG (0x46) / ACTIVATE_RETRY_DIALOG (0x45) [H]

USE sets byte +0x78 of the mode sub-object [5] to 1 (marks the retry dialog as the end-of-battle choice). ACTIVATE
calls the check callback (`DAT_140b30720`) and, when it does not return true, the show callback (`DAT_140b30718`).
Tactical modes. No player test.

### 5.5 ENABLE_OBJECTIVE_DISPLAY (0x5a) [H write]

P1 defaults to 1. Writes `DAT_140b2a078` (the visibility of the objectives panel). The same byte is toggled by the key
0x53 in all modes.

### 5.6 SHOW_COMMAND_BAR (0x6d) [H]

P1 atoi != 0. Game mode 0 (galactic): the galactic HUD show / hide routine (RVA 0x30ef20). Space and land: RVA 0x30efc0
together with the hide counter `DAT_140b2c198` (show decrements to a minimum of 0, hide increments; nested hides need
matching shows).

### 5.7 SHOW_* / HIDE_* slot rewards (0x73..0x78, 0x7f) [H]

Galactic. The rewards set (SHOW) or clear (HIDE) a global planet pointer that forces that slot visible on the planet
display: smuggle `DAT_140b30578`, raid `..580`, steal `..588`, black market `..590`, sabotage `..598`, corruption
`..5a0`, remove corruption `..5a8`. SHOW_SPECIAL_SLOT (0x7f, RVA 0x462170): P1 planet, P2 slot-type name (enum
RAID_SLOT 1, SMUGGLE 2, STEAL 3, BLACK_MARKET 4, SABOTAGE 5, CORRUPTION 6, REMOVE_CORRUPTION 7, NEUTRALIZE_HERO 8),
optional P3 (0 clears). Errors: invalid planet, non-existent planet, non-existent slot type. The reader is the
planet-ability slot code (around RVA 0x674314) [M]. The forced visibility is **global**, not per player.

### 5.8 START_CINEMATIC_MODE (0x6b) / STOP_CINEMATIC_MODE (0x6c) [H, details M]

START (RVA 0x2cd010 on the cinematic controller `DAT_140b27a10`): +0x350 := 1, +0x3a8 := 0, saves the cursor
position and, when the cinematic check (RVA 0x2cc780) passes, +0x37c := 3, +0x34a := 1 (the "cinematic active" bit read
by RVA 0x2cd0c0) and +0x348 := 1. STOP (RVA 0x2cbb80): fades the bars object (`DAT_140a28c10`, 0.5 s) if present, ends
the cinematic (RVA 0x49b5c0) and clears the fields. While active: the commandbar movie and the advisor hint show are
suppressed, the text width changes, and the typewriter tick is muted for a tutorial player in some modes. The
letterbox and the full HUD hiding were not traced. [L] Works in tactical modes and galactic alike.

### 5.9 ENABLE_GALACTIC_CORRUPTION_HOLOGRAM (0x8c) [M]

P1 = planet type, P2 defaults to 1. Writes the planet block byte +0x2e4. The byte is read in the planet-ownership
change routine (RVA 0x28bf10): when the changed planet is the local player's current planet and the byte equals 1, the
galactic scene gets a hologram-display call (RVA 0x362510) alongside the normal planet-changed update. What exactly
the hologram shows was not traced. [L] Galactic only; the change routine runs for the local human.

### 5.10 SELECT_PLANET (0x5c) [H]

Galactic. P1 = type name; the first instance is selected in the galactic UI (RVA 0x3de570, as if clicked). Errors
"invalid unit", "non-existent unit". Local HUD; no player test. LOCK_PLANET_SELECTION does not stop this reward, which
selects directly. [M]

### 5.11 SCROLL_LOCK (0x67) [H write, M readers]

Writes the byte at mode +0x2d1 (P1 != 0). The input and camera code (RVA 0x2ee40, 0x39170, 0x390d0, 0x2fdce0,
0x3d1260) skip edge and keyboard scrolling when it is 1. Both camera types. [M]

### 5.12 LOCK_PLANET_SELECTION (0x72) [H]

Writes `DAT_140b2a993`. Readers: the planet-icon click handler (RVA 0x10fd30, three places), the empty-click deselect
(RVA 0x30fc0) and RVA 0x346a10. While set, a click does not select a planet.

### 5.13 ZOOM_IN (0x14) / ZOOM_OUT (0x15) [H]

Call the galactic HUD zoom (RVA 0x3e4660 / 0x3e48a0), the same routines as the player's own zoom, **only when the story
event gate (RVA 0x446220, TUTORIAL_ZOOM event) permits** (see `STORY_REWARDS_GALACTIC.md`). Galactic only.

### 5.14 POSITION_CAMERA (0x18, RVA 0x461080) [H]

P1 = optional type name (empty: the event's position, +0x40); P2 = optional "x,y,z" string added as an offset. Calls
RVA 0x448c20: in galactic mode an instant set of the galactic camera position and refresh; in tactical modes the
tactical camera position setter (x and z). Logs "Positioning camera to (...)".

### 5.15 SCROLL_CAMERA (0x1a) [H write, M mover]

Same target as POSITION_CAMERA but calls RVA 0x44a1a0 on the story manager. It records the current camera (galactic
data in mode 0, tactical camera otherwise), start and target direction, speed = 0.8 x distance (default 42.0 when the
distance is 0), start time and an active flag (+0x70). The per-frame mover was not read. [M] A port can animate the
camera linearly to the target over distance / speed.

### 5.16 Pause rewards [H]

- PAUSE_GAME (0x16): calls the HUD pause callback at HUD +0x27f0 (set by the mode), the same as the pause key.
- PAUSE_GALACTIC (0x61): RVA 0x28bb80 on the galactic time manager (`DAT_140b153e0`): a pause counter at +0x2c; the first
  pause records the time and tells the AI and time system.
- UNPAUSE_GALACTIC (0x62): RVA 0x28c720 with argument 1 (clears the counter).
- PAUSE_GALACTIC_GAME (0x52): P1 given and equal to 0 means unpause; otherwise pause.

### 5.17 DISABLE_MOVIES (0x2b) [H]

Writes `DAT_140b30470 := (P1 != 0)`. The only reader is the START_MOVIE reward (0x47, full-screen Bink, RVA 0x28bc10),
which is skipped when the flag is set or when movies are off (`DAT_140b15af8`). It does **not** affect
COMMANDBAR_MOVIE or the talking-head loop.

## 6. Advisor, command-bar movie and hint queue

### 6.1 SET_ADVISOR (0x6a) [H]

P1 = 0 or 1 (required). Writes the byte at **plot player** +0x3f8 + (current game mode type 0..2): the per-player
per-mode "advisor hints enabled" flag. Logs "Setting on-screen advisor to ON/OFF". It only gates the periodic hint text
(6.4); the talking-head movie is unaffected. The Lua `Enable_Advisor_Hints` writes the same byte (1.5).

### 6.2 COMMANDBAR_MOVIE (0x49) / STOP_COMMANDBAR_MOVIE (0x4a) [H]

P1 = movie name, P2 = loop flag (atoi != 0). RVA 0x2fc7a0:
1. clears any hint text lines first (RVA 0x2f8bf0) **unless the name is "Light_Loop"**;
2. stops the current Bink and opens the new one in the movie player (0xb306dd), with a finish callback (RVA 0x2fb120)
   when it is not looping;
3. positions it on component Movie_galactic (0x27a) in HUD mode 1 or Movie_tactical (0x27b) in modes 2 and 3, offset by
   the CB_Movie_Offset constant; sets +0x26b1 and +0x26d8 (movie visible).
The movie is hidden while cinematic mode or the advanced map (+0x2a34) is active. HUD mode 0 returns without display.
STOP (RVA 0x30ac40) closes the Bink and removes the quad.

### 6.3 Hint queue and show [H]

- **Enqueue** (RVA 0x2dd630; HUD, text, colour, front flag, sound). Requires HUD mode != 0 and non-empty text. The queue
  is a ring deque at HUD +0x2970 + mode * 0x28 (one per mode); entries are 0x38 bytes (wide text, sound, colour rgba).
  back (front = 0) uses the faction sound at faction +0x2f58 (SFXEvent_HUD_Advisor_Message); front insert (front = 1)
  uses +0x2f68 (Urgent) and resets the display timer (+0x2a10 := 0) so it shows at once. The two sound offsets +0x2f68
  and +0x2f78 are inferred from the code, not the table. [M]
- **Per-frame update** (RVA 0x30c3b0): the timer +0x2a10 counts down by the frame time. While > 0 the text fades in
  (alpha steps of 8). At 0, if lines are still shown (+0x2a28 > 0) it fades out by 8 per update; otherwise, with the
  queue non-empty, it calls the show routine.
- **Show** (RVA 0x2e4260). Shows the front entry only when: HUD mode != 0, no Bink movie is open (`DAT_140b30690 == 0`),
  +0x2a34 clear (no advanced map), +0x2908 == 0, the help component exists, and mode 1 has +0x26a4 set (tactical modes
  need +0x26a0 <= 0). It sets +0x26b2 := 1, plays the entry sound, calls the callback at +0x2888, wraps the text into
  the help component (0x26e galactic, 0x26f tactical, with frame and the 0x270 back) and sets the timer
  +0x2a10 = (character count, entry +0x10) x Advisor_Hint_Duration. With Advisor_Hint_Duration 0.1 s the text stays
  characters x 0.1 s.
- **Light_Loop**: RVA 0x305c40 opens the looping movie `Light_Loop` while +0x26b2 or +0x26b3 is set and no other
  movie is up, closes it when both clear (from `Q6_T27_ELIM_END_TIMING.md`). [M]
- Other enqueue users: "enemy fleet approaching" (RVA 0x2854c0; throttle 5000 ms through player +0x3b8; front insert;
  faction sound at +0x6f0), the planet-changed-hands text (RVA 0x3fb040), Lua Game_Message (front, white). [M]

### 6.4 Periodic hint, Advisor_Hint_Interval / Duration, Galactic_Advisor_Hints [H, tick rate M]

Inside the player tick (RVA 0x287e70), for the local human player only, when the galactic-manager state check
(RVA 0x28b010) is true, **if** player +0x3f8[mode] != 0 and the next-hint tick player +0x3e0[mode] <= the mode's current
tick:
1. take the faction's hint text at index player +0x3ec[mode] (RVA 0x33e210) from the faction list for the mode
   (Galactic_Advisor_Hints at faction type +0x2ef0, Land_Advisor_Hints +0x2f08, Space_Advisor_Hints +0x2f20: 24-byte
   vectors of 32-byte strings; count by RVA 0x33e2e0);
2. localize it and enqueue at the **back** with colour (0,193,91,255) and the faction hint sound (SFXEvent_HUD_Advisor_Hint,
   +0x2f78);
3. set next tick = now + (int)(ticks per second `DAT_140b0a340` x Advisor_Hint_Interval + 0.5);
4. increment the index, wrapping to 0 at the list count.

GameConstants: Advisor_Hint_Interval (GameConstants +0x11e38, global 0xb27638, shipped 300) and Advisor_Hint_Duration
(+0x11e3c, global 0xb2763c, shipped 0.1). Default_Transmission_Message is at faction +0x2f38. Enable_Advisor_Hints (the
flag at player +0x3f8) must be on for the periodic hint; SET_ADVISOR and the Lua call set it.

### 6.5 Battle-start advisor line [M for timing, L for source]

The player begin-mode routine (RVA 0x285760, run at mode start) sets player +0x3e0[mode] := 0x5a (90 ticks), so the
first periodic hint is due **90 ticks after the mode starts**, not Advisor_Hint_Interval later. At the rate the tactical
sim runs this is the "about 10 s after battle start" line reported near RVA 0x364b20. The tick rate was not confirmed
(so 90 ticks = 10 s is [M]), and the function at 0x364b20 itself was not read; the periodic hint path above is the
only advisor-line path found that fits. [M / L]

### 6.6 HUD advisor sounds [M]

SFXEvent_HUD_Advisor_Message, SFXEvent_HUD_Advisor_Urgent_Message and SFXEvent_HUD_Advisor_Hint are faction-type fields
(+0x2f58, +0x2f68, +0x2f78). Back-of-queue entries use Message; front inserts use Urgent; the periodic hint uses
Hint. The HINT channel of SCREEN_TEXT plays the unrelated SFX "GUI_Text_Hint_SFX".

## 7. Which player and which mode, summary

| reward | works in | player used |
|---|---|---|
| SCREEN_TEXT, MULTIMEDIA, SPEECH, SFX | any HUD mode | none (local machine) |
| FLASH_GUI, FORCE_CLICK_GUI, HIDE_TUTORIAL_CURSOR, DUAL_FLASH | any HUD mode | none |
| HIGHLIGHT_OBJECT, FLASH_OBJECT, FLASH_TERRAIN, POSITION/SCROLL_CAMERA | any mode with a camera | none (highlight: local human faction) |
| FLASH_UNIT, FLASH_SPECIAL_ABILITY, DISABLE_RETREAT, USE/ACTIVATE_RETRY_DIALOG | tactical | none (retreat: named faction) |
| FLASH_PLANET, FLASH_PLANET_GUI, FLASH_FLEET_WITH_UNIT, FLASH_PRODUCTION_CHOICE, FLASH_ADVANCED_MAP_OBJECT, SELECT_PLANET, SHOW/HIDE slots, ZOOM, galactic pause rewards, corruption hologram | galactic | none |
| SET_ADVISOR | any mode (byte per mode) | **plot player** |
| LOCK_CONTROLS, SCROLL_LOCK, LOCK_PLANET_SELECTION, ENABLE_OBJECTIVE_DISPLAY, SHOW_COMMAND_BAR, cinematic mode, DISABLE_MOVIES, COMMANDBAR_MOVIE | any mode | none |

Which shipped plot set (galactic or tactical story file) contains each reward was not tabulated. [L]

## 8. Open items

- [L] The unknown-channel SCREEN_TEXT colour beyond the subtitle gate; localized-text miss behaviour.
- [M] The SCROLL_CAMERA per-frame mover; the exact LOCK_CONTROLS effect on the camera.
- [M] Whether TUTORIAL_DIALOG pauses the simulation.
- [L] RVA 0x364b20 (battle-start advisor line) was not read; the tick rate `DAT_140b0a340`; sound-field offsets +0x2f68 / +0x2f78.
- [L] What component 0x20f draws; the typewriter tick sound and its rate `DAT_140b15ab4`.
- [M] No reader for HIDE_AUTORESOLVE and HIDE_CURSOR_ON_CLICK (dead in retail).
- [L] What the corruption hologram displays.
- [L] Shipped-plot distribution of each reward by plot set.
