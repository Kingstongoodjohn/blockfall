# 0004 — Occlusion culling on the CPU with a small depth picture

**Status:** Accepted (2026-10-30)

## Context
Every instance on the 800 m map (≈5–7 thousand boxes, trees, props, characters) was sent to the
GPU every frame; nothing was culled, not even outside the view. The user asked for occlusion
culling.

## Decision
- At the end of `buildScene`, a CPU rasteriser (`src/game/occlusion.*`) draws the big solid
  things into a 256x144 depth picture: the ground as a coarse 20 m grid whose corners sit below
  every real height around them (so it never pokes above the real ground), house walls and
  floors, and unedited build walls/floors. Only camera-facing box faces are drawn.
- Every instance's bounding sphere is tested against the view frustum and then against the
  picture (with an 8x8 block max-depth level to skip quickly). Hidden instances move to
  `SceneList::shadowOnly`; the renderer draws those in the shadow pass only, so shadows don't
  pop when their casters are culled. Hidden characters are flagged `shadowOnly`.
- Conservative by design: occluders cover only pixels whose centres they cover, things touching
  the near plane are always drawn, and a test checks that every hidden point is really blocked
  (world raycast + terrain march).
- Game-side (no Windows code), so it is unit-tested like the rest of `blockfall_core`.

## Consequences
- From a typical spot most of the map isn't drawn (the test scene: 5,547 instances → ~840).
- About 0.4–0.5 ms of CPU per frame on the test machine; the renderer then sorts and uploads far
  fewer instances.
- Trees, rocks, roofs and edited builds never occlude (their collision boxes don't match what is
  drawn closely enough to be safe).
