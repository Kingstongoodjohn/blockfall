# 0003 — One shared detail atlas for all skins; normals from per-texel sculpt, not a Cycles bake

**Status:** Accepted (2026-10-18)

## Context
Skins got normal, roughness and metal maps at 4096. Per-skin maps were measured first: about
58 MB of PNG per skin at 4096, roughly 3.7 GB for 64 skins in git, and about 8.6 GB of GPU
memory. That breaks GitHub's push limit and most graphics cards.

## Decision
- **One shared atlas** (`assets/character_normal.png` + `character_material.png`, 4096): the left
  half holds the body (its skin-texture islands repacked), the right half the outfit kit pieces,
  scalp caps and face parts, plus 8 hair-strand tiles. Meshes carry a second UV set for it.
- The material map stores a **zone per texel** (main, trim, leather, metal, hair, skin, ...).
  Each skin's colours and kit material come from `skins_catalog.py` as numbers; the shader turns
  zone + palette into colour, roughness and metalness. Per-skin colour textures stay as they
  were and are not regenerated.
- **Normals come from a "sculpt" evaluated per texel** (muscles, folds, ridges, rivets) plus fine
  detail (weave, grain, stitching, pores), turned into normals by their slope. A Cycles
  high-poly → low-poly normal bake was tried first: smooth subdivision shrank small parts so
  rays missed (garbage normals), simple subdivision baked facets. The per-texel version is the
  same maths without the sampling errors. Ambient occlusion is still baked with Cycles.
- Zones are read with an exact texel fetch (mip-mapped zone numbers would average into wrong
  zones).

## Consequences
- 12 MB of maps instead of gigabytes; fits in memory on any card.
- Skins can't each have unique bumps; variety comes from builds, kit combinations and palette.
- The atlas must be rebuilt (Blender, ~1.5 min) whenever kit geometry changes; the rebuild leaves
  the body's skin-texture UVs untouched (check with `tools/character/glb_compare.py`).
