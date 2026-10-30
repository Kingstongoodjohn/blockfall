# Blockfall skin catalogue - the one list both the texture painter (Blender script) and the game
# (src/game/skins_data.inc, generated from this file) use.
#
# Each skin: name, shirt, pants, hair, accent, hat colour, hair style, hat, headband, pattern,
# skin tone, rarity.
#   hair style: 0 spiky, 1 flat cap of hair, 2 mohawk, 3 long/back
#   hat:        0 none, 1 baseball cap, 2 beanie, 3 helmet
#   pattern:    0 plain, 1 stripes, 2 camo, 3 checker, 4 hex plates, 5 stars, 6 flames, 7 sash,
#               8 racing stripes, 9 plaid, 10 two-tone, 11 chevrons, 12 lightning, 13 panelled jacket,
#               14 energy cracks, 15 circuit traces, 16 tiger stripes, 17 gradient, 18 pixel camo
#   rarity:     0 uncommon (800), 1 rare (1200), 2 epic (1500), 3 legendary (2000)
RARITY_PRICE = [800, 1200, 1500, 2000]
RARITY_NAME = ['UNCOMMON', 'RARE', 'EPIC', 'LEGENDARY']

SKIN_TONES = [(0.93, 0.72, 0.58), (0.80, 0.56, 0.40), (0.55, 0.36, 0.25), (0.98, 0.82, 0.70),
              (0.68, 0.48, 0.34), (0.90, 0.68, 0.52), (0.84, 0.62, 0.50), (0.45, 0.28, 0.20)]

SKINS = [
    # name,            shirt,              pants,              hair,               accent,             hat col,            hs, hat, band, pat, tone, rar
    ('RECRUIT',        (0.20, 0.45, 0.90), (0.16, 0.18, 0.26), (0.85, 0.70, 0.40), (0.10, 0.22, 0.45), (0.1, 0.2, 0.4),    0, 0, 1, 0, 0, 0),
    ('CRIMSON FANG',   (0.80, 0.12, 0.14), (0.12, 0.10, 0.10), (0.08, 0.07, 0.07), (0.95, 0.70, 0.20), (0.8, 0.1, 0.1),    2, 0, 1, 6, 1, 2),
    ('NIGHT OWL',      (0.12, 0.12, 0.16), (0.10, 0.10, 0.12), (0.55, 0.35, 0.15), (0.30, 0.80, 1.00), (0.1, 0.1, 0.14),   1, 2, 0, 4, 2, 1),
    ('MINT RUNNER',    (0.25, 0.85, 0.65), (0.95, 0.95, 0.95), (0.20, 0.14, 0.10), (0.10, 0.45, 0.35), (0.2, 0.7, 0.55),   0, 1, 0, 1, 3, 0),
    ('SUNBURST',       (0.98, 0.62, 0.12), (0.30, 0.16, 0.08), (0.95, 0.85, 0.30), (0.90, 0.20, 0.30), (0.95, 0.6, 0.1),   2, 0, 1, 6, 4, 2),
    ('VIOLET STORM',   (0.50, 0.20, 0.75), (0.18, 0.12, 0.24), (0.90, 0.90, 0.95), (0.85, 0.45, 1.00), (0.4, 0.15, 0.6),   3, 0, 0, 5, 5, 1),
    ('ARCTIC OPS',     (0.90, 0.92, 0.95), (0.70, 0.74, 0.80), (0.45, 0.20, 0.12), (0.20, 0.40, 0.70), (0.85, 0.88, 0.92), 1, 2, 1, 2, 6, 1),
    ('GOLD RUSH',      (0.95, 0.78, 0.20), (0.15, 0.13, 0.08), (0.08, 0.07, 0.07), (1.00, 0.95, 0.70), (0.9, 0.75, 0.2),   3, 0, 1, 4, 7, 3),
    ('JUNGLE CAT',     (0.30, 0.42, 0.20), (0.25, 0.22, 0.14), (0.15, 0.10, 0.06), (0.75, 0.65, 0.30), (0.3, 0.4, 0.2),    1, 1, 0, 2, 2, 0),
    ('BUBBLEGUM',      (0.98, 0.55, 0.78), (0.35, 0.80, 0.95), (0.98, 0.80, 0.90), (1.00, 1.00, 1.00), (0.98, 0.5, 0.75),  3, 0, 0, 5, 0, 1),
    ('STEEL KNIGHT',   (0.55, 0.58, 0.64), (0.30, 0.32, 0.36), (0.30, 0.25, 0.20), (0.85, 0.30, 0.25), (0.55, 0.58, 0.64), 1, 3, 0, 4, 1, 2),
    ('EMBER',          (0.20, 0.08, 0.06), (0.12, 0.08, 0.06), (0.95, 0.35, 0.10), (1.00, 0.55, 0.10), (0.2, 0.08, 0.06),  2, 0, 0, 6, 3, 3),
    ('TIDE',           (0.10, 0.50, 0.70), (0.08, 0.20, 0.35), (0.10, 0.10, 0.12), (0.60, 0.95, 1.00), (0.1, 0.5, 0.7),    0, 0, 1, 1, 4, 0),
    ('BANANA SPLIT',   (0.98, 0.92, 0.30), (0.55, 0.30, 0.15), (0.35, 0.22, 0.12), (0.95, 0.40, 0.30), (0.98, 0.9, 0.3),   0, 1, 0, 3, 0, 0),
    ('RAVEN',          (0.08, 0.08, 0.10), (0.06, 0.06, 0.08), (0.05, 0.05, 0.06), (0.55, 0.10, 0.70), (0.08, 0.08, 0.1),  3, 0, 0, 0, 7, 2),
    ('SNOW FOX',       (0.95, 0.95, 0.97), (0.85, 0.85, 0.88), (0.95, 0.95, 0.95), (0.95, 0.45, 0.20), (0.95, 0.95, 0.97), 3, 2, 0, 0, 3, 1),
    ('CIRCUIT',        (0.10, 0.14, 0.18), (0.08, 0.10, 0.14), (0.20, 0.60, 0.95), (0.20, 1.00, 0.50), (0.1, 0.14, 0.18),  2, 0, 0, 4, 5, 2),
    ('LUMBERJACK',     (0.80, 0.20, 0.15), (0.20, 0.22, 0.40), (0.40, 0.25, 0.12), (0.10, 0.10, 0.10), (0.8, 0.2, 0.15),   1, 2, 0, 3, 1, 0),
    ('COBALT',         (0.10, 0.25, 0.75), (0.08, 0.12, 0.30), (0.95, 0.90, 0.60), (0.95, 0.85, 0.20), (0.1, 0.25, 0.75),  0, 1, 0, 7, 0, 1),
    ('DESERT HAWK',    (0.80, 0.68, 0.45), (0.55, 0.45, 0.30), (0.25, 0.15, 0.08), (0.45, 0.30, 0.15), (0.8, 0.68, 0.45),  1, 1, 0, 2, 4, 0),
    ('NEON PULSE',     (0.05, 0.05, 0.08), (0.05, 0.05, 0.08), (0.95, 0.20, 0.80), (0.20, 1.00, 0.90), (0.05, 0.05, 0.08), 2, 0, 1, 1, 6, 3),
    ('ROSEWOOD',       (0.55, 0.15, 0.25), (0.25, 0.12, 0.15), (0.30, 0.15, 0.10), (0.95, 0.80, 0.60), (0.55, 0.15, 0.25), 3, 0, 0, 0, 2, 1),
    ('PILOT',          (0.30, 0.35, 0.28), (0.25, 0.28, 0.24), (0.55, 0.40, 0.25), (0.90, 0.70, 0.30), (0.3, 0.3, 0.26),   1, 3, 0, 7, 3, 1),
    ('SURF',           (0.95, 0.50, 0.15), (0.20, 0.65, 0.85), (0.90, 0.80, 0.50), (0.98, 0.95, 0.90), (0.95, 0.5, 0.15),  0, 0, 1, 1, 5, 0),
    ('GRAPHITE',       (0.25, 0.25, 0.28), (0.18, 0.18, 0.20), (0.12, 0.12, 0.12), (0.95, 0.95, 0.95), (0.25, 0.25, 0.28), 1, 0, 0, 4, 1, 1),
    ('WILDFIRE',       (0.95, 0.30, 0.10), (0.20, 0.10, 0.06), (0.10, 0.08, 0.06), (1.00, 0.85, 0.20), (0.95, 0.3, 0.1),   2, 0, 1, 6, 2, 2),
    ('MOSS',           (0.35, 0.55, 0.30), (0.20, 0.28, 0.18), (0.45, 0.30, 0.15), (0.80, 0.75, 0.40), (0.35, 0.55, 0.3),  3, 2, 0, 2, 6, 0),
    ('ORBIT',          (0.15, 0.15, 0.35), (0.10, 0.10, 0.25), (0.90, 0.90, 0.95), (0.70, 0.80, 1.00), (0.15, 0.15, 0.35), 1, 3, 0, 5, 0, 2),
    ('CHERRY BOMB',    (0.90, 0.10, 0.25), (0.95, 0.95, 0.95), (0.10, 0.08, 0.08), (0.10, 0.10, 0.10), (0.9, 0.1, 0.25),   3, 1, 0, 5, 3, 1),
    ('SANDSTORM',      (0.90, 0.80, 0.60), (0.75, 0.65, 0.45), (0.95, 0.90, 0.70), (0.60, 0.40, 0.20), (0.9, 0.8, 0.6),    0, 2, 0, 2, 7, 0),
    ('VOLT',           (0.95, 0.95, 0.20), (0.10, 0.10, 0.12), (0.10, 0.10, 0.12), (0.10, 0.10, 0.12), (0.95, 0.95, 0.2),  2, 0, 0, 7, 4, 1),
    ('GLACIER',        (0.60, 0.85, 0.95), (0.30, 0.50, 0.70), (0.80, 0.90, 0.98), (1.00, 1.00, 1.00), (0.6, 0.85, 0.95),  3, 0, 1, 4, 3, 2),
    ('BRICKLAYER',     (0.75, 0.35, 0.25), (0.35, 0.33, 0.30), (0.20, 0.14, 0.08), (0.95, 0.80, 0.30), (0.75, 0.35, 0.25), 1, 3, 0, 3, 1, 0),
    ('PHANTOM',        (0.25, 0.28, 0.40), (0.15, 0.17, 0.28), (0.85, 0.85, 0.90), (0.60, 0.95, 1.00), (0.25, 0.28, 0.4),  2, 0, 0, 0, 6, 2),
    ('HONEYBEE',       (0.98, 0.75, 0.10), (0.10, 0.10, 0.10), (0.10, 0.08, 0.06), (0.10, 0.10, 0.10), (0.98, 0.75, 0.1),  0, 0, 1, 1, 2, 1),
    ('TOXIC',          (0.30, 0.90, 0.20), (0.12, 0.20, 0.10), (0.20, 0.90, 0.30), (0.95, 0.95, 0.20), (0.3, 0.9, 0.2),    2, 0, 0, 2, 5, 2),
    ('ADMIRAL',        (0.12, 0.18, 0.45), (0.95, 0.95, 0.95), (0.80, 0.80, 0.80), (0.95, 0.85, 0.30), (0.12, 0.18, 0.45), 1, 1, 0, 7, 0, 3),
    ('MIDNIGHT',       (0.06, 0.06, 0.15), (0.05, 0.05, 0.10), (0.15, 0.15, 0.30), (0.90, 0.80, 0.30), (0.06, 0.06, 0.15), 3, 0, 1, 5, 7, 3),
    ('CORAL',          (0.98, 0.45, 0.40), (0.95, 0.90, 0.80), (0.85, 0.55, 0.30), (0.20, 0.80, 0.70), (0.98, 0.45, 0.4),  0, 1, 0, 0, 5, 0),
    ('BLOCKFALL CHAMP',(0.98, 0.78, 0.20), (0.20, 0.15, 0.08), (0.98, 0.78, 0.20), (0.98, 0.95, 0.80), (0.98, 0.78, 0.2),  2, 3, 1, 4, 1, 3),
    ('SPEEDSTER',      (0.95, 0.95, 0.95), (0.12, 0.12, 0.14), (0.10, 0.08, 0.06), (0.90, 0.15, 0.15), (0.95, 0.95, 0.95), 0, 1, 0, 8, 1, 1),
    ('HIGHLANDER',     (0.70, 0.15, 0.18), (0.18, 0.18, 0.22), (0.80, 0.40, 0.15), (0.20, 0.35, 0.20), (0.7, 0.15, 0.18),   1, 0, 0, 9, 3, 0),
    ('SPLIT SECOND',   (0.15, 0.80, 0.95), (0.10, 0.10, 0.12), (0.10, 0.10, 0.12), (0.98, 0.30, 0.60), (0.15, 0.8, 0.95),   2, 0, 1, 10, 0, 2),
    ('ARROWHEAD',      (0.35, 0.30, 0.25), (0.20, 0.18, 0.15), (0.25, 0.15, 0.08), (0.90, 0.80, 0.55), (0.35, 0.3, 0.25),   1, 1, 0, 11, 2, 0),
    ('THUNDERCLAP',    (0.12, 0.10, 0.30), (0.08, 0.08, 0.18), (0.98, 0.95, 0.40), (0.98, 0.95, 0.40), (0.12, 0.1, 0.3),    0, 0, 1, 12, 4, 3),
    ('RIDGE RUNNER',   (0.85, 0.45, 0.15), (0.25, 0.22, 0.20), (0.30, 0.20, 0.10), (0.95, 0.90, 0.80), (0.85, 0.45, 0.15),  1, 2, 0, 13, 5, 1),
    ('LAVENDER',       (0.75, 0.65, 0.95), (0.30, 0.25, 0.40), (0.95, 0.92, 0.98), (0.40, 0.25, 0.65), (0.75, 0.65, 0.95),  3, 0, 0, 1, 6, 0),
    ('OUTLAW',         (0.35, 0.20, 0.12), (0.25, 0.18, 0.12), (0.12, 0.08, 0.05), (0.80, 0.65, 0.35), (0.35, 0.2, 0.12),   1, 1, 0, 10, 7, 1),
    ('SKYLINE',        (0.20, 0.30, 0.50), (0.15, 0.20, 0.35), (0.90, 0.90, 0.92), (0.95, 0.60, 0.20), (0.2, 0.3, 0.5),     0, 3, 0, 13, 0, 2),
    ('CANDY CANE',     (0.95, 0.95, 0.95), (0.90, 0.12, 0.15), (0.95, 0.95, 0.95), (0.90, 0.12, 0.15), (0.95, 0.95, 0.95),  1, 2, 0, 8, 3, 1),
    ('REDWOOD',        (0.55, 0.25, 0.15), (0.25, 0.30, 0.20), (0.35, 0.20, 0.10), (0.95, 0.85, 0.50), (0.55, 0.25, 0.15),  2, 0, 0, 9, 2, 0),
    ('STATIC',         (0.08, 0.08, 0.10), (0.12, 0.12, 0.14), (0.95, 0.95, 0.95), (0.95, 0.95, 0.95), (0.08, 0.08, 0.1),   3, 0, 1, 12, 1, 2),
    ('MARINER',        (0.95, 0.95, 0.95), (0.15, 0.25, 0.50), (0.85, 0.65, 0.30), (0.15, 0.25, 0.50), (0.15, 0.25, 0.5),   0, 1, 0, 1, 5, 0),
    ('INFERNO KING',   (0.10, 0.05, 0.05), (0.15, 0.06, 0.04), (0.98, 0.45, 0.10), (0.98, 0.60, 0.10), (0.1, 0.05, 0.05),   2, 3, 0, 6, 7, 3),
    ('MEADOW',         (0.90, 0.85, 0.55), (0.35, 0.50, 0.30), (0.50, 0.30, 0.15), (0.90, 0.40, 0.50), (0.9, 0.85, 0.55),   3, 2, 0, 5, 3, 0),
    ('BLUE STEEL',     (0.30, 0.45, 0.65), (0.20, 0.25, 0.35), (0.20, 0.20, 0.22), (0.80, 0.85, 0.90), (0.3, 0.45, 0.65),   1, 3, 0, 4, 4, 2),
    ('SUNSET',         (0.95, 0.40, 0.30), (0.40, 0.15, 0.35), (0.20, 0.12, 0.08), (0.98, 0.85, 0.30), (0.95, 0.4, 0.3),    3, 0, 1, 10, 6, 1),
    ('GHOST',          (0.80, 0.82, 0.85), (0.55, 0.57, 0.62), (0.90, 0.90, 0.92), (0.30, 0.35, 0.45), (0.8, 0.82, 0.85),   2, 0, 0, 2, 0, 1),
    ('TANGERINE',      (0.98, 0.60, 0.15), (0.95, 0.95, 0.95), (0.10, 0.08, 0.06), (0.20, 0.70, 0.40), (0.98, 0.6, 0.15),   0, 1, 0, 11, 2, 0),
    ('SHADOW OPS',     (0.12, 0.14, 0.12), (0.10, 0.12, 0.10), (0.08, 0.06, 0.05), (0.60, 0.20, 0.20), (0.12, 0.14, 0.12),  1, 3, 0, 2, 1, 2),
    ('BERRY',          (0.60, 0.15, 0.45), (0.20, 0.10, 0.20), (0.15, 0.10, 0.12), (0.95, 0.80, 0.90), (0.6, 0.15, 0.45),   3, 1, 0, 3, 5, 1),
    ('PRISM',          (0.95, 0.95, 0.95), (0.20, 0.20, 0.25), (0.40, 0.90, 0.95), (0.95, 0.30, 0.90), (0.95, 0.95, 0.95),  2, 0, 1, 8, 3, 3),
    ('DRIFTER',        (0.50, 0.50, 0.45), (0.35, 0.32, 0.28), (0.35, 0.30, 0.25), (0.85, 0.70, 0.40), (0.5, 0.5, 0.45),    0, 2, 0, 13, 6, 0),
    ('ROYAL GUARD',    (0.75, 0.10, 0.15), (0.10, 0.10, 0.30), (0.10, 0.08, 0.06), (0.98, 0.85, 0.30), (0.75, 0.1, 0.15),   1, 3, 0, 7, 4, 3),
    # ---- v15: 30 more. Each one started as a written concept (the comment), then got a palette,
    # a body build and outfit pieces (OUTFITS), a texture pattern and, for some, emission FX (FX).
    # name,            shirt,              pants,              hair,               accent,             hat col,            hs, hat, band, pat, tone, rar
    ('MAGMA CORE',     (0.10, 0.08, 0.08), (0.08, 0.06, 0.06), (0.15, 0.08, 0.05), (1.00, 0.45, 0.08), (0.1, 0.08, 0.08),  2, 3, 0, 14, 4, 3),  # obsidian armour, lava glowing through the cracks
    ('FROSTBYTE',      (0.85, 0.93, 0.98), (0.55, 0.70, 0.82), (0.75, 0.90, 1.00), (0.25, 0.90, 1.00), (0.85, 0.93, 0.98), 0, 0, 0, 15, 3, 2),  # ice-white hacker, cyan circuits that light up
    ('NIGHT CIRCUIT',  (0.07, 0.09, 0.16), (0.05, 0.06, 0.10), (0.10, 0.10, 0.12), (0.20, 1.00, 0.45), (0.07, 0.09, 0.16), 1, 2, 0, 15, 1, 2),  # stealth operative with green signal lines
    ('SAND VIPER',     (0.78, 0.66, 0.42), (0.42, 0.40, 0.24), (0.20, 0.12, 0.06), (0.30, 0.22, 0.10), (0.78, 0.66, 0.42), 1, 0, 0, 16, 4, 1),  # desert hunter in snake-stripe wrap
    ('DAWN RUNNER',    (1.00, 0.55, 0.20), (0.30, 0.18, 0.30), (0.95, 0.80, 0.55), (1.00, 0.35, 0.55), (1.0, 0.55, 0.2),   0, 0, 1, 17, 0, 0),  # marathon kit fading from sunrise orange to pink
    ('PIXEL PUNK',     (0.95, 0.35, 0.70), (0.12, 0.12, 0.16), (0.25, 0.95, 0.85), (0.25, 0.95, 0.85), (0.95, 0.35, 0.7),  2, 0, 0, 18, 5, 1),  # arcade rebel in pixel camo with a teal mohawk
    ('STORM CHASER',   (0.32, 0.32, 0.40), (0.18, 0.18, 0.24), (0.85, 0.85, 0.90), (0.70, 0.55, 1.00), (0.32, 0.32, 0.4),  3, 0, 0, 12, 6, 2),  # weather hunter with a glowing bolt on the chest
    ('JADE SENTINEL',  (0.12, 0.45, 0.32), (0.10, 0.22, 0.16), (0.08, 0.07, 0.06), (0.95, 0.78, 0.30), (0.12, 0.45, 0.32), 1, 3, 0, 4, 7, 3),   # temple guardian in jade scale armour and gold trim
    ('HARBOR PILOT',   (0.12, 0.20, 0.40), (0.15, 0.17, 0.25), (0.55, 0.38, 0.20), (1.00, 0.50, 0.10), (0.12, 0.2, 0.4),   0, 1, 0, 0, 3, 0),   # coast-guard pilot with an orange life vest
    ('CANYON SCOUT',   (0.62, 0.32, 0.18), (0.55, 0.48, 0.34), (0.35, 0.22, 0.12), (0.92, 0.82, 0.60), (0.62, 0.32, 0.18), 1, 1, 0, 13, 2, 0),  # red-rock explorer with a canvas pack
    ('VOID WALKER',    (0.05, 0.04, 0.08), (0.04, 0.03, 0.06), (0.30, 0.20, 0.45), (0.70, 0.30, 1.00), (0.05, 0.04, 0.08), 3, 0, 0, 14, 7, 3),  # hooded wanderer leaking violet light
    ('TUNDRA MEDIC',   (0.92, 0.93, 0.95), (0.72, 0.74, 0.78), (0.40, 0.25, 0.15), (0.85, 0.12, 0.12), (0.92, 0.93, 0.95), 1, 2, 0, 0, 0, 1),   # cold-weather medic, red cross trim
    ('NEON RACER',     (0.06, 0.06, 0.07), (0.06, 0.06, 0.07), (0.95, 0.90, 0.20), (1.00, 0.92, 0.10), (0.06, 0.06, 0.07), 0, 3, 0, 8, 1, 2),   # night street racer, glowing yellow stripes
    ('MOSS GOLEM',     (0.30, 0.42, 0.22), (0.24, 0.30, 0.18), (0.20, 0.32, 0.12), (0.60, 0.55, 0.30), (0.30, 0.42, 0.22), 1, 2, 0, 2, 2, 1),   # forest giant overgrown with moss
    ('CORAL DIVER',    (0.10, 0.65, 0.70), (0.08, 0.25, 0.35), (0.85, 0.55, 0.40), (1.00, 0.45, 0.40), (0.1, 0.65, 0.7),   3, 0, 0, 17, 5, 1),  # reef diver fading from teal to coral
    ('IRON FORGE',     (0.24, 0.24, 0.27), (0.16, 0.16, 0.18), (0.10, 0.08, 0.07), (1.00, 0.55, 0.15), (0.24, 0.24, 0.27), 1, 3, 0, 4, 4, 2),   # blacksmith in plate armour still glowing hot
    ('SKY MARSHAL',    (0.45, 0.68, 0.92), (0.90, 0.92, 0.95), (0.30, 0.20, 0.12), (0.95, 0.80, 0.30), (0.45, 0.68, 0.92), 0, 1, 0, 13, 3, 1),  # airfield marshal in a sky-blue flight jacket
    ('STRIPE STRIKER', (0.80, 0.12, 0.12), (0.10, 0.10, 0.12), (0.10, 0.08, 0.07), (0.08, 0.08, 0.09), (0.8, 0.12, 0.12),   2, 0, 1, 16, 6, 0),  # brawler in red-and-black tiger stripes
    ('LUNAR SCOUT',    (0.72, 0.75, 0.82), (0.30, 0.35, 0.48), (0.90, 0.92, 0.95), (0.45, 0.70, 1.00), (0.72, 0.75, 0.82), 3, 3, 0, 15, 0, 2),  # moon-base scout with glowing visor eyes
    ('BRAMBLE',        (0.40, 0.30, 0.18), (0.26, 0.32, 0.18), (0.30, 0.18, 0.08), (0.55, 0.65, 0.30), (0.4, 0.3, 0.18),   1, 2, 0, 2, 2, 0),   # woodland trapper in leaf camo
    ('GLITCH',         (0.40, 0.15, 0.60), (0.08, 0.06, 0.10), (0.30, 1.00, 0.40), (0.30, 1.00, 0.40), (0.4, 0.15, 0.6),   2, 0, 0, 18, 5, 3),  # a game character that broke: flickering pixels
    ('SUNFORGE',       (0.98, 0.85, 0.40), (0.95, 0.93, 0.88), (0.98, 0.90, 0.60), (1.00, 0.75, 0.20), (0.98, 0.85, 0.4),  3, 3, 0, 17, 1, 3),  # golden sun knight with a radiant trim
    ('TRAIL BLAZER',   (0.95, 0.45, 0.10), (0.20, 0.20, 0.22), (0.25, 0.15, 0.08), (0.95, 0.95, 0.95), (0.95, 0.45, 0.1),  0, 1, 0, 1, 3, 0),   # hiker in an orange shell jacket
    ('RIPTIDE',        (0.06, 0.22, 0.50), (0.05, 0.15, 0.30), (0.10, 0.10, 0.12), (0.30, 0.90, 0.95), (0.06, 0.22, 0.5),  0, 0, 1, 11, 4, 1),  # surf rescue in wave chevrons
    ('EMBERWOLF',      (0.16, 0.15, 0.16), (0.12, 0.11, 0.12), (0.55, 0.30, 0.12), (1.00, 0.40, 0.05), (0.16, 0.15, 0.16), 1, 0, 0, 14, 6, 2),  # hooded hunter with smouldering cracks
    ('POLAR NIGHT',    (0.92, 0.94, 0.97), (0.12, 0.16, 0.32), (0.80, 0.80, 0.85), (0.12, 0.16, 0.32), (0.92, 0.94, 0.97), 3, 2, 0, 9, 3, 1),   # arctic trekker in a navy plaid parka
    ('CIPHER',         (0.06, 0.06, 0.07), (0.92, 0.92, 0.93), (0.06, 0.06, 0.07), (0.95, 0.95, 0.98), (0.06, 0.06, 0.07), 0, 0, 0, 15, 7, 2),  # codebreaker in black and white with lit circuits
    ('MARIGOLD',       (0.98, 0.75, 0.10), (0.45, 0.10, 0.18), (0.30, 0.15, 0.10), (0.45, 0.10, 0.18), (0.98, 0.75, 0.1),  3, 0, 0, 5, 5, 0),   # festival dancer in marigold and maroon
    ('TITAN GUARD',    (0.30, 0.33, 0.38), (0.18, 0.20, 0.25), (0.15, 0.12, 0.10), (0.30, 0.55, 1.00), (0.30, 0.33, 0.38), 1, 3, 0, 4, 1, 2),   # heavy siege trooper in gunmetal plates
    ('AURORA',         (0.10, 0.75, 0.70), (0.25, 0.15, 0.45), (0.90, 0.95, 1.00), (0.75, 0.45, 1.00), (0.1, 0.75, 0.7),   3, 0, 0, 17, 0, 3),  # northern-lights mage, cape shimmering teal to violet
]

PATTERN_NAME = ['PLAIN', 'STRIPES', 'CAMO', 'CHECKER', 'HEX', 'STARS', 'FLAMES', 'SASH', 'RACING', 'PLAID', 'TWO-TONE', 'CHEVRONS', 'LIGHTNING', 'PANELLED',
                'CRACKS', 'CIRCUIT', 'TIGER', 'GRADIENT', 'PIXELS']

# ---- outfits: body build, outfit kit pieces and what the main kit pieces are made of.
# The kit pieces are modelled once (tools/character/kit.py) and shared; each skin wears its own
# combination, coloured from its palette (shirt = main, pants = secondary, accent = trim).
BUILDS = ['ATHLETIC', 'HEAVY', 'SLIM']
KIT = ['jacket', 'hood', 'vest', 'pauldrons', 'gauntlets', 'shinguards', 'boots', 'belt', 'cape', 'scarf',
       'backpack', 'goggles', 'mask', 'holster']
MATERIALS = ['cloth', 'leather', 'metal', 'rubber']
OUTFITS = {
    # name:            (build, [kit pieces], main material)
    'RECRUIT':         (0, ['belt', 'boots'], 'cloth'),
    'CRIMSON FANG':    (0, ['jacket', 'scarf', 'gauntlets'], 'leather'),
    'NIGHT OWL':       (2, ['hood', 'mask', 'cape'], 'cloth'),
    'MINT RUNNER':     (2, ['belt'], 'rubber'),
    'SUNBURST':        (0, ['vest', 'goggles'], 'rubber'),
    'VIOLET STORM':    (2, ['cape', 'gauntlets'], 'cloth'),
    'ARCTIC OPS':      (1, ['vest', 'hood', 'backpack', 'boots'], 'rubber'),
    'GOLD RUSH':       (1, ['pauldrons', 'belt', 'gauntlets'], 'metal'),
    'JUNGLE CAT':      (0, ['vest', 'holster', 'boots'], 'cloth'),
    'BUBBLEGUM':       (2, ['backpack'], 'rubber'),
    'STEEL KNIGHT':    (1, ['pauldrons', 'gauntlets', 'shinguards', 'belt', 'cape'], 'metal'),
    'EMBER':           (0, ['jacket', 'mask'], 'leather'),
    'TIDE':            (2, ['goggles', 'belt'], 'rubber'),
    'BANANA SPLIT':    (1, ['backpack', 'boots'], 'cloth'),
    'RAVEN':           (2, ['cape', 'hood', 'mask'], 'cloth'),
    'SNOW FOX':        (2, ['scarf', 'jacket', 'boots'], 'cloth'),
    'CIRCUIT':         (0, ['vest', 'goggles', 'gauntlets'], 'rubber'),
    'LUMBERJACK':      (1, ['jacket', 'boots', 'belt'], 'cloth'),
    'COBALT':          (0, ['pauldrons', 'belt'], 'metal'),
    'DESERT HAWK':     (0, ['scarf', 'goggles', 'holster', 'boots'], 'cloth'),
    'NEON PULSE':      (2, ['jacket', 'mask'], 'rubber'),
    'ROSEWOOD':        (2, ['cape', 'belt'], 'leather'),
    'PILOT':           (0, ['jacket', 'goggles', 'scarf', 'holster'], 'leather'),
    'SURF':            (2, ['goggles'], 'rubber'),
    'GRAPHITE':        (1, ['vest', 'holster', 'shinguards'], 'rubber'),
    'WILDFIRE':        (0, ['jacket', 'gauntlets', 'boots'], 'leather'),
    'MOSS':            (1, ['hood', 'backpack', 'boots'], 'cloth'),
    'ORBIT':           (0, ['vest', 'backpack', 'gauntlets', 'boots'], 'rubber'),
    'CHERRY BOMB':     (2, ['jacket', 'belt'], 'leather'),
    'SANDSTORM':       (0, ['scarf', 'mask', 'goggles'], 'cloth'),
    'VOLT':            (2, ['gauntlets', 'goggles'], 'rubber'),
    'GLACIER':         (1, ['pauldrons', 'cape'], 'metal'),
    'BRICKLAYER':      (1, ['vest', 'belt', 'boots', 'gauntlets'], 'cloth'),
    'PHANTOM':         (2, ['hood', 'cape', 'mask'], 'cloth'),
    'HONEYBEE':        (2, ['backpack', 'goggles'], 'rubber'),
    'TOXIC':           (0, ['vest', 'mask', 'gauntlets'], 'rubber'),
    'ADMIRAL':         (1, ['jacket', 'belt', 'pauldrons'], 'cloth'),
    'MIDNIGHT':        (0, ['cape', 'hood'], 'cloth'),
    'CORAL':           (2, ['scarf'], 'cloth'),
    'BLOCKFALL CHAMP': (1, ['pauldrons', 'cape', 'belt', 'gauntlets', 'shinguards'], 'metal'),
    'SPEEDSTER':       (2, ['goggles', 'boots'], 'rubber'),
    'HIGHLANDER':      (1, ['belt', 'scarf', 'boots'], 'cloth'),
    'SPLIT SECOND':    (2, ['jacket', 'gauntlets'], 'rubber'),
    'ARROWHEAD':       (0, ['hood', 'holster', 'gauntlets', 'boots'], 'leather'),
    'THUNDERCLAP':     (1, ['pauldrons', 'cape', 'gauntlets'], 'metal'),
    'RIDGE RUNNER':    (0, ['jacket', 'backpack', 'boots'], 'cloth'),
    'LAVENDER':        (2, ['scarf', 'belt'], 'cloth'),
    'OUTLAW':          (0, ['jacket', 'holster', 'scarf', 'mask'], 'leather'),
    'SKYLINE':         (0, ['vest', 'goggles', 'backpack'], 'rubber'),
    'CANDY CANE':      (1, ['scarf', 'boots'], 'cloth'),
    'REDWOOD':         (1, ['jacket', 'boots', 'backpack'], 'leather'),
    'STATIC':          (2, ['hood', 'gauntlets'], 'rubber'),
    'MARINER':         (0, ['jacket', 'belt'], 'cloth'),
    'INFERNO KING':    (1, ['pauldrons', 'cape', 'gauntlets', 'belt'], 'metal'),
    'MEADOW':          (2, ['hood', 'backpack'], 'cloth'),
    'BLUE STEEL':      (1, ['pauldrons', 'shinguards', 'gauntlets'], 'metal'),
    'SUNSET':          (2, ['scarf', 'goggles'], 'cloth'),
    'GHOST':           (2, ['hood', 'cape'], 'cloth'),
    'TANGERINE':       (0, ['vest', 'boots'], 'rubber'),
    'SHADOW OPS':      (1, ['vest', 'mask', 'holster', 'gauntlets', 'boots'], 'rubber'),
    'BERRY':           (2, ['jacket', 'belt'], 'leather'),
    'PRISM':           (0, ['cape', 'pauldrons', 'gauntlets'], 'metal'),
    'DRIFTER':         (0, ['hood', 'scarf', 'backpack', 'boots'], 'cloth'),
    'ROYAL GUARD':     (1, ['pauldrons', 'belt', 'cape', 'shinguards'], 'metal'),
    'MAGMA CORE':      (1, ['pauldrons', 'gauntlets', 'shinguards', 'belt'], 'metal'),
    'FROSTBYTE':       (2, ['goggles', 'backpack', 'gauntlets'], 'rubber'),
    'NIGHT CIRCUIT':   (0, ['vest', 'mask', 'holster', 'boots'], 'rubber'),
    'SAND VIPER':      (0, ['scarf', 'goggles', 'holster', 'boots'], 'cloth'),
    'DAWN RUNNER':     (2, ['belt', 'backpack'], 'rubber'),
    'PIXEL PUNK':      (2, ['jacket', 'gauntlets'], 'leather'),
    'STORM CHASER':    (0, ['hood', 'cape', 'goggles'], 'cloth'),
    'JADE SENTINEL':   (1, ['pauldrons', 'shinguards', 'belt', 'cape'], 'metal'),
    'HARBOR PILOT':    (0, ['vest', 'goggles', 'boots'], 'rubber'),
    'CANYON SCOUT':    (0, ['scarf', 'holster', 'backpack', 'boots'], 'cloth'),
    'VOID WALKER':     (2, ['hood', 'mask', 'cape'], 'cloth'),
    'TUNDRA MEDIC':    (1, ['backpack', 'belt', 'boots', 'gauntlets'], 'rubber'),
    'NEON RACER':      (2, ['goggles', 'gauntlets', 'boots'], 'rubber'),
    'MOSS GOLEM':      (1, ['pauldrons', 'shinguards', 'backpack'], 'leather'),
    'CORAL DIVER':     (2, ['goggles', 'backpack', 'belt'], 'rubber'),
    'IRON FORGE':      (1, ['pauldrons', 'gauntlets', 'belt', 'boots'], 'metal'),
    'SKY MARSHAL':     (0, ['jacket', 'belt', 'holster'], 'leather'),
    'STRIPE STRIKER':  (0, ['gauntlets', 'boots'], 'rubber'),
    'LUNAR SCOUT':     (2, ['backpack', 'goggles', 'shinguards'], 'metal'),
    'BRAMBLE':         (0, ['hood', 'scarf', 'boots', 'holster'], 'cloth'),
    'GLITCH':          (2, ['mask', 'jacket'], 'rubber'),
    'SUNFORGE':        (1, ['pauldrons', 'cape', 'gauntlets', 'belt'], 'metal'),
    'TRAIL BLAZER':    (0, ['backpack', 'boots', 'belt'], 'cloth'),
    'RIPTIDE':         (2, ['goggles', 'vest'], 'rubber'),
    'EMBERWOLF':       (0, ['hood', 'gauntlets', 'scarf'], 'leather'),
    'POLAR NIGHT':     (1, ['hood', 'scarf', 'backpack', 'boots'], 'cloth'),
    'CIPHER':          (0, ['vest', 'mask', 'holster'], 'rubber'),
    'MARIGOLD':        (2, ['scarf', 'belt'], 'cloth'),
    'TITAN GUARD':     (1, ['pauldrons', 'gauntlets', 'shinguards', 'belt'], 'metal'),
    'AURORA':          (2, ['cape', 'goggles'], 'cloth'),
}

# ---- emission FX: parts that give off their own light (bit 1: trim and accent paint, e.g.
# glowing piping, energy cracks and circuit lines; bit 2: eyes and goggle lenses).
FX_TRIM, FX_EYES = 1, 2
FX = {
    'MAGMA CORE': FX_TRIM | FX_EYES, 'FROSTBYTE': FX_TRIM, 'NIGHT CIRCUIT': FX_TRIM | FX_EYES, 'STORM CHASER': FX_TRIM,
    'JADE SENTINEL': FX_TRIM, 'VOID WALKER': FX_TRIM | FX_EYES, 'NEON RACER': FX_TRIM, 'IRON FORGE': FX_TRIM,
    'LUNAR SCOUT': FX_EYES, 'GLITCH': FX_TRIM | FX_EYES, 'SUNFORGE': FX_TRIM, 'EMBERWOLF': FX_TRIM | FX_EYES,
    'CIPHER': FX_TRIM, 'AURORA': FX_TRIM,
}


def outfit(name):
    """(build, kit bitmask, material index) for a skin."""
    build, pieces, mat = OUTFITS[name]
    mask = 0
    for p in pieces:
        mask |= 1 << KIT.index(p)
    return build, mask, MATERIALS.index(mat)


def slug(name):
    return name.lower().replace(' ', '_')


def write_cpp(path):
    lines = ['// Generated by tools/character/skins_catalog.py - do not edit by hand.',
             '// name, shirt, pants, hair, accent, hat colour, hair style, hat, headband, pattern, skin tone, rarity,',
             '// body build, outfit kit pieces (bit per KIT entry), kit material, emission FX (1 trim, 2 eyes)']
    for s in SKINS:
        name, shirt, pants, hair, accent, hat, hs, hatk, band, pat, tone, rar = s
        build, kit, mat = outfit(name)
        c = lambda v: '{%.2ff, %.2ff, %.2ff, 1}' % v
        lines.append('    {"%s", %s, %s, %s, %s, %s, %d, %d, %s, %d, %d, %d, %d, 0x%04xu, %d, %d},' % (
            name, c(shirt), c(pants), c(hair), c(accent), c(hat), hs, hatk, 'true' if band else 'false', pat, tone, rar,
            build, kit, mat, FX.get(name, 0)))
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    import os, sys
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'game', 'skins_data.inc')
    write_cpp(out)
    print('wrote', out, len(SKINS), 'skins')
