# Q6 t27: when the galactic end screen appears after an elimination (static read, 2026-11-15)

Question (open item of `Q6_T15_RETAIL_RUN.md`, "q6 t25"): ELIM D (human owns nothing, the script
gives a neutral planet to the Rebel AI) showed Defeat at most 1.0 s after the owner change; ELIM E
(human owns one planet, the script makes it Neutral) showed it 4.3 to 5.3 s after. What adds the
~4 s?

Binary: FoC Steam x64 `StarWarsG.exe`. Addresses are image addresses (base `0x140000000`). Static
read only; no game run, no trace. Earlier readings this builds on are cited, not repeated:
`BKC1_RE_ANSWERS.md` Q3, `Q6_T15_RETAIL_RUN.md`, `BATTLE_TRANSITIONS.md` §3.1a.

## Results

| Question | Answer |
|---|---|
| Which victory test fires in D and in E | The same one: the Rebel AI's implicit type 1 (`Opponent_Controls_No_Planets`), tested inside the owner-change call. A change to Neutral takes no other path [H]. |
| When is it evaluated | In the frame of the owner change, inside `0x1403fb040`. No timer, no period, no later re-test [H]. |
| Delay between a passed test and "monitor ready" | The record's countdown: 30 logic frames, for D and E alike [H]. |
| Delay between "monitor ready" and the end screen | None by itself, but the main loop shows the end screen only while **no message box is up and no Bink movie is open** [H]. |
| What adds E's ~4 s | **Not settled.** It is not in the victory monitor. It is one of the two main-loop gates (or the monitor's hold counter) being closed for 99 to 131 frames. The candidate found is the looping `Light_Loop` Bink that the GUI opens while an advisor hint text or an encyclopedia pop-up is shown [M that this gate exists and works this way; L that it is E's cause]. It does not explain by itself why D was not delayed too. |

Frame arithmetic from the probe records (`tools/probe_mod/q6_t15_probes/out/`): D's owner change
and last heartbeat are at f = 659, the next heartbeat (due about f = 691) never came, so D ended
within 32 frames: consistent with 30 frames and open gates. E's take is at f = 1237, heartbeats
follow at 1270, 1301, 1333, 1366 and none at about 1398: E ended 129 to 161 frames after the take,
that is 99 to 131 frames after the countdown ran out.

## 1. The owner change and the tests are the same for D and E [H]

- Lua `Change_Owner` (`0x1403f7280`) returns without effect when the planet already belongs to the
  given faction. Otherwise it clears the planet's conflict, starbase and space build queue, and
  then calls the owner setter `0x1403fb040` with full control (1.0).
- One difference before that call: **when the planet's old owner is the local player**, two
  local-player steps run (`0x1402ba6c0`: each local object in orbit that is not dying is ordered
  to a friendly neighbouring planet, with a fallback chain ending at the same planet;
  `0x1402ba850`: ground forces retreat). Otherwise `0x1404b7670` / `0x1404b7860` run. This is the
  only D / E difference found in the owner-change path. It moves units; no hold, message box or
  movie call was seen in it [M: the two functions were read, their callees only in part].
- In `0x1403fb040` the derived owner (planet data `+0x6C`) switches **at once** when the new
  target owner differs from the old one, also when the new owner is Neutral. The control ramp
  (`0x1404adab0`, `Political_Control_Change_Time_Seconds`) changes the control value only; it does
  not postpone the owner switch on this path.
- Because the owner changed, the same call then tests, for every player in list order,
  `0x14028f570(mode manager, type, player)` for types 0, 1, 4, 5, 9 and stops at the first pass.
  There is no branch on the new owner being Neutral before or around the tests.
- Type 1 (`0x140344710`, case 1) reads each planet's derived owner and is blocked only by an enemy
  playable owner (with the AI / multiplayer skips of `Q6_T15_RETAIL_RUN.md`). After D's and after
  E's change no planet has a human owner, so the Rebel's implicit record passes in both.
- A pass declares at once (`0x140343b90`): countdown := the record's value (30), a log line, one
  GUI call (`0x1402f8c90`), and nothing else for galactic types (no win / lose text, no end-wait
  flag).
- Differences in the tail of `0x1403fb040` that do not touch timing: the "planet changed hands"
  text is queued as an advisor hint when `0x140b31410` is set; the "planet lost" speech plays only
  when the old owner is local **and** the new owner's faction is playable, so E (Neutral) gets no
  speech.

## 2. From the declared record to the end screen [H]

- The countdown (`0x140344670`, from the common tick `0x1403639d0`) falls by the logic frames
  since its last call. "Ready" (`0x140342dc0`) = hold counter (`+0x5c`) below 1, a record
  declared, countdown below 1.
- If the hold counter is above 0 when a test is asked for, the test is queued and answers "no"
  (`0x140341f40`); it runs when the hold is released (`BATTLE_TRANSITIONS.md` §3.1a). No hold
  raise was found on the Lua owner-change path.
- The main loop (`0x14005d990`, after each logic frame) shows the end screen (`0x140092bb0`) when
  the monitor is ready **and** either the game is multiplayer or both of these are zero:
  - `0x140a61e60`: a message box is up;
  - `0x140b30690`: the one global Bink handle (set by the movie open `0x14044d920`, cleared by
    `0x14044f130`; a non-looping movie is closed at its last frame by `0x14044dea0`, a looping one
    only by its owner).
- `0x140092bb0` itself has no wait: it returns early only when the screen is already shown, in
  multiplayer, or without a winner.

So a ready monitor waits for as long as any Bink is open or a message box is up, and for nothing
else.

## 3. What opens a Bink in the galactic view without a script asking for one [M]

- Advisor hint text. `0x1402dd630` queues {text, sound, colour} per game mode. Callers include the
  "planet changed hands" text in `0x1403fb040` and Lua `Game_Message` (`0x1406a33e0`, which
  inserts at the front and resets the hint timer). The GUI update (`0x14030c3b0`, per rendered
  frame) shows the next queued text (`0x1402e4260`) when no text is up, **no Bink is open**, and
  three GUI flags allow it (`+0x26a4` set, `+0x2a34` and `+0x2908` clear). The text stays for
  **(number of characters) × `Advisor_Hint_Duration`** seconds (0.1 in FoC `GAMECONSTANTS.XML`),
  then fades out by 8 alpha steps per update.
- While a hint text is shown (GUI `+0x26b2`) or an encyclopedia pop-up is shown (`+0x26b3`, the
  hover tooltip of `HUD_GAPS.md` §2.2), and no GUI movie is active and no Bink is open,
  `0x140305c40` starts the movie `Light_Loop` **looping** (`0x1402fc7a0`). When both flags are
  clear it closes it (`0x14030ac40`). The movie file is in the base game's `movies.meg`.
- A looping `Light_Loop` therefore keeps `0x140b30690` non-zero for the whole time a hint text or
  an encyclopedia pop-up is visible, which closes the main-loop gate of section 2.

## Not settled

1. **Why D was not delayed [L].** D's script also calls `Game_Message` right after the owner
   change (57 characters, 5.7 s by the rule above), and the "planet changed hands" text may be
   queued in both runs. By section 3 as read, D's end screen should have waited too, and E should
   have waited 8.2 s for its 82-character message, not 4.3 to 5.3 s. So either the hint was not
   shown or `Light_Loop` not open in these runs for a reason not read (the three GUI flags, the
   hint component missing, the Bink failing to open), or E's wait has another cause.
2. **Other candidates for E's wait, not excluded [L]:** an encyclopedia pop-up from the resting
   mouse cursor (it opens `Light_Loop` the same way; with the "delay tooltips" option its delay is
   4000 ms); a consequence of the E-only fleet retreat of section 1; a message box.
3. E's 99 to 131 extra frames would fit one hint text of roughly 33 to 43 characters plus fades.
   That is arithmetic only; the text of `TEXT_PLANET_CHANGED_HANDS` was not read [L].
4. Whether `0x140b31410` (gate of the "planet changed hands" text) was set in the probe runs.

Probe measurements that would separate these (same kit, modes D and E):

| Run | If the end time ... | Then |
|---|---|---|
| E without the `Game_Message` after the take | drops to about 1 s | the Lua message's hint text is the cause |
| D with a 150-character `Game_Message` right after the give | grows to several seconds | the hint gate is real and scales with text length; D's short time needs another reason |
| E with no fleet at the planet | drops to about 1 s | the retreat is involved |
| E and D with the cursor parked over empty space, and again over a planet | changes with the cursor | the encyclopedia pop-up is the cause |
| any run, a screenshot every 0.5 s after the event | shows a hint text box / the comm-window light until the end screen | confirms the gate directly |

Heartbeats every 0.25 s instead of 1 s would make each of these readings exact to a few frames.

## What this means for the port

- Tests, order and countdown need no change: every galactic win is ready 30 logic frames after the
  owner change that passed its test, for an AI gain and for a change to Neutral alike [H].
- Retail rule to model [H]: the end screen is shown in the first frame in which the monitor is
  ready and no message box is up and no movie is open. The port's 30-frame VictoryClaim matches D.
- E's extra ~4 s is presentation (a gate held by a GUI movie or a message box), not simulation
  state: it should not be added as a rule constant. Until item 1 is settled by a probe, any
  number for it would be a guess.

## How this was read

Ghidra 12.1.4 through the ReVa MCP server 7.3.2 on the kept project (`toolchain\re\reva-work\foc`,
`/StarWarsG.exe`), driven over stdio. Decompiles and cross-references gave sections 1 and 2; a
whole-image instruction scan by field displacement (ReVa `run-script`) found the readers and
writers of the GUI flags in section 3. Probe records and screenshots of the two q6 t25 sessions
and FoC `GAMECONSTANTS.XML` / `MOVIES.XML` supplied the frame numbers and constants.
`re_reference/` was not used.
