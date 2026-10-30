# 03 — v15: fix animations, realistic skins, occlusion culling

**Asks (2026-10-30):** "Fix animations", "Make the skins as realistic as this" (a photo of a
Fortnite-style skier skin in a fitted suit: front panel, stripes, knee pad, thigh straps,
gloves, laced boots), "Use occlusion culling", "Upload this to GitHub", and "Why does this say
it's a virus. Fix that" (SmartScreen blue box + a block).

**Found by measuring** (`tools/character/anim_check.py` on the skinned mesh): soles 26 cm under
the ground when crouching, 13 cm in the run, feet sliding in the sprint, and forward-running legs
when strafing or backing up.

**Done:** leg IK + step-cycle gaits matched to per-gait stride rates; directional legs in the
animation system; tailored outfit painting for all 64 skins; CPU occlusion culling with
shadow-only lists (ADR 0004); exe icon/version info/manifest and README guidance on the
warnings. Tests pass; demo screenshots checked; exe rebuilt; pushed.
