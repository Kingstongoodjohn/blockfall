# 04 — v16: 30 more skins, K-Bucks for real money

**Asks (2026-10-30):** "Make Kbucks purchases for the same price as the old vbuck prices" (chosen:
real payments with Stripe), and "Make 30 more skins" following a described Fortnite pipeline
(concept → 3D model → D/N/M textures + emission FX → rig/animation → engine).

**Done:** 30 written concepts mapped onto the engine's pipeline (palette, build + outfit kit,
painted diffuse with five new patterns, shared normal/material atlas, new emission FX for trim
and eyes, shared rig); owned skins widened to 128 with backward-compatible files; a FastAPI +
Stripe Checkout server with claim-once purchases and webhook verification, a threaded game
client with a GET K-BUCKS screen, WinHTTP transport and browser opening; purchases survive a
restart. Packs: 1,000/$7.99, 2,800/$19.99, 5,000/$31.99, 13,500/$79.99 (pre-Oct-2023 V-Bucks).

**Not done:** deploying the server (needs the user's host and Stripe keys); server-side accounts.
