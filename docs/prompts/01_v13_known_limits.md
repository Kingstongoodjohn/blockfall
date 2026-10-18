# 01 — v13: fix the known limits

**Goal:** Remove the four items from the README's "Known limits" / stale sections.

**Asked for:** "do all of those":
1. Sound (choice made: XAudio2 + sounds synthesised in code; minimal CMake edits allowed).
2. Bot pathfinding.
3. Door/window shape rules for wall edits.
4. Clean up the stale README sections (trust the code, not the old text).

**Found along the way:** `Blockfall.exe` didn't compile with MSVC (error C2026, the scene shader
was longer than one string literal allows). Fixed first, as its own commit.

**Done means:** each feature is its own commit with tests, all tests pass, the `--shots` demo
runs, README and CLAUDE.md describe the new state, and everything is pushed. `bin/Blockfall.exe`
is not rebuilt (that needs a separate request for a release build).
