# Blockfall world textures: tileable 2048x2048 albedo + normal maps for every material.
# Written by code (numpy) so they are reproducible; run: python3 tools/textures/make_textures.py
import os, math
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'assets', 'textures')
N = 2048
rng = np.random.default_rng(1234)


def smooth(t):
    return t * t * (3 - 2 * t)


def value_noise(n, cells, seed):
    """Tileable value noise: a cells x cells random lattice, smoothly interpolated, repeating."""
    r = np.random.default_rng(seed)
    lat = r.random((cells, cells)).astype(np.float32)
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float32)
    fx = xs / n * cells
    fy = ys / n * cells
    x0 = np.floor(fx).astype(int) % cells
    y0 = np.floor(fy).astype(int) % cells
    x1 = (x0 + 1) % cells
    y1 = (y0 + 1) % cells
    tx = smooth(fx - np.floor(fx))
    ty = smooth(fy - np.floor(fy))
    a = lat[y0, x0]; b = lat[y0, x1]; c = lat[y1, x0]; d = lat[y1, x1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def fbm(n, base_cells, octaves, seed, gain=0.5):
    out = np.zeros((n, n), np.float32)
    amp = 1.0
    total = 0.0
    for o in range(octaves):
        out += value_noise(n, base_cells * (2 ** o), seed + o * 17) * amp
        total += amp
        amp *= gain
    return out / total


def worley(n, cells, seed):
    """Tileable cell noise: distance to the nearest of cells x cells jittered points, plus the cell id."""
    r = np.random.default_rng(seed)
    pts = (np.mgrid[0:cells, 0:cells].transpose(1, 2, 0) + r.random((cells, cells, 2))) / cells
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float32) / n
    best = np.full((n, n), 9.0, np.float32)
    second = np.full((n, n), 9.0, np.float32)
    ident = np.zeros((n, n), np.int32)
    cx = np.floor(xs * cells).astype(int)
    cy = np.floor(ys * cells).astype(int)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            gx = (cx + dx) % cells
            gy = (cy + dy) % cells
            px = pts[gy, gx, 1] + (cx + dx - gx) / cells
            py = pts[gy, gx, 0] + (cy + dy - gy) / cells
            d = np.sqrt((xs - px) ** 2 + (ys - py) ** 2)
            closer = d < best
            second = np.where(closer, best, np.minimum(second, d))
            ident = np.where(closer, gy * cells + gx, ident)
            best = np.where(closer, d, best)
    return best, second, ident


def normal_from_height(h, strength):
    """Normal map (tangent space, +y up on the texture) from a tileable height field."""
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    nx = -dx
    ny = -dy
    nz = np.ones_like(h)
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / l, ny / l, nz / l], -1)


def save(name, albedo, height, nstrength):
    os.makedirs(OUT, exist_ok=True)
    a = np.clip(albedo, 0, 1)
    Image.fromarray((a * 255).astype(np.uint8), 'RGB').save(os.path.join(OUT, name + '_albedo.png'), optimize=True)
    nm = normal_from_height(height, nstrength) * 0.5 + 0.5
    Image.fromarray((np.clip(nm, 0, 1) * 255).astype(np.uint8), 'RGB').save(os.path.join(OUT, name + '_normal.png'), optimize=True)
    print('wrote', name)


def gray(v):
    return np.stack([v, v, v], -1)


def tint(h, c0, c1):
    """Colour between c0 and c1 by h (0..1)."""
    h = h[..., None]
    return np.array(c0) * (1 - h) + np.array(c1) * h


# Textures store the *detail* around a mid grey: the game multiplies by the piece's colour, so the
# average must stay near 0.5 for colours to come out right.
def normalise(a, mean=0.5, keep_colour=0.4):
    # Mostly luminance detail (the piece's own colour supplies the hue), scaled so the average
    # brightness is `mean`; the same factor for every channel so hues never flip.
    lum = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
    a = gray(lum) * (1 - keep_colour) + a * keep_colour
    lum = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
    return a * (mean / max(float(lum.mean()), 1e-4))


def wood():
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32) / N
    plank_h = 1 / 6.0
    row = np.floor(ys / plank_h)
    offset = (row % 2) * 0.5
    grain = fbm(N, 4, 5, 10)
    rings = np.sin((xs + offset + grain * 0.6) * 40 + row * 3.7) * 0.5 + 0.5
    fine = fbm(N, 64, 3, 11)
    h = rings * 0.5 + fine * 0.5
    # Gaps between planks.
    gap = np.abs((ys / plank_h) % 1 - 0.5) > 0.47
    ends = np.abs(((xs + offset) * 1.5) % 1 - 0.5) > 0.49
    h = np.where(gap | ends, h * 0.3, h)
    knots, _, _ = worley(N, 5, 12)
    h = h - np.clip(0.06 - knots, 0, 1) * 4
    col = tint(h, (0.36, 0.26, 0.16), (0.66, 0.5, 0.32))
    col = np.where((gap | ends)[..., None], col * 0.5, col)
    save('wood', normalise(col, 0.5), h, 6.0)


def stone():
    d1, d2, ident = worley(N, 10, 20)
    edge = np.clip((d2 - d1) * 14, 0, 1)   # 0 at cell borders
    r = np.random.default_rng(21)
    cell_shade = r.random(10 * 10 + 1)[ident]
    fine = fbm(N, 48, 3, 22)
    h = edge * (0.7 + 0.3 * cell_shade) * (0.85 + 0.15 * fine)
    col = tint(h, (0.28, 0.28, 0.3), (0.62, 0.6, 0.58)) * (0.9 + 0.2 * cell_shade[..., None])
    save('stone', normalise(col, 0.5), h, 5.0)


def brick():
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32) / N
    rows = 16.0
    row = np.floor(ys * rows)
    bx = (xs + (row % 2) * 0.5) * 8.0
    fx = bx % 1
    fy = (ys * rows) % 1
    mortar = (fx < 0.06) | (fx > 0.94) | (fy < 0.1) | (fy > 0.9)
    r = np.random.default_rng(30)
    bid = (row * 1000 + np.floor(bx)).astype(np.int64)
    shade = (np.sin(bid * 12.9898) * 43758.5453) % 1
    fine = fbm(N, 96, 3, 31)
    h = np.where(mortar, 0.15 + fine * 0.1, 0.75 + shade * 0.15 + fine * 0.1)
    col = tint(shade, (0.62, 0.3, 0.24), (0.78, 0.42, 0.3)) * (0.9 + 0.2 * fine[..., None])
    col = np.where(mortar[..., None], np.array((0.72, 0.68, 0.62)) * (0.8 + 0.3 * fine[..., None]), col)
    save('brick', normalise(col, 0.5), h, 5.0)


def metal():
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32) / N
    brushed = fbm(N, 2, 1, 40) * 0.0 + value_noise(N, 512, 41) * 0.3 + value_noise(N, 1024, 42) * 0.2
    brushed = np.repeat(brushed[::1, :1], N, 1) if False else brushed
    # Horizontal brushing: stretch noise along x.
    streak = np.roll(value_noise(N, 256, 43), 0, 1)
    streak = (streak + np.roll(streak, 5, 1) + np.roll(streak, 11, 1) + np.roll(streak, 23, 1)) / 4
    panels = 4.0
    px = (xs * panels) % 1
    py = (ys * panels) % 1
    seam = (px < 0.015) | (py < 0.015)
    rivet = ((px - 0.06) ** 2 + (py - 0.06) ** 2 < 0.0004) | ((px - 0.94) ** 2 + (py - 0.06) ** 2 < 0.0004) | \
            ((px - 0.06) ** 2 + (py - 0.94) ** 2 < 0.0004) | ((px - 0.94) ** 2 + (py - 0.94) ** 2 < 0.0004)
    scratches = np.clip(fbm(N, 20, 2, 44) - 0.62, 0, 1) * 6
    h = 0.6 + streak * 0.2 - scratches * 0.15
    h = np.where(seam, 0.3, h)
    h = np.where(rivet, 0.9, h)
    col = gray(0.5 + streak * 0.25 - scratches * 0.2)
    col = np.where(seam[..., None], col * 0.6, col)
    col = np.where(rivet[..., None], col * 1.2, col)
    save('metal', normalise(col, 0.5), h, 5.0)


def grass():
    blades = fbm(N, 128, 3, 50)
    clumps = fbm(N, 8, 4, 51)
    h = blades * 0.6 + clumps * 0.4
    clover = np.clip(fbm(N, 24, 3, 52) - 0.6, 0, 1) * 3
    col = tint(h, (0.26, 0.5, 0.2), (0.5, 0.74, 0.3))
    col = col * (1 - clover[..., None] * 0.5) + np.array((0.2, 0.42, 0.18)) * clover[..., None] * 0.5
    save('grass', normalise(col, 0.5), h, 3.0)


def leaf():
    d1, d2, ident = worley(N, 24, 60)
    r = np.random.default_rng(61)
    shade = r.random(24 * 24 + 1)[ident]
    h = np.clip(1 - d1 * 18, 0, 1) * (0.7 + 0.3 * shade)
    col = tint(shade, (0.22, 0.45, 0.2), (0.45, 0.7, 0.3)) * (0.75 + 0.35 * h[..., None])
    save('leaf', normalise(col, 0.5), h, 4.0)


def cloth():
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32) / N
    weave = (np.sin(xs * 2 * np.pi * 256) * np.sin(ys * 2 * np.pi * 256)) * 0.5 + 0.5
    fine = fbm(N, 128, 2, 70)
    folds = fbm(N, 4, 3, 71)
    h = weave * 0.35 + fine * 0.25 + folds * 0.4
    col = gray(0.46 + weave * 0.08 + folds * 0.08)
    save('cloth', normalise(col, 0.5), h, 1.5)


def skin():
    pores = value_noise(N, 512, 80) * 0.5 + value_noise(N, 256, 81) * 0.5
    blotch = fbm(N, 6, 3, 82)
    h = pores * 0.7 + blotch * 0.3
    col = gray(0.5) * (0.96 + 0.08 * blotch[..., None]) * (0.98 + 0.04 * pores[..., None])
    col[..., 0] *= 1.02
    save('skin', normalise(col, 0.5), h, 0.8)


def plaster():
    fine = fbm(N, 64, 4, 90)
    cracks = np.clip(0.03 - np.abs(fbm(N, 6, 3, 91) - 0.5), 0, 1) * 30
    stains = fbm(N, 3, 3, 92)
    h = 0.6 + fine * 0.3 - cracks * 0.3
    col = gray(0.5 + fine * 0.12 - cracks * 0.3) * (0.92 + 0.16 * stains[..., None])
    save('plaster', normalise(col, 0.5), h, 2.5)


def roof():
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32) / N
    rows = 20.0
    row = np.floor(ys * rows)
    tx = (xs * 10 + (row % 2) * 0.5) % 1
    ty = (ys * rows) % 1
    # Scalloped tiles: a rounded lower edge.
    curve = np.sqrt(np.clip(1 - (tx - 0.5) ** 2 * 4, 0, 1))
    tile = ty < 0.15 + 0.8 * curve
    shade = (np.sin((row * 10 + np.floor(xs * 10 + (row % 2) * 0.5)) * 12.9898) * 43758.5453) % 1
    fine = fbm(N, 64, 3, 100)
    h = np.where(tile, 0.5 + ty * 0.4 + fine * 0.1, 0.1 + fine * 0.1)
    col = tint(shade, (0.5, 0.22, 0.16), (0.66, 0.32, 0.22)) * (0.85 + 0.3 * ty[..., None])
    col = np.where(tile[..., None], col, np.array((0.3, 0.14, 0.1)))
    save('roof', normalise(col, 0.5), h, 5.0)


def bark():
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float32) / N
    # Vertical ridges: noise stretched along y.
    ridges = value_noise(N, 48, 110)
    ridges = (ridges + np.roll(ridges, 7, 0) + np.roll(ridges, 15, 0) + np.roll(ridges, 29, 0) + np.roll(ridges, 53, 0)) / 5
    cracks = np.clip(0.5 - np.abs(fbm(N, 16, 3, 111) - 0.5) * 6, 0, 1)
    fine = fbm(N, 128, 2, 112)
    h = ridges * 0.6 + cracks * 0.25 + fine * 0.15
    col = tint(h, (0.3, 0.2, 0.12), (0.58, 0.42, 0.26))
    save('bark', normalise(col, 0.5), h, 5.0)


if __name__ == '__main__':
    import sys
    only = sys.argv[1:]
    for f in (wood, stone, brick, metal, grass, leaf, cloth, skin, plaster, roof, bark):
        if not only or f.__name__ in only:
            f()
