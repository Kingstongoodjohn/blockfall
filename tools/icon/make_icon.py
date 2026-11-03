# Makes src/platform/res/blockfall.ico:  python tools/icon/make_icon.py src/platform/res/blockfall.ico
# (also writes a 512 px preview PNG next to it).
# Blockfall app icon: an original chunky, slanted block-letter B made of straight-edged shapes
# (like build pieces), gold-to-orange, thick dark outline and a cyan drop edge, on a navy tile.
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 1024  # draw big, then shrink for each icon size

def letter_mask(scale=1.0, dx=0, dy=0):
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    sk = 0.18                        # slant: x shifts right as y goes up
    def P(x, y):
        cx, cy = S / 2, S / 2
        x, y = (x - 512) * scale + cx + dx, (y - 512) * scale + cy + dy
        return (x + (S / 2 - y) * sk, y)
    # Outer B: a stem plus two bowls with clipped (bevelled) corners, all straight edges.
    outer = [(250, 170), (640, 170), (720, 250), (720, 430), (668, 482), (740, 554), (740, 774),
             (660, 854), (250, 854)]
    d.polygon([P(*p) for p in outer], fill=255)
    # The two counters (holes), also bevelled.
    for hole in ([(410, 300), (560, 300), (585, 325), (585, 405), (560, 430), (410, 430)],
                 [(410, 560), (575, 560), (600, 585), (600, 700), (575, 725), (410, 725)]):
        d.polygon([P(*p) for p in hole], fill=0)
    return m

def gradient(top, bottom):
    g = Image.new('RGB', (S, S))
    d = ImageDraw.Draw(g)
    for y in range(S):
        t = y / (S - 1)
        d.line([(0, y), (S, y)], fill=tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3)))
    return g

def build():
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    # Tile: rounded square, navy with a lighter top.
    tile = Image.new('L', (S, S), 0)
    ImageDraw.Draw(tile).rounded_rectangle([24, 24, S - 24, S - 24], radius=190, fill=255)
    bg = gradient((44, 70, 140), (14, 20, 44))
    img.paste(bg, (0, 0), tile)
    # Faint build grid on the tile.
    grid = Image.new('L', (S, S), 0)
    gd = ImageDraw.Draw(grid)
    for k in range(0, S, 128):
        gd.line([(k, 0), (k, S)], fill=38, width=6)
        gd.line([(0, k), (S, k)], fill=38, width=6)
    grid = ImageChops.multiply(grid, tile)
    img.paste((140, 180, 255), (0, 0), grid)
    # Letter: cyan drop edge, dark outline, gold-orange face, a light bevel at the top.
    base = letter_mask()
    drop = letter_mask(dx=26, dy=34)
    outline = base.filter(ImageFilter.MaxFilter(41))
    drop_out = drop.filter(ImageFilter.MaxFilter(41))
    img.paste((10, 12, 24), (0, 0), drop_out)
    img.paste((40, 200, 235), (0, 0), drop)
    img.paste((10, 12, 24), (0, 0), outline)
    face = gradient((255, 226, 92), (255, 128, 24))
    img.paste(face, (0, 0), base)
    shine = ImageChops.multiply(base, letter_mask(dy=22).point(lambda v: 255 - v))
    img.paste((255, 248, 200), (0, 0), shine)
    return img

big = build()
out = sys.argv[1]
big.resize((256, 256), Image.LANCZOS).save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
big.resize((512, 512), Image.LANCZOS).save(out.replace('.ico', '_preview.png'))
