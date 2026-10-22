"""Makes sce_sys/icon0.png, icon0.dds, pic0.dds, pic1.dds for the title (own artwork, no PS5SD placeholders).
usage: python make-icons.py docs/images/icon-art.jpg   (the artwork: drawn on the console by PS5SD with Z-Image Turbo)
The DDS files are BC7 (mode 6 only, a small encoder here): same header layout as PS5SD's, 4:1 smaller than RGBA."""
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(os.path.dirname(here), "sce_sys")

def font(size, bold=True):
    for f in (r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try: return ImageFont.truetype(f, size)
        except OSError: pass
    return ImageFont.load_default()

# ---- the artwork: a square picture (made on the console with PS5SD + Z-Image Turbo, samples/icons/dragon.png)
art = Image.open(sys.argv[1]).convert("RGB")
if art.width != art.height: art = art.crop((0, 0, min(art.size), min(art.size)))
edge = np.asarray(art.resize((64, 64)), float)
bgc = tuple(int(v) for v in np.concatenate([edge[0], edge[-1], edge[:, 0], edge[:, -1]]).mean(0))  # the picture's own background

icon = art.resize((512, 512), Image.LANCZOS).convert("RGBA")
icon.convert("RGB").save(os.path.join(out, "icon0.png"))

def wall(text):
    w, h = 3840, 2160
    base = np.zeros((h, w, 3), float) + np.array(bgc)
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot((xx - w * 0.70) / w, (yy - h * 0.5) / w)
    base += (np.clip(1 - d / 0.42, 0, 1) ** 2)[:, :, None] * np.array([10, 40, 50])  # a soft glow behind the art
    im = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8)).convert("RGBA")
    size = 2000
    pic = art.resize((size, size), Image.LANCZOS).convert("RGBA")
    m = np.hypot(*(np.mgrid[0:size, 0:size] - size / 2)) / (size / 2)
    pic.putalpha(Image.fromarray((np.clip((1 - m) / 0.35, 0, 1) * 255).astype(np.uint8)))  # fade the square's edges into the wall
    im.alpha_composite(pic, (int(w * 0.70 - size / 2), h // 2 - size // 2))
    if text:
        d2 = ImageDraw.Draw(im)
        d2.text((330, 800), "PS5MG", font=font(300), fill=(238, 244, 248, 255))
        d2.text((340, 1150), "3D Mesh Generation on PS5", font=font(110, False), fill=(150, 205, 225, 255))
        d2.text((340, 1330), "picture to textured mesh, on the console's own GPU", font=font(70, False), fill=(120, 160, 180, 255))
    else:
        im = Image.blend(im, Image.new("RGBA", im.size, (8, 14, 24, 255)), 0.35)
    return im

# ---- BC7, mode 6 (one subset, 7-bit RGBA endpoints with a p-bit each, 4-bit indices)
W16 = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], np.float32)

def bc7(img):
    a = np.asarray(img.convert("RGBA"), np.float32)
    h, w = a.shape[:2]
    blocks = a.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)
    n = len(blocks)
    mean = blocks.mean(1, keepdims=True)
    c = blocks - mean
    cov = np.einsum("bpi,bpj->bij", c, c)
    axis = np.ones((n, 4), np.float32)
    for _ in range(6):
        axis = np.einsum("bij,bj->bi", cov, axis)
        axis /= np.linalg.norm(axis, axis=1, keepdims=True) + 1e-9
    t = np.einsum("bpi,bi->bp", c, axis)
    e0 = mean[:, 0] + axis * t.min(1)[:, None]
    e1 = mean[:, 0] + axis * t.max(1)[:, None]

    def quant(e):
        best = None
        for p in (0, 1):
            q = np.clip(np.round((e - p) / 2), 0, 127)
            err = ((q * 2 + p - e) ** 2).sum(1)
            if best is None: best = (q, np.full(n, p), err)
            else:
                m = err < best[2]
                best = (np.where(m[:, None], q, best[0]), np.where(m, p, best[1]), np.minimum(err, best[2]))
        return best[0].astype(np.uint64), best[1].astype(np.uint64)
    q0, p0 = quant(e0); q1, p1 = quant(e1)
    d0 = q0.astype(np.float32) * 2 + p0[:, None]; d1 = q1.astype(np.float32) * 2 + p1[:, None]
    v = d1 - d0
    tt = np.einsum("bpi,bi->bp", blocks - d0[:, None, :], v) / ((v * v).sum(1)[:, None] + 1e-9) * 64
    idx = np.abs(tt[:, :, None] - W16[None, None, :]).argmin(2).astype(np.uint64)
    swap = idx[:, 0] >= 8  # the first index has 3 bits: its top bit must be 0
    idx = np.where(swap[:, None], 15 - idx, idx)
    q0s = np.where(swap[:, None], q1, q0); q1s = np.where(swap[:, None], q0, q1)
    p0s = np.where(swap, p1, p0); p1s = np.where(swap, p0, p1)
    lo = np.zeros(n, np.uint64); hi = np.zeros(n, np.uint64)
    def put(val, start, bits):
        nonlocal lo, hi
        val = val.astype(np.uint64)
        if start + bits <= 64: lo |= val << np.uint64(start)
        elif start >= 64: hi |= val << np.uint64(start - 64)
        else:
            k = 64 - start
            lo |= (val & np.uint64((1 << k) - 1)) << np.uint64(start)
            hi |= val >> np.uint64(k)
    put(np.full(n, 64, np.uint64), 0, 7)
    pos = 7
    for ch in range(4):
        put(q0s[:, ch], pos, 7); put(q1s[:, ch], pos + 7, 7); pos += 14
    # pos is 63 now: p-bits at 63 and 64, indices from 65
    put(p0s, 63, 1); put(p1s, 64, 1)
    put(idx[:, 0], 65, 3)
    for i in range(1, 16): put(idx[:, i], 68 + 4 * (i - 1), 4)
    return np.stack([lo, hi], 1).astype("<u8").tobytes(), (w, h)

def dds(img, path, template):
    data, (w, h) = bc7(img)
    head = bytearray(open(os.path.join(out, template), "rb").read(148))  # PS5SD's header: BC7, no mips, same size
    assert struct.unpack("<II", head[12:20]) == (h, w), "template size differs"
    open(path, "wb").write(bytes(head) + data)

if __name__ == "__main__":
    for name, im, tmpl in (("icon0.dds", icon, "icon0.dds"), ("pic0.dds", wall(True), "pic0.dds"), ("pic1.dds", wall(False), "pic1.dds")):
        dds(im, os.path.join(out, name), tmpl)
        if name == "pic0.dds": im.convert("RGB").resize((960, 540)).save(os.path.join(os.path.dirname(here), "out", "pic0-preview.png"))
        print("wrote", name)
