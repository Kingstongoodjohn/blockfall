# 0001 — Commit the prebuilt Blockfall.exe

**Status:** Accepted (recorded 2026-10-18; the practice predates this file)

## Context
Building Blockfall needs Visual Studio with the C++ workload, which is a large install most
players don't have.

## Decision
Keep a Release build at `bin/Blockfall.exe` in the repo, so anyone can download the repo and
double-click to play. `build/` output stays gitignored.

## Consequences
- Anyone can play without building.
- Each committed `.exe` (~4 MB) is stored in git history forever, so only commit it for real
  releases, after the tests pass.
- The committed `.exe` can fall behind the source. Rebuild it when releasing.
