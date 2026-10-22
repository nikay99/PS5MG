"""Builds docs/images/presets.jpg: one picture made with the three quality presets (same seed), whole mesh on top and a close-up
of the head below. Input: out/render/preset_<fast|balanced|detailed>.png (tools/render-glb.py ... single, RENDER_SIZE=2400) and
the job facts in FACTS below (read them from the console's job list: time, faces, texture)."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FACTS = [  # preset, label, steps/detail line, time, faces, texture
    ("fast", "Fast", "6 steps", "1:52", "144k faces", "1024 px texture"),
    ("balanced", "Balanced", "8 steps", "2:13", "139k faces", "1024 px texture"),
    ("detailed", "Detailed", "12 steps, 300k face budget, 2048 px atlas", "2:56", "296k faces", "2048 px texture"),
]


def font(size, bold=True):
    for f in (r"C:\Windows\Fonts\segoeuib.ttf" if bold else r"C:\Windows\Fonts\segoeui.ttf", r"C:\Windows\Fonts\arialbd.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


bg = (236, 238, 241)


def crop_bg(im, box):
    """A crop that may reach past the picture: the part outside is the background colour, not black."""
    c = Image.new("RGB", (box[2] - box[0], box[3] - box[1]), bg)
    c.paste(im, (-box[0], -box[1]))
    return c


CELL = 700
W, H = CELL * 3, 150 + CELL * 2 + 60
out = Image.new("RGB", (W, H), bg)
d = ImageDraw.Draw(out)
for i, (key, name, detail, secs, faces, tex) in enumerate(FACTS):
    im = Image.open(os.path.join(root, "out", "render", f"preset_{key}.png")).convert("RGB")
    a = np.asarray(im, int)
    mask = (np.abs(a - np.array(bg)).sum(2) > 30)
    ys, xs = np.where(mask)
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    w, h = x1 - x0, y1 - y0
    pad = int(0.06 * max(w, h))
    whole = crop_bg(im, (x0 - pad, y0 - pad, x1 + pad, y1 + pad))
    whole = whole.resize((CELL, int(whole.height * CELL / whole.width)), Image.LANCZOS)
    if whole.height > CELL:
        whole = whole.resize((int(whole.width * CELL / whole.height), CELL), Image.LANCZOS)
    out.paste(whole, (i * CELL + (CELL - whole.width) // 2, 150 + (CELL - whole.height) // 2))
    # close-up: the head, the same fraction of the object in every image
    side = int(0.50 * w)
    hx0, hy0 = x0 - int(0.04 * w), y0 + int(0.17 * h)
    close = crop_bg(im, (hx0, hy0, hx0 + side, hy0 + side)).resize((CELL, CELL), Image.LANCZOS)
    out.paste(close, (i * CELL, 150 + CELL))
    d.text((i * CELL + 24, 18), name, font=font(46), fill=(30, 36, 48))
    d.text((i * CELL + 24, 74), f"{detail}", font=font(24, False), fill=(90, 98, 115))
    d.text((i * CELL + 24, 106), f"{secs} on the console, {faces}, {tex}", font=font(24, False), fill=(90, 98, 115))
d.text((24, H - 46), "Same picture, same seed (1234). More steps change the result as well as sharpen it; the lower row is the same close-up of the head.",
       font=font(22, False), fill=(90, 98, 115))
for i in (1, 2):
    d.line([(i * CELL, 130), (i * CELL, H - 60)], fill=(214, 218, 226), width=2)
out.save(os.path.join(root, "docs", "images", "presets.jpg"), "JPEG", quality=86, optimize=True)
print("presets.jpg", out.size)
