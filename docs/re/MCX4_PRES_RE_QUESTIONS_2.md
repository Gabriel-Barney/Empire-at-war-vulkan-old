# mcx4-pres: second batch of RE questions (t13 / t14 / t15), 2026-11-18

Open rows and implementation details that `MCX4_T13_RE_ANSWERS.md`, `MCX4_T14_RE_ANSWERS.md` and
`MCX4_T15_RE_ANSWERS.md` did not settle. Answers go to `docs/re/MCX4_PRES_RE_ANSWERS_2.md` (behaviour in words, [H] /
[M] / [L], RVAs; no decompiled code). Each question names what the port needs to build it.

## Q1 Countdown timer column (0x3ce60 retreat, 0x3d2b0 build timers, drawn from 0x2ee40 / 0x3c9e0)

Rows: GameConstants `GUI_Tactical_Countdown_Timers_Screen_X/_Y/_Spacing`, `GUI_Strategic_Countdown_Timers_Screen_*`,
Faction `Land/Space_Retreat_Countdown_Text_ID`, `Land/Space_Retreat_Countdown_Color_RGBA`, GameObjectType
`Build_Countdown_Text_ID`, `Build_Countdown_Text_RGBA`.

1. Which font draws the timers: `Game_Play_UI_Countdown_Font_Name` / `_Size` (GameConstants +0x3d0 / +0x3f0), another
   font, or a component font? Size, outline / shadow, alignment (left at X?).
2. Retreat countdown 0x3ce60: who sees it - only the local player while their own retreat runs, or also while an enemy
   retreats? What number is shown (seconds left, rounded how) and how is it joined to the label (a `%` / `##`
   placeholder in the text, or label then "M:SS" as in 0x3d2b0)? Seconds zero-padded?
3. Build timers 0x3d2b0: the exact text "label M:SS" (separator, padding); is there an owner / visibility filter after
   all; order of the timers in the column relative to the retreat timer.
4. Is X a fraction of the screen width from the left edge and Y a fraction of the height from the top edge (top of the
   text or baseline)?

## Q2 Tactical win / lose banner (0x365150, from the victory declare 0x343b90)

1. When is the banner removed: mode exit, a timer, the battle-end dialog, the next banner only?
2. The draw call 0x25b070 arguments after the font: outline / shadow / scale flags.

## Q3 Unit_Ability.SFXEvent_Special_Ability_Loop (record +0x1a0)

The T13 "Needs Ghidra" bullet: a typed cross-reference of the Unit_Ability record (type +0x1ab0 vector entries, the
per-object slot pointers obj + (ability + 0x3d) x 0x10) for any read of record +0x1a0; likely in the special-ability
apply paths 0x48c000..0x492000 and the tractor beam / Death Star II fire abilities. If no read exists: retail-unused.

## Q4 Retreat refusal lines (Faction `*_Retreat_Not_Allowed_SFXEvent`, `_Reason_1/2/3_SFXEvent`, `*_Retreat_Cancel_SFXEvent`)

1. 0x340920: the refusal codes 3 / 4 (T13 "Optional checks") and which code maps to Reason_1 / _2 / _3.
2. The land retreat cancel argument in 0x534f40: what cancels a running retreat (a second b_retreat click, a Lua
   call, units returning?) and who hears `*_Retreat_Cancel_SFXEvent`.

## Q5 T14 rows still open (11)

GameObjectType `Include_In_UI_Map_Header` (+0xb2), `Name_Adjust` (+0x65c), `TSW_Post_Music_Wait_Frames` (+0x21a8),
`Planet_Ability_Name` (+0x2250, the immediate in 0x51a280); HardPoint `Tooltip_Text` (+0x120); RadarMapSettings
`Land_Is_Guide_Rectangle` (+0x130), `Space_Is_Guide_Rectangle` (+0xc0), `Use_Event_System` (+0x51d) (radar map object
0x140b2bcb0); Unit_Ability `Alternate_Name_Text` (+0x1c0), `Alternate_Description_Text` (+0x1e0) (ability button
tooltip); CommandBarComponent `Scale_Duration`. For each: reader and effect, or retail-unused.

## Q6 T15 rows still open (24)

GameConstants `Enemy_Color` / `Player_Color` (faction +0xe8 / +0xf8 after the copy in 0x33a580: any reader of those
faction slots); GameObjectType `Apply_Y_Turret_Rotate_To_Axis` (+0x5b0), `Apply_Z_Turret_Rotate_To_Axis` (+0x5ac),
`Particle_Attach_To_Collision` (+0xb7), `Pre_Lit` (+0x25), `Highlight_Blob_Material_Name` (+0x348; hits 0x27f9a0,
0x3ea1b0, IonCannon, LuckyShot), `Land_Terrain_Model_Mapping` (+0x220, type 0x34); Decal `Category` (+0x40); Flare (6
rows, alLensFlare); LightSource (6 rows, alLightSource); LightningEffect `Replace_Faded`, `Texture_Repeat`,
`Texture_Scroll`. For each: reader and effect, or retail-unused.

## Q7 Ability light pulses (base special ability table 0xa3d690; T15 "Force_Healing_Ability.Owner_Light_Effect_Type family")

The port builds the reader at the real owner. Needed: the pulse shape of the object light effect started by
0x39f2a0 (type, colour, colour2, duration, pulse count): is it a model colour multiply or an added light, how colour
blends to colour2 over one pulse (sine / triangle / step), and what type values 0 / 1 / 2 mean.
