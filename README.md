# Blockfall

A third-person build-and-shoot prototype written in C++17 with Win32 and Direct3D 11. There's no game engine.

You start in a lobby. Pick **Solo, Duos, Trios or Squads**, invite friends to your party, and press **PLAY**. You drop in with your pickaxe, a pistol and 100 materials. The map is 300m × 300m with seven named places (Sawmill Springs, Boulder Bend, Hilltop Hollow, Rusty Reach, Cobble Cove, Lantern Lane, Pine Pocket), each a cluster of houses with chests around them, and 16-20 players depending on the mode. Loot up, harvest, build, edit, and be the last team standing.

## Run it

Double-click `bin\Blockfall.exe`. It needs Windows 10 or 11 and a GPU that supports Direct3D 11 (almost all of them do).
Keep the `assets` folder next to `bin` - that's where the character model and skin textures live.

If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**. The game isn't signed with a paid certificate, which is why Windows warns about it.

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
- **Friends must be real.** You can only add a friend by the name of a real player account on
  this PC (one made on the sign-in screen).
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

- **MODE:** use the `<` `>` buttons to pick Solo (16 players), Duos (8 teams of 2), Trios (6 teams of 3) or Squads (5 teams of 4).
- **FRIENDS:** set your name, add friends by name, and invite up to 3 of them to your party. Inviting someone switches the mode up automatically if your party wouldn't fit.
- **Your friends list stays on your PC.** In a match, invited friends play as AI teammates with their names. Empty team spots are filled with bots. Real online play would need a game server; that's a separate project.
- **Teammates** follow you, fight with you, and show their names and health over their heads and on the left of the screen. If you go down, you spectate a teammate. Your team wins if it's the last one standing.
- Bots on other teams fight each other too, not just you.

## Controls (default - change any of them in Settings)

| Key | What it does |
|---|---|
| W A S D | Move |
| Mouse | Look |
| Space | Jump |
| Shift (hold) | Sprint (1.5x speed, uses stamina) |
| Ctrl (hold) | Crouch |
| Left click | Shoot / swing pickaxe / use Med Kit / place a build / select edit tiles |
| Right click | Aim down sights / reset a piece while editing |
| R | Reload (in build mode: rotate the ramp) |
| F | Pickaxe |
| 1 - 5 | Hotbar slots |
| E | Hold 0.5s to open a chest, tap to pick up an item |
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
- Mouse sensitivity and V-Sync are on the same screen.
- Everything (name, keys, friends) is saved to `%APPDATA%\Blockfall\settings.ini`.

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

**Movement.** Walk 4.6 m/s, sprint 6.9 m/s, crouch 2.6 m/s. You can walk up ramps and stand on anything you build.

**Map.** Press M for the full map: named places, a grid (A-F, 1-6), you and your teammates. The mini map in the top right shows the 130 m around you, north up, and the grid square you're in. Walking into a named place shows its name (and DISCOVERED the first time).

**Inventory.** Press Tab (the match keeps going, like the real thing).
- Click a slot, then another slot, to move or swap items.
- Pick a slot and press DROP to throw it on the ground.
- You can also drop ammo, and see your materials.

**Stamina.** Sprinting drains the yellow bar in about 6 seconds. When it runs out you drop to walking. It starts refilling 1 second after you stop sprinting, and you can sprint again once it's back to 25%.

**Pickaxe.** Press F. Hold left click to swing every 0.5 seconds.
- Trees give 8 materials per hit, rocks 10, and house walls and roofs 6.
- They wobble when hit and break when their health runs out.
- It does 50 damage to builds and 20 to players.

**Building.** Pieces snap to a grid: 5m wide, 4m tall. Each piece costs 10 materials (harvest more with the pickaxe).
- Walls go on the edge of the square you're standing in, on the side you're facing. Look up to place them higher.
- Floors and stairs go in the cell ahead; cones on the ground ahead, or over your head when you look up.
- Ramps point the way you face.
- Hold left click to keep placing pieces as you move.
- Walls have 150 HP. Floors, ramps and cones have 140. Bullets (yours and the bots') break them, and they darken as they take damage.

**Editing.** Look at one of your builds and press G.
- Walls show a 3x3 grid. Floors and cones show 2x2.
- Click and drag over tiles to mark them. Let go of the mouse (or press G) to confirm.
- On walls and floors, marked tiles are cut out. On cones, a marked quadrant is **flattened down to the base** (it stays solid, you can stand on it) instead of disappearing. Flatten all four and you get a floor.
- Right click resets the piece to full.
- For example, cutting the bottom-middle and middle tiles of a wall makes a door you can walk through.

**Loot.** Every chest drops:
- 1 gun: Assault Rifle, Shotgun, SMG, Pistol or Sniper.
- A stack of the matching ammo: Light, Medium, Heavy or Shells.
- A 50% chance of a Med Kit.

Rarity odds are Common 40%, Uncommon 30%, Rare 18%, Epic 9%, Legendary 3%. Each step up adds 5% damage. Dropped items float and glow in their rarity color. If your hotbar is full, picking up swaps out what you're holding.

**Med Kit** (the one consumable). Heals 50 health after 3 seconds of standing still. Moving, jumping or getting hit cancels it. They stack up to 3.

**Bots.** Each bot goes through these steps:
1. **Wander** around the map. Teammates follow you instead.
2. **Spot** the nearest enemy they can see (you, your teammates, or another team's bots). Walls block their view, and they wait about half a second before reacting.
3. **Chase** them to their gun's best range.
4. **Shoot** in short bursts while strafing. They're less accurate than you and do less damage, so you can win fights.
5. **Take cover** behind a house, rock, tree or one of your walls when their health drops below 35%, then come back out.

Bots drop their gun when eliminated.

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
| Anti-aliasing | Low = FXAA. Medium = temporal AA (blends jittered frames, reprojected with the depth buffer). High = temporal + sharpen, like TSR. |
| Post-processing | Low = distance fog + ACES tonemap. Medium = + bloom + colour grading. High = + depth of field while aiming + vignette. |

**Look.** Characters, trees, rocks and shadows are rounded (spheres, ovals and cylinders). Builds and houses stay flat on purpose. Text uses a smooth font.

**HUD.**
- Stamina, shield and health bars.
- An icon for every weapon (Assault Rifle, Shotgun, SMG, Pistol, Sniper), plus the pickaxe, Med Kit, ammo and materials.
- Ammo (magazine / reserve) and materials.
- A 5-slot hotbar with rarity-colored borders.
- Hit markers (gold for headshots) and floating damage numbers.
- Enemy health bars, kill feed, enemies-left counter and FPS.

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
| Skin price, K-Bucks per kill, the skin list | `src/game/skins.h` / `src/game/skins.cpp` |
| Shaders (lighting, shadows, bumps, post effects) | `src/platform/renderer_d3d11.cpp` |
| Grid size, build cost, cone height | top of `src/game/building.h` |
| Camera distance and shoulder offset | `Game::updateCamera` in `src/game/game.cpp` |

## Tests

All the game rules live in `src/game/` and don't depend on Windows, so they can be tested anywhere. There are 324 checks covering every stage and every new feature. Some examples:
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

To run them:

```
cmake -S . -B build-tests -DBLOCKFALL_TESTS=ON
cmake --build build-tests --config Release
build-tests\Release\blockfall_tests.exe
```

`Blockfall.exe --shots <folder>` plays a scripted demo and saves screenshots to that folder. It walks through the lobby, friends and settings screens, then plays a squads match: pickaxe, sprint, team fight, pause and victory. It's handy to check that rendering works on a new machine.

## Files

```
src/core/       math (vectors, matrices, ray tests), random numbers
src/game/       all game rules (no Windows code)
  frontend      sign in, lobby, friends, locker, item shop, settings/keybinds, pause, end screens
  account       local accounts (K-Bucks, skins and stats saved per player)
  keybinds      rebindable controls
  settings      saving name, keys, sensitivity, friends
  atlas         HUD texture: text + weapon icons
  map           the map picture for the mini map and map screen
  world         map, terrain, houses, chests, collision, raycasts
  building      grid, pieces, placement
  editing       build mode + edit mode
  player        movement, sprint, shooting, reload, Med Kit
  weapons       spread/bloom, fire rate, damage falloff
  loot          rarity, gun type, ammo rolls
  ai            enemy states
  hud           HUD layout (crosshair, bars, hotbar, text)
  scene         turns the game into boxes for the renderer; attaches guns to hands
  model         loads assets/character.glb (cgltf), skeleton + animation sampling
  charanim      animation state system (clip picking, cross-fades, upper-body layer, aim pitch)
src/third_party cgltf (glTF loader), stb_image (PNG)
assets/         character.glb + skins/*.png (made by tools/character/build_character.py)
tools/character Blender scripts that build the character, textures and animations
src/platform/   Windows window, mouse/keyboard input, Direct3D 11 renderer (shadow map, G-buffer, AO,
                PBR, normal maps, LOD, FXAA/TAA, bloom/fog/DOF, GPU skinning + cartoon shader)
tests/          logic tests
```

## Known limits (it's a prototype)

- Edits don't follow Fortnite's door/window shape rules (any tiles can be cut).
- The character is one body shared by all skins (skins change the texture, hair and headband), and
  the animations are scripted keyframes, not motion capture.
- Bots don't pathfind. They steer and step around things, so they can get stuck on complicated builds.
- Friends are local to your PC (AI teammates with their names), not real online players.
- The graphics effects are this game's own small versions, not UE5's. There's no real global illumination, just screen-space AO and bounce.
- There's no sound or fall damage.
