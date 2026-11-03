# v17: better POIs, more buildings and skyscrapers

**Prompt (2026-11-03):** "I want you to make the poi's better by adding more buildings, more
skyscrapers, etc,"

**What was built**
- `World::buildHighRise`: enterable high-rise with window bands (pillars + glass panes), a door,
  stairs up the +x wall through a hole in every floor, cover on upper floors, chests every third
  floor and on the roof, a water tank and antenna on tall roofs. `buildTower` now uses it.
- Cobble Cove (Town, radius 46 → 60) became a downtown: two avenues with sidewalks, a skyscraper
  on each inner corner (7-14 floors), mid-rise offices, the town hall, houses, street lamps and
  parked cars. The player spawn moved to just outside its edge.
- More buildings at Hilltop Hollow, Lantern Lane, Scrap Depot, Rusty Reach, Barnyard Bluff, Willow
  Walk, Pine Pocket and Lookout Ridge (taller tower).
- Glass (`PropKind::Glass`): solid, blocks shots, breaks from one bullet or pickaxe hit.
- A 16 m spatial grid for statics, used by `collectSolids`, `groundHeight` and `raycast`, so the
  extra ~3000 props don't slow movement and shooting (CPU frame stays ~2 ms; without the grid it
  was 7.4 ms).

**Checks:** 568 tests pass. New ones: a skyscraper on all four downtown corners, buildings with
windows in 5 places, nothing solid on the spawn, walking up 6 flights to a roof with the real
movement code, glass blocks then breaks, and the grid agrees with a full scan on 400 random
rays/ground/collision checks. Demo screenshot `06a_downtown` (frame 339).
