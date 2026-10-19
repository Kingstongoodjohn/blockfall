# 02 — v14: more realistic characters

**Goal:** Do what makes Fortnite skins look polished: (1) each skin its own sculpted model with
baked normal maps, (2) several texture layers per skin (PBR), (3) hair cards, swinging capes,
hair and straps, and faces with their own rig.

**Choices made with the user:**
- Map storage: 4096 asked for; per-skin 4096 measured at ~3.7 GB, so a **shared 4096 atlas** was
  chosen instead (see ADR 0003).
- Models: an **outfit kit** (14 pieces, 3 builds) combined per skin rather than 64 unique models.
- Look: a **Stylized / Realistic** setting.
- Pillow installed into Blender's Python (user site-packages, used via PYTHONPATH).

**Done means:** pipeline and game committed separately, each building and passing tests;
assets rebuilt (skin textures untouched); close-up demo shots in both styles checked by eye;
README, CLAUDE.md and an ADR updated; pushed. `bin/Blockfall.exe` not rebuilt.
