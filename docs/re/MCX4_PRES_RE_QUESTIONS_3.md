# mcx4-pres: third batch of RE questions (t13 / t14 / t15), 2026-11-18

Open presentation rows that `MCX4_T13/T14/T15_RE_ANSWERS.md` and `MCX4_PRES_RE_ANSWERS_2.md` did not settle far enough
to build. Answers go to `docs/re/MCX4_PRES_RE_ANSWERS_3.md` (behaviour in words, [H] / [M] / [L], RVAs; no decompiled
code). Each question names what the port needs. A row the exe never reads: say **retail-unused**; read but no effect:
**read, no effect**.

## Q1 GameObjectType.Camera_Aligned (+0x2f; object update 0x3989a0 -> 0x29bfa0)

T15 says the model turns to face the camera each update [M]. The port needs the rotation: a full billboard (the model's
axes = the camera's), or a turn about the world Z axis only (yaw towards the camera position / view direction)? Which
model axis points at the camera (+X forward, -Y, ...)? Does it use the camera position or the view direction? Land and
space alike? Does it change the object's sim facing or only the drawn transform?

## Q2 GameConstants.Num_Structures_For_Medium_Planet_Name / _Large_ (planet labels 0x3ea1b0, label virtual +0x1c0)

T14 settles the class pick (structure count below 3 -> 0, below 5 -> 1, else 2). What does the label do with the class:
font size per class (which sizes / font), a different font, a scale on the text, a different component? Which planet
label is it (the galactic planet name under the planet)?

## Q3 GameObjectType.Highlight_Blob_Material_Name (+0x348; SelectBehavior 0x3c19c0 / service 0x3c2310)

PRES_2 Q6: drawn with that material, tinted with the owner's Faction `Selection_Blob_RGBA`. When is it drawn: always
for an object of the type (FoC sets it on heroes), only while selected, only while hovered, only for the local
player's / friendly objects? Is it drawn as well as, or instead of, the normal selection blob? Size (Select_Box_Scale?)
and land / space shape?

## Q4 GameConstants.Tool_Tip_Small_Font_Name / _Size (tooltip renderer 0x140a286f0 +0xc8)

Which tooltip text uses the small font (a second line, the cost line, the hotkey, the encyclopedia body)? Name the
drawing function and the text it draws with +0xc8.

## Q5 Command bar icon rows: GameConstants Icons_Per_Column, Min_Accuracy_For_Icon, Min_Sight_Range_For_Icon

Readers and effect (no spec yet). Which panel lays out icons per column; what do the two thresholds gate (an icon
shown when the unit's accuracy / sight range is at least the value?) and where?

## Q6 Movie rows: Movie.Overlay, Movie.Pause_Game_While_Playing, Movie.Movie_Start_Frame

Readers in the Movies.xml loader / player. What does Overlay do (drawn over the game, a texture name, a flag)? Does
Pause_Game_While_Playing pause the sim only while an in-game (command bar / hologram) movie plays? Movie_Start_Frame:
the first frame decoded / shown?

## Q7 GameObjectType.User_Bound_Min / User_Bound_Max, Should_Death_Clone_Play_Idle

Readers and effect (no spec yet). User_Bound_*: a box used for picking / selection / culling? Should_Death_Clone_Play_Idle:
the death clone's animation choice (idle instead of the death animation?).

## Q8 TacticalCamera.Intensity / Radius / Diffuse

The ledger lists these under TacticalCamera (FoC data: 20 / 19 / 13 uses). Which XML element really holds them (a
light / flare block inside the camera files?), who reads them, and what they do.

## Q9 Faction.Minimum_Visible_Base_Level, Standalone_Space_Maps_Special_Weapon_A / _B

Readers and effect (no spec yet).

## Q10 GameConstants.GUI_Flash_Level, GUI_Rapid_Flash_Duration, GUI_Planet_Flash_Level

Readers and effect: the flash brightness of a flashing command bar button / planet, and the rapid flash's duration
(next to the settled GUI_Flash_Duration / GUI_Planet_Fade_Duration).

## Q11 Land bombard targeting (Faction.SFXEvent_Bombard_Cancelled; 0x2e7bc0, HUD cancel 0x2f6c30)

T13: the cancel line plays when bombard targeting is toggled off. How does the land `b_special_weapon` (or hotkey)
choose bombard targeting (0x2e7bc0) over bombing-run targeting (0x2e7950): by the local faction, by which manager
(PlanetaryBombardManager / BombingRunManager) is available, or both buttons? What does a click in bombard targeting
send (command id, arguments)?

## Q12 GameObjectType.Radar_Rotate_Icon (+0xd6; blip builders 0x35b4d0 / 0x35c000)

T14: the blip icon turns with the object's facing. FoC sets it on the Empire space stations (i_radar_station2..5_e).
The port needs the angle: the object's world yaw as is, or with an offset (which icon direction is "forward": up / +Y
of the texture, or right / +X)? Does the radar itself rotate with the camera (so the angle is relative to the camera
yaw)? Sign convention (counter-clockwise on screen for a positive world yaw?).
