# 00 — Project setup

**Goal:** Bring the existing Blockfall repo up to the standard project template, so every
future Claude Code session starts from a shared, written-down understanding of the project.

**Source:** the "Claude Code Setup — Bootstrap Instructions" guide (Phase 4: project template).

**Asked for:**
- Clone `Kingstongoodjohn/blockfall` into `Desktop\Code`.
- Add a project `CLAUDE.md` describing the stack, how to build/test, domain concepts, key
  decisions, hard rules, gotchas and current state.
- Add `docs/prompts/` and `docs/decisions/`.
- Make `.gitignore` cover `.env` and editor/build junk; add a `.env.example`.
- Don't change any game code.

**Done means:** files committed with explicit paths, tests still pass, pushed to GitHub.
