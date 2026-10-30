# Blockfall

A third-person build-and-shoot prototype written in C++17 with Win32 and Direct3D 11. There's no game engine.

You start in a lobby. Pick **Solo, Duos, Trios or Squads**, invite friends to your party, and press **PLAY**. You ride the battle bus over an 800 m × 800 m map with 12 named places, jump, and glide down with your pickaxe, a pistol and 100 materials. Every match has 100 players. Loot up, harvest, build, edit, and be the last team standing.

## Run it

Double-click `bin\Blockfall.exe`. It needs Windows 10 or 11 and a GPU that supports Direct3D 11 (almost all of them do).
Keep the `assets` folder next to `bin` - that's where the character model and skin textures live.

### If Windows says it's a virus or blocks it

It's a false alarm: the game isn't signed with a paid code-signing certificate, and Windows treats
any new unsigned download as risky until enough people have run it.

- **Blue box "Windows protected your PC"** (SmartScreen): click **More info**, then **Run anyway**.
- **Windows Security says it found a threat** (Defender): open **Windows Security → Virus & threat
  protection → Protection history**, pick the Blockfall entry, choose **Allow on device**. Then
  send `bin\Blockfall.exe` to Microsoft as a false positive at
  <https://www.microsoft.com/wdsi/filesubmission> (choose "Home customer", "Incorrectly detected
  as malware"). Microsoft usually clears it in a few days, and then Defender stops flagging it for
  everyone.
- **"Smart App Control blocked part of this app"** (Windows 11): Smart App Control blocks every
  unsigned app and has no "run anyway". It can be turned off in **Windows Security → App & browser
  control → Smart App Control**, but Windows won't let you turn it back on without a reinstall, so
  that's your call.

The exe carries its name, version, publisher and an app manifest, which keeps the guessing
checks calmer. Only signing the exe with a code-signing certificate removes these warnings for good.

## What's new in v16 - 30 new skins with glowing FX, and a K-Bucks store

- **30 new skins (94 in all).** Each started as a written concept (in `skins_catalog.py`), then
  got a palette, a body build and outfit pieces, its own painted texture and, for some, emission
  FX. Among them: MAGMA CORE (obsidian armour with lava glowing through the cracks), FROSTBYTE
  (ice-white with lit cyan circuits), VOID WALKER, GLITCH (flickering pixel camo), JADE SENTINEL,
  SUNFORGE, AURORA (teal-to-violet gradient) and NEON RACER.
- **Five new patterns:** energy cracks, circuit traces, tiger stripes, gradients and pixel camo.
- **Emission FX:** a skin's trim, cracks or circuit lines can glow (with a slow pulse) and its
  eyes and goggle lenses can light up. Glowing parts ignore shadows.
- **More than 64 skins can be owned.** Accounts and settings now remember up to 128; old files
  load as before.
- **GET K-BUCKS** (Item Shop): buy K-Bucks with real money through Stripe at the old V-Bucks
  prices: 1,000 for $7.99, 2,800 for $19.99, 5,000 for $31.99, 13,500 for $79.99. The game opens
  Stripe's checkout page in your browser, then adds the K-Bucks a few seconds after you pay.
  Purchases not finished yet are remembered in your account and picked up the next time you
  start the game. It needs the small payment server in `server/payments` to be online; see
  [its README](server/payments/README.md) for the 20-minute setup. Until then the screen says the
  store isn't set up.

## What's new in v15 - animations that touch the ground, tailored outfits, occlusion culling

- **Feet stay on the ground.** Every standing and moving animation now uses leg IK: the game
  works out the hip and knee bends that put each foot exactly on the ground. Before, feet sank up
  to 26 cm into the ground when crouching and 13 cm when running, and slid while sprinting.
- **Real running.** Jog and sprint are built from a step cycle: the planted foot stays still on
  the ground while the body passes over it, the heel peels up, the other leg swings through and
  the hips bob. Sprinting leans forward with long strides and a big arm drive. Each gait's stride
  length matches its speed (`STRIDE_WALK`, `STRIDE_SPRINT`, `STRIDE_CROUCH` in `game.h`), so feet
  don't skate.
- **Strafing and backing up.** The legs turn toward where you're actually going (up to 60
  degrees) while your chest, arms and gun stay on the crosshair. Walking backward plays the step
  cycle in reverse. No more forward-running legs sliding sideways.
- **Crouch, idle and heal** stand properly: weight shifts and breathing in idle, one heel up in the
  crouch, feet planted while healing.
- **Tailored outfits.** Every skin's clothes were repainted like real gear: a fitted top with a
  front panel and zip, darker side panels, piping, ribbed collar and cuffs, an arm band and
  sleeve stripes; trousers with side stripes, knee pads and thigh straps with buckles; a leather
  belt with a buckle and pouches; padded gloves; tall laced leather boots with toe caps and
  treaded soles; creases at the elbows, knees, waist and ankles.
- **Occlusion culling.** Each frame the ground, house walls and floors and your builds are drawn
  into a small depth picture on the CPU, and anything outside the view or fully hidden behind
  them isn't drawn (it still casts its shadow). From a typical spot that skips most of the map:
  in the test scene 5,547 objects become about 840.
- **Fewer virus warnings.** The exe now has its name, version, publisher, an icon and an app
  manifest. See "If Windows says it's a virus" above for the rest.

## What's new in v14 - outfits, real hair, faces that move

- **Outfit kit.** 14 modelled outfit pieces: jacket, hood, tactical vest, pauldrons, gauntlets,
  shin guards, boots, belt, cape, scarf, backpack, goggles, bandana mask and a thigh holster.
  Every skin wears its own combination (STEEL KNIGHT in metal armour and a cape, PILOT in a
  leather jacket with goggles and a scarf...), coloured from its palette.
- **Three body builds:** athletic, heavy and slim. The outfit pieces fit each one.
- **Hair made of cards:** layered strips with a see-through strand texture instead of solid
  shapes. Long hair, capes and scarves **swing** with spring physics: they trail behind when you
  run and stream upward when you skydive.
- **Faces:** real eyes, eyelids, brows and lips on face bones. Characters blink and change
  expression: focused while aiming, straining while sprinting or swinging, hurt when hit, in
  pain when knocked, smiling in the lobby.
- **Detail maps:** one shared 4096 x 4096 normal map and material map (shine, metalness, shading,
  what each spot is made of) cover the body and every outfit piece: sculpted muscles and folds,
  armour ridges and rivets, cloth weave, leather grain, stitching and skin pores.
- **CHARACTERS: STYLIZED / REALISTIC** (Settings → Graphics). Stylized keeps the cartoon look
  with outlines. Realistic is physically based: shiny metal, matte cloth, skin that softens the
  light, and a highlight running across hair.

## What's new in v13 - sound, smarter bots, real door shapes

- **Sound.** 26 sound effects: each gun, reloads, hit markers (a "ding" on headshots), the
  pickaxe, building, breaking and editing, footsteps, landing, chests, pickups, heals,
  shockwaves, knocks, eliminations, revives, the storm and menu clicks. Sounds get quieter with
  distance and come from the side they happen on. There are no audio files: every sound is
  generated by code when the game starts (`src/game/sfx.cpp`) and played with XAudio2, which
  comes with Windows. **SOUND VOLUME** is on the Settings → Graphics tab, next to V-Sync.
- **Bots find their way around things.** A bot that gets stuck on the ground now plans a route
  around the obstacle with A* pathfinding on a small grid (`src/game/nav.cpp`): out of a U of
  walls, round a house, in through a door. Before, it could only swerve and hop.
- **Door and window shapes for wall edits.** A wall edit is only applied if the cut-out tiles
  make one rectangle (a door, window, arch or half wall) and nothing left over would float.
  Bad shapes show red while you drag and aren't applied.
- **Fixed:** the game builds with Visual Studio again (the scene shader was too long for one
  MSVC string).

## What's new in v12 - athletic bodies, 64 skins, cleaner picture

- **New body shape.** The character is built with athletic proportions like a modern shooter
  skin: a smaller head, thin neck, broad shoulders, narrow waist, slim arms and long legs
  (`tools/character/build_character.py`). Hair, hats and the face follow the new head.
- **Better skin textures.** Every skin now has sewn-looking detail: a front zip, side and sleeve
  seams, cuffs, a hem, pants pockets front and back, a fly, rimmed knee pads, boot laces and
  welts, glove straps and knuckle pads, a folded collar, cheek colour, eyelids, nose and chin
  shading, hair strands, cloth weave and soft shadows in the armpits, crotch, under the chin
  and under the belt.
- **64 skins** (up from 40) and 6 new shirt patterns: racing stripes, plaid, two-tone,
  chevrons, a lightning bolt and a panelled jacket.
- **Skydive fixed.** The whole body now tips over at the hips, so you fall belly-down, head
  first, arms out and legs trailing - instead of standing up in the air.
- **Battle bus fixed.** The bus is drawn in flat colours with a white roof deck, yellow walkway,
  hand rails, windows, bumpers and lights, you stand on the deck (not the edge), and the camera
  can no longer drop inside the bus when you look up.
- **Anti-aliasing redone.** High now renders at 2x resolution and downsamples (supersampling);
  the temporal pass clips history in YCoCg colour space and sharpens only brightness, which
  removes the rainbow fringes on dark edges.
- **Knocked phase** in Duos, Trios and Squads (not Solos). When your health hits zero with a
  teammate still standing you're knocked instead of out: you crawl slowly, can't shoot or build,
  and bleed out over 40 s. A teammate holds **E** next to you for 5 s to pick you up (back at
  30 hp). Enemies can finish you. AI squads revive each other too, and crawl toward their team.
  When nobody on a team is left standing, its knocked players are out (team wiped).
- **Shockwave fuse.** A shockwave stops where it lands, pulses blue for 1 second, then blows.
- **Longer matches.** The storm takes about 12 minutes to close instead of 7, bots hit each
  other softer and don't go looking for each other across the map in the first minute, so a
  100-player lobby still has 80+ alive after a minute and a half and ends with a real final circle.
- **No enemy health bars.** You only see your damage numbers; teammates still show theirs.
- **Flat edit tiles.** The selection tiles on cones and ramps are flat 2D tiles on the surface.

## What's new in v11 - textures and skins

- **Real high-res textures.** Every material now has a 2048 x 2048 albedo *and* normal map
  (`assets/textures/`): wood planks, bark, stone cobbles, brick, metal panels with rivets, grass,
  leaves, cloth weave, skin, cracked plaster and roof tiles. They're mapped from three sides
  (triplanar), so nothing needs UVs. The Materials setting turns them on (Low = light, Medium+ =
  full), and the Normal Maps setting uses the texture normals (High adds the fine grain on top).
  Made by `tools/textures/make_textures.py` - seamless, reproducible, and the game still runs
  without them (it falls back to the shader bumps).
- **40 skins** (up from 8), each with its own 2048 x 2048 painted texture: 8 shirt patterns
  (stripes, camo, checker, hex plates, stars, flames, sash), 4 hair styles, 3 hats (cap, beanie,
  helmet), headbands, 8 skin tones. Skins have rarities with prices: Uncommon 800, Rare 1200,
  Epic 1500, Legendary 2000 K-Bucks.
- **Item Shop rotation:** 2 featured (25% off) + 6 daily skins, re-dealt every day from the whole
  catalogue. The Locker shows what you own, with pages.
- The skin list lives in one place, `tools/character/skins_catalog.py`; it generates both the
  textures and `src/game/skins_data.inc` for the game. Add a line there to add a skin.

## What's new in v10

- **Bigger builds.** Grid cells are now 6 m wide and 4.5 m tall (were 5 x 4), close to Fortnite's
  proportions against the 1.95 m character. Cones are taller to match.
- **Stairs edit like Fortnite.** Edit a ramp and *swipe* across the 2x2 grid the way you want the
  stairs to go up: two tiles in a line = straight stairs that way; an L of three = turn stairs that
  climb from the first tile round to the last; all four = plain stairs again.
- **Build placement rules, studied from Fortnite and applied:**
  - Walls go on the edge of *your own* cell in the direction the camera faces (snapped to the four
    directions); looking up puts the wall a level higher, looking down a level lower.
  - Floors and stairs go in the cell in front of you at your level; looking up ≈ 20°+ puts them a
    level higher, looking down puts them in your own cell.
  - Pyramids (cones) go on the ground in the cell in front of you; looking up puts one over your
    head - the "cone on top of my box" move.
  - Everything is chosen relative to your own level, so hills don't shift the result.
- **Three kinds of AI.** Every bot has a play style: *fighters* (fast reactions, push, ramp up to
  high targets, only retreat under 20 HP), *hiders/runners* (run from fights toward houses, box up
  with four walls and a cone when hurt, heal to full, retreat early) and *mixed*. Teammates are
  fighters or mixed.
- **Bots build**: a wall between them and whoever just shot them, ramps to reach you, and boxes.
  They carry wood/brick/metal and gain 50 wood per kill.
- **Bots loot and heal**: they pick up better guns, med kits and shield potions off the ground,
  open chests they walk past, use heals when nobody's shooting, and carry shield like you do.
- **Eliminated players drop 50 of a random material** (wood, brick or metal) next to their gun.
- **Out of a material?** Building switches to the next one you have: wood → brick → metal.
- **Shockwave grenades** (one chest in four, stack of 6): throw with fire; everyone within 6.5 m,
  you included, gets launched - no damage, and no fall damage after one.
- **Fall damage**: drops over 7 m (about a story and a half) hurt, 7 per extra metre, up to 90.
  Glider landings and shockwave launches are safe.
- Fixed: edited floors/walls that lost the tiles holding them up now break instead of floating -
  support is checked against the real remaining shape, and re-checked after every edit.
- Fixed: aiming down sights no longer blurs the screen (depth of field is off in matches).

## What's new in v9

- **Sign in.** The first time you start the game you create an account (name + password).
  Your K-Bucks, skins, equipped skin and stats are saved to that account
  (`%APPDATA%\Blockfall\accounts\<name>.ini`), so they survive reinstalls and new settings files.
  Sign out from the lobby to switch accounts. Accounts live on your PC - there is no online server.
- **Item Shop.** New SHOP button in the lobby. Two featured skins a day are 25% off (900 K-Bucks);
  the rest cost 1200. The Locker now just lists the skins you own and lets you equip them.
- **Big map: 800 m x 800 m, 100 players** (Solo 100, Duos 50 teams, Trios 34, Squads 25).
  Rolling hills, ~900 trees, 380 rocks, 100 spawn points, a longer bus ride and a storm that
  starts at 300 m.
- **12 places that all look different:** Cobble Cove (a town with roads and a town hall),
  Barnyard Bluff (barn, silos, fenced field with hay), Hilltop Hollow (3-floor tower with stairs
  and a roof chest), Sawmill Springs (mill shed and log piles), Boulder Bend (quarry full of
  rocks), Rusty Reach (gas station with a canopy over the pumps), Pine Pocket (camp huts round a
  fire), Mossy Ruins (broken stone walls), Lantern Lane (a market of eight stalls), Scrap Depot
  (warehouse with crate stacks and barrels), Willow Walk (houses round a well) and Lookout Ridge
  (a 4-floor tower on a hill).
- **Character fixes:** every clip leans the right way now (sprinting leans forward, skydiving is
  belly-down with arms back, gliding hangs from the handles), and the body is more human: a
  V-shaped torso, tapered arms and legs, squarer limbs, a flatter chest, boots and mitt hands.
- **Cone edits** work the way you asked: the tiles you select are the side that rises (to 3.5 m,
  so you can get under it); the other side slopes to the floor. Selecting all four tiles keeps a
  plain cone instead of deleting it.
- **Builds build up.** A fresh piece starts at 20% health and reaches full in 2.5 s. Editing a
  piece restarts that build-up, toward the health it had (damage isn't healed by editing).
- The hotbar number on a gun is now its **total ammo** (loaded + reserve).
- **Better font**: a smooth bold system font (Bahnschrift / Segoe UI) with proper letter widths and
  mipmaps, and the "P looks like B" bug is gone.
- Mouse sensitivity changes in steps of 0.1.

## What's new in v8 - real characters

The stick-and-ball people are gone. Every player is now a rigged, textured, animated character
loaded from `assets/character.glb`, with one painted texture per skin in `assets/skins/`.

How it was made (the same pipeline a studio uses, just scripted):

1. **Model.** `tools/character/build_character.py` runs inside Blender (the free `bpy` Python
   module). A stick figure of joints with radii is wrapped in a chunky rounded body by Blender's
   Skin modifier and smoothed with Subdivision - the cartoon look on purpose.
2. **Texture.** Each skin's texture is painted procedurally: every texel is mapped back onto the
   body, and a little shader decides shirt / pants / boots / belt / gloves / hair / eyes / mouth /
   emblem from where it sits. The texture's alpha channel marks the accent trim, which the game
   recolours cyan for teammates.
3. **Rig.** Blender's "create armature from skin" turns the joints into a 22-bone skeleton with
   weights. Every skin uses this one skeleton, so every animation works on every skin.
4. **Animate.** 17 clips are keyframed in the script: idle, walk, sprint, crouch, crouch walk,
   jump, skydive, glide, aim, shoot, reload, build, heal, pickaxe idle, pickaxe swing, wave, dance.
5. **Export** as one glTF binary (`.glb`): meshes (3 levels of detail), 4 hair styles, headband,
   skeleton and animations in one file.

And in the game:

- `src/game/model.cpp` reads the .glb with cgltf, mirrors it into Blockfall's left-handed world,
  and samples the animation keyframes (lerp / slerp).
- `src/game/charanim.cpp` is the animation state system: it picks clips from what you're doing,
  cross-fades when the action changes, plays arm actions (aim, swing, build, reload) over the
  legs' walk/sprint/crouch, and bends the chest to follow your aim up and down.
- Guns and the pickaxe are pinned to the `Hand_R` bone, so they follow every animation.
- The renderer does **GPU skinning**: the bone matrices go to a constant buffer and the vertex
  shader blends up to 4 bones per vertex, so 20+ players cost almost nothing.
- Characters use a **cartoon shader**: stepped lighting, a rim light and a dark outline (an
  inverted hull). Turning Smooth Shading off makes the steps hard; the outline is dropped far away.
- Three body LODs by distance (the Mesh Detail setting moves the distances).
- If the `assets` folder is missing, the old built-in figures are used.

To change the character or add animations: edit `tools/character/build_character.py` and run
`python3 tools/character/build_character.py` (needs `pip install bpy`). `tools/character/preview.py`
renders check pictures with Blender's own renderer.

## What's new in v7

- **Locker and skins.** Press LOCKER in the lobby. There are 8 skins (RECRUIT is free). Click a
  skin to preview it on the stage, then BUY or EQUIP. Your equipped skin is what you wear in the
  match, and bots/friends wear random ones so you can tell people apart.
- **K-Bucks.** The game's currency, shown top-right in the lobby. You earn **25 K-Bucks for every
  elimination** (paid out the moment you get the kill, and shown on the end-of-match screen).
  Every skin costs **1200 K-Bucks**. Balance, owned skins and equipped skin are saved in
  `%APPDATA%\Blockfall\settings.ini` (edit `kbucks=` there if you want to test the locker fast).
- Healing in the storm works now - taking a storm tick no longer cancels a Med Kit / Big Pot / Mini.
- Cone editing picks the tile you are actually looking at (it used to pick the opposite one when
  the cone was between you and the tile).

## What's new in v5

- **Battle bus:** every match starts on the bus. Press Space to jump, steer with WASD, the glider opens near the ground. Teammates jump with you; other squads pick their own spots.
- **Storm:** a circle that shrinks in stages (timer under the mini map, edge shown on both maps and as a purple wall in the world). Outside it you take 1, then 2, 3, 5, 8 damage a second. Bots run for the safe zone.
- **Wood / brick / metal:** trees give wood, rocks and house walls give brick, scrap barrels and roofs give metal. Right-click in build mode switches material. Walls: wood 150, brick 300, metal 450 HP.
- **Cone edits fixed:** editing a cone moves its peak like the real thing. One tile = slope to that corner; two on a side = ramp; three = only that corner stays up; four = flat.
- **Stairs (ramps) edit too:** two tiles on a side turn the ramp that way; an L shape makes turn stairs that spiral around the untouched corner.
- **Builds need support:** you can't place in the air, and anything left floating breaks.
- **Build health:** a bar and HP number for the build under your crosshair, small bars on damaged ones.
- **Ramps and cones are solid** from the side (you can still walk under the tall end).
- **Shield potions:** Big Pot (+50, 5 s, stacks to 2) and Minis (+25 up to 50, 2 s, stack to 6) drop from chests.
- **Guns:** bullets go exactly where the crosshair points. Magazine count and a reload ring sit right next to the crosshair. Damage you deal stacks up over the target and resets after a second without a hit. Snipers have a scope and a scope view when aiming. Dropped guns look like their icons.
- **Characters:** knees, elbows, shoulders, spiky hair, headband, belt, boots, gloves and a face, with their own outfit colours.
- The controls box is off by default (F1 shows it).

## Lobby, friends and modes

- **MODE:** use the `<` `>` buttons to pick Solo (100 players), Duos (50 teams of 2), Trios (34 teams of 3) or Squads (25 teams of 4).
- **FRIENDS:** set your name, add friends by name, and invite up to 3 of them to your party. Inviting someone switches the mode up automatically if your party wouldn't fit.
- **Your friends list stays on your PC.** In a match, invited friends play as AI teammates with their names. Empty team spots are filled with bots. Real online play would need a game server; that's a separate project.
- **Teammates** follow you, fight with you, and show their names and health over their heads and on the left of the screen. In team modes you're knocked first and a teammate can revive you; once you're eliminated, you spectate a teammate. Your team wins if it's the last one standing.
- Bots on other teams fight each other too, not just you.

## Controls (default - change any of them in Settings)

| Key | What it does |
|---|---|
| W A S D | Move |
| Mouse | Look |
| Space | Jump |
| Shift (hold) | Sprint (1.5x speed, uses stamina) |
| Ctrl (hold) | Crouch |
| Left click | Shoot / swing pickaxe / use a heal / throw a shockwave / place a build / select edit tiles |
| Right click | Aim down sights / reset a piece while editing |
| R | Reload (in build mode: rotate the ramp) |
| F | Pickaxe |
| 1 - 5 | Hotbar slots |
| E | Hold 0.5s to open a chest, tap to pick up an item, hold 5s next to a knocked teammate to revive |
| Q | Toggle build mode |
| Z / X / C / V | Wall / Floor / Ramp / Cone |
| G | Edit the build you're looking at (press G again to confirm) |
| T | Reset edit: puts the build you're editing or looking at back to full |
| Tab | Inventory |
| M | Map |
| F1 | Show / hide the controls box |
| Esc | Pause menu (Resume, Settings, Leave match) |

**Custom keybinds:** go to Lobby (or Esc in a match) → **SETTINGS / KEYBINDS**.
- Every action has a **primary** and a **secondary** key, and either one works.
- Left-click a key, then press the new key or mouse button. Mouse 4/5, middle click and the scroll wheel work too.
- Press **Delete** while it says PRESS A KEY to clear that key. Right-clicking a key, or hovering it and pressing Delete, clears it too.
- The same key can be used by more than one action (for example scroll wheel up on both Wall and Edit for a wheel reset). The game just tells you who else uses it.
- Mouse sensitivity is on the same screen. V-Sync and sound volume are on the Graphics tab.
- Name, keys, friends, graphics and volume are saved to `%APPDATA%\Blockfall\settings.ini`.
  K-Bucks and skins are saved to your account (`%APPDATA%\Blockfall\accounts\<name>.ini`).

## Build it yourself

**What you need:** Visual Studio 2022 (the free Community edition is fine) with the **"Desktop development with C++"** workload. That includes CMake and the Windows SDK, which has Direct3D 11.

**Option A - Visual Studio, no command line**

1. Open Visual Studio and choose **File > Open > Folder...**, then pick this `blockfall` folder.
2. Wait for it to say "CMake generation finished" at the bottom.
3. At the top, set the startup item to **Blockfall.exe** and the configuration to **x64-Release**.
4. Press **F5** to build and run.

**Option B - double-click `build.bat`**

It runs CMake for you and puts the game at `build\Release\Blockfall.exe`. Run it from the **"x64 Native Tools Command Prompt for VS 2022"** if double-clicking says it can't find CMake.

**Option C - command line**

```
cmake -S . -B build
cmake --build build --config Release
build\Release\Blockfall.exe
```

## What's in the game

**Camera & crosshair.** The camera sits over your right shoulder, so your character is left of center. The crosshair is always at the exact center of the screen. When you shoot, the game first traces a line from the camera through the crosshair to find what you're aiming at. The bullet then flies from your gun to that point. The crosshair lines spread out as your shots get less accurate.

**Movement.** Walk 4.6 m/s, sprint 6.9 m/s, crouch 2.6 m/s. You can walk up ramps and stand on anything you build. Drops over 7 m cause fall damage (7 per extra metre, up to 90).

**Map.** Press M for the full map: named places, a grid (A-F, 1-6), you and your teammates. The mini map in the top right shows the 130 m around you, north up, and the grid square you're in. Walking into a named place shows its name (and DISCOVERED the first time).

**Inventory.** Press Tab (the match keeps going, like the real thing).
- Click a slot, then another slot, to move or swap items.
- Pick a slot and press DROP to throw it on the ground.
- You can also drop ammo, and see your materials.

**Stamina.** Sprinting drains the yellow bar in about 6 seconds. When it runs out you drop to walking. It starts refilling 1 second after you stop sprinting, and you can sprint again once it's back to 25%.

**Pickaxe.** Press F. Hold left click to swing every 0.5 seconds.
- Trees give 8 wood per hit, rocks 10 brick, house walls 6 brick and roofs 8 metal.
- They wobble when hit and break when their health runs out.
- It does 50 damage to builds and 20 to players.

**Building.** Pieces snap to a grid: 6 m wide, 4.5 m tall. Each piece costs 10 wood, brick or metal (right click switches; harvest more with the pickaxe).
- Walls go on the edge of the square you're standing in, on the side you're facing. Look up to place them higher.
- Floors and stairs go in the cell ahead; cones on the ground ahead, or over your head when you look up.
- Ramps point the way you face (R rotates them).
- Hold left click to keep placing pieces as you move.
- Pieces need support: you can't build in the air, and anything left floating breaks.
- Walls have 150 / 300 / 450 HP in wood / brick / metal; floors, ramps and cones 140 / 280 / 400. A new piece starts at 20% and builds up to full in 2.5 s. Bullets (yours and the bots') break them, and they darken as they take damage.

**Editing.** Look at one of your builds and press G.
- Walls show a 3x3 grid. Floors, cones and ramps show 2x2.
- Click and drag over tiles to mark them. Let go of the mouse (or press G) to confirm.
- **Walls:** marked tiles are cut out, but only in a door, window, arch or half-wall shape: the
  cut has to be one rectangle, and every tile left has to connect down to the bottom row. A
  shape that breaks the rules shows red and isn't applied. For example, the bottom-middle and
  middle tiles make a door you can walk through.
- **Floors:** any tiles can be cut out (not all four).
- **Cones:** the marked tiles are the side that rises; the rest slopes down to the floor.
- **Ramps:** swipe the way you want the stairs to climb (see v10 above).
- Right click (or T) resets the piece to full.

**Loot.** Every chest drops:
- 1 gun: Assault Rifle, Shotgun, SMG, Pistol or Sniper.
- A stack of the matching ammo: Light, Medium, Heavy or Shells.
- A 50% chance of a Med Kit, a 40% chance of 3 Minis or 20% of a Big Pot, and a 25% chance of 2 Shockwave grenades.

Rarity odds are Common 40%, Uncommon 30%, Rare 18%, Epic 9%, Legendary 3%. Each step up adds 5% damage. Dropped items float and glow in their rarity color. If your hotbar is full, picking up swaps out what you're holding.

**Heals.** You have to stand still; moving, jumping or getting shot cancels them (storm ticks don't).
- **Med Kit:** +50 health in 3 s, stacks to 3.
- **Big Pot:** +50 shield in 5 s, stacks to 2.
- **Mini:** +25 shield (only up to 50) in 2 s, stacks to 6.

**Bots.** Every bot has a play style: *fighters* push and build ramps up to you, *hiders* run for
houses and box up when hurt, *mixed* do a bit of both. They go through these states:
1. **Wander** around the map, looting chests, guns and heals they pass. Teammates follow you instead.
2. **Spot** the nearest enemy they can see (you, your teammates, or another team's bots). Walls block their view, and they wait a moment before reacting.
3. **Chase** them to their gun's best range, building ramps to reach high targets.
4. **Shoot** in short bursts while strafing. They're less accurate than you and do less damage, so you can win fights.
5. **Take cover** or **flee** when hurt, throw up a wall when shot, box up and heal.

They head for the safe zone when the storm closes in. When one gets stuck on the ground, it plans
a path around whatever is in the way. Eliminated bots drop their gun and 50 materials.

**Animations.** Characters have a pose for each action: idle breathing, walk, sprint (leaning, bigger arm swing, gun lowered), crouch, jump (legs tucked), aim (gun raised to the eye), shoot (recoil kick), reload (left hand drops to the magazine, which falls out and back), build, heal (hands to the chest), and the pickaxe swing. Bots use the same set.

**Graphics settings** (Settings → GRAPHICS). Each one is Off / Low / Medium / High, with PERFORMANCE, BALANCED and EPIC presets. These are this game's own Direct3D 11 versions of the ideas behind the UE5 features — not Nanite or Lumen themselves, which are Unreal Engine code:

| Setting | What it does |
|---|---|
| Mesh detail | Round shapes (people, trees, rocks) have 3 levels of triangles; the renderer picks one by distance, like Nanite picking cluster detail. Off = lowest everywhere. High = finest, plus a 4x finer terrain mesh. |
| Smooth shading | Off shows every flat triangle. Low blends normals across edges. Medium adds a rim light. High a touch more. |
| Normal maps | Painted-on bumps that react to light: wood grain, stone cracks, grass, brick rows on house walls, roof tiles. Higher = stronger and finer. |
| Materials (PBR) | Each surface has roughness and metalness. Off = plain shading. Low = simple highlights. Medium = GGX. High = GGX + fresnel + sky reflections on metal and shiny things. |
| Lighting | Off = flat ambient. Low = sky/ground hemisphere light. Medium = + screen-space ambient occlusion (dark corners). High = + colour bounce from nearby surfaces (a red wall tints the floor). |
| Shadows | Sun shadows from a shadow map. Low 1024 hard, Medium 2048 soft (3x3 PCF), High 4096 with shadows that soften the further they fall from the object (PCSS-style). |
| Anti-aliasing | Low = FXAA. Medium = temporal AA (blends jittered frames, reprojected with the depth buffer). High = renders at 2x resolution and downsamples, plus temporal + sharpen. |
| Post-processing | Low = distance fog + ACES tonemap. Medium = + bloom + colour grading. High = + vignette. (Depth of field is off in matches so aiming stays sharp.) |

**Look.** Characters, trees, rocks and shadows are rounded (spheres, ovals and cylinders). Builds and houses stay flat on purpose. Text uses a smooth font.

**HUD.**
- Stamina, shield and health bars.
- An icon for every weapon (Assault Rifle, Shotgun, SMG, Pistol, Sniper), plus the pickaxe, Med Kit, ammo and materials.
- Ammo (magazine / reserve) and materials.
- A 5-slot hotbar with rarity-colored borders.
- Hit markers (gold for headshots) and floating damage numbers.
- Teammate health bars (enemies don't show one, you only see your damage numbers), kill feed, players-left counter and FPS.

## Changing the numbers

| What | Where |
|---|---|
| Gun stats (damage, fire rate, magazine, reload, spread, range) | `src/game/types.h` |
| Rarity odds and Med Kit chance | `src/game/loot.h` |
| Speeds, stamina, starting materials, bot damage, players per mode | top of `src/game/game.h` |
| Pickaxe damage and swing speed | `src/game/types.h` |
| Default keys | `src/game/keybinds.cpp` |
| Weapon icon shapes | `src/game/atlas.cpp` |
| Place names and how many houses each gets | `World::generate` in `src/game/world.cpp` |
| Graphics presets | `GraphicsSettings::preset` in `src/game/settings.cpp` |
| Skin price, K-Bucks per kill | `src/game/skins.h` |
| The skin list | `tools/character/skins_catalog.py` (generates `src/game/skins_data.inc`) |
| Shaders (lighting, shadows, bumps, post effects) | `src/platform/renderer_d3d11.cpp` |
| Grid size, build cost, cone height | top of `src/game/building.h` |
| Wall edit shape rules | `validWallEdit` in `src/game/building.cpp` |
| Camera distance and shoulder offset | `Game::updateCamera` in `src/game/game.cpp` |
| Sound effects (how each one is made, how far it carries) | `synthSfx` / `sfxRange` in `src/game/sfx.cpp` |
| Bot pathfinding (grid size, search limits) | top of `src/game/nav.h` |

## Tests

All the game rules live in `src/game/` and don't depend on Windows, so they can be tested anywhere. There are 519 checks covering every stage and every new feature. Some examples:
- Sprint is 1.5x and is blocked while aiming or shooting.
- The aim ray hits the exact screen center.
- Walls block movement.
- You can climb a ramp onto a floor.
- Cutting a door lets you walk through it.
- Rarity odds match over 100,000 chests.
- The Med Kit timing works.
- Bots go Wander > Spot > Shoot with a reaction delay, can't see through walls, and take cover that actually hides them.
- The pickaxe gives the right materials and breaks trees.
- Stamina drains and refills on time.
- Keybinds save, load and swap.
- Clicking PLAY starts the right team sizes, and friends keep their names.
- There's no friendly fire, bots fight each other, and spectating and winning work.
- Wall edits accept doors and windows and refuse floating or scattered cuts.
- A bot walks out of a U-shaped trap and finds the door into a closed box.
- Every sound effect synthesises cleanly, fades with distance and pans to the right side.
- A cape sags under gravity, trails behind when running and lifts when falling; eyelids close
  downward and the jaw drops the lower lip on the real model.

To run them:

```
cmake -S . -B build-tests -DBLOCKFALL_TESTS=ON
cmake --build build-tests --config Release --target blockfall_tests
build-tests\Release\blockfall_tests.exe
```

Run it from the repo root (some checks load `assets/`). It ends with `N passed, 0 failed`.

`Blockfall.exe --shots <folder>` plays a scripted demo and saves screenshots to that folder. It walks through the lobby, friends and settings screens, then plays a squads match: pickaxe, sprint, team fight, pause and victory. It's handy to check that rendering works on a new machine.

## Files

```
src/core/       math (vectors, matrices, ray tests), random numbers
src/game/       all game rules (no Windows code)
  frontend      sign in, lobby, friends, locker, item shop, settings/keybinds, pause, end screens
  account       local accounts (K-Bucks, skins and stats saved per player)
  keybinds      rebindable controls
  settings      saving name, keys, sensitivity, graphics, volume, friends
  atlas         HUD texture: text + weapon icons
  map           the map picture for the mini map and map screen
  world         map, terrain, houses, chests, collision, raycasts
  building      grid, pieces, placement
  editing       build mode + edit mode
  player        movement, sprint, shooting, reload, Med Kit
  weapons       spread/bloom, fire rate, damage falloff
  loot          rarity, gun type, ammo rolls
  ai            bot states, play styles, building, looting
  nav           bot pathfinding (A* on a walkability grid)
  sfx           sound events and sound synthesis
  hud           HUD layout (crosshair, bars, hotbar, text)
  scene         turns the game into boxes for the renderer; attaches guns to hands
  model         loads assets/character.glb (cgltf), skeleton + animation sampling
  charanim      animation state system (clip picking, cross-fades, upper-body layer, aim pitch)
  charphys      swinging hair, capes and scarves (spring physics on bone chains)
  charface      facial expressions and blinking
src/third_party cgltf (glTF loader), stb_image (PNG)
assets/         character.glb, character_normal.png + character_material.png (shared detail atlas)
                and skins/*.png (all made by tools/character/build_character.py)
tools/character Blender scripts that build the character, outfit kit, hair cards, face rig,
                detail atlas, textures and animations
src/platform/   Windows window, mouse/keyboard input, XAudio2 sound, Direct3D 11 renderer (shadow map,
                G-buffer, AO, PBR, normal maps, LOD, FXAA/TAA, bloom/fog/DOF, GPU skinning + cartoon shader)
tests/          logic tests
```

## Known limits (it's a prototype)

- Wall edit rules are a simplified version of Fortnite's (one rectangular opening, nothing
  floating), not its exact list of allowed shapes.
- Skins are combinations of 3 builds and 14 shared outfit pieces, not 64 individually sculpted
  models, and the "sculpting" is done by code, not by an artist. Animations are scripted
  keyframes with leg IK, not motion capture; feet are planted for flat ground, so on slopes and
  stairs one foot can float a little. Outfit pieces can clip through each other in extreme poses.
- Bots only plan paths along the ground, and only once they're stuck. They don't plan routes up
  ramps or across upper floors, so tall builds can still trip them up.
- Friends are local to your PC (AI teammates with their names), not real online players.
- K-Bucks are kept in the account file on your PC, so they can be edited by hand. Payments go
  through Stripe and each purchase is credited once, but a cheat-proof balance needs online
  accounts.
- Occlusion culling only uses the ground, house walls/floors and unedited build walls/floors as
  blockers. Trees, rocks and edited builds never hide anything.
- The graphics effects are this game's own small versions, not UE5's. There's no real global illumination, just screen-space AO and bounce.
- Sound effects are simple synthesised ones. There's no music, no voice and no looping sounds
  (wind, the bus engine).
