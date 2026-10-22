"""Makes sce_sys/icon0.png, icon0.dds, pic0.dds, pic1.dds for the title (own artwork, no PS5SD placeholders).
usage: python make-icons.py <transparent-render.png>      (tools/render-glb.py <glb> out.png alpha single)
The DDS files are BC7 (mode 6 only, a small encoder here): same header layout as PS5SD's, 4:1 smaller than RGBA."""
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(os.path.dirname(here), "sce_sys")
obj = Image.open(sys.argv[1]).convert("RGBA")
bbox = obj.getbbox(); obj = obj.crop(bbox)

def font(size, bold=True):
    for f in (r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try: return ImageFont.truetype(f, size)
        except OSError: pass
    return ImageFont.load_default()

def gradient(w, h, top, bottom, glow=(0.7, 0.45), glow_rgb=(40, 120, 150)):
    y = np.linspace(0, 1, h)[:, None, None]
    img = np.array(top)[None, None, :] * (1 - y) + np.array(bottom)[None, None, :] * y
    img = np.broadcast_to(img, (h, w, 3)).copy()
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot((xx / w - glow[0]) * w / max(w, h), (yy / h - glow[1]) * h / max(w, h))
    g = np.clip(1 - d / 0.55, 0, 1) ** 2
    img += g[:, :, None] * (np.array(glow_rgb)[None, None, :] * 0.8)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")

def place(base, im, height, cx, cy, shadow=True):
    w = int(im.width * height / im.height)
    im = im.resize((w, height), Image.LANCZOS)
    if shadow:
        sh = Image.new("RGBA", base.size, (0, 0, 0, 0))
        a = im.getchannel("A").point(lambda v: int(v * 0.55))
        sh.paste((0, 0, 0, 255), (cx - w // 2 + height // 40, cy - height // 2 + height // 30), a)
        base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(height // 30)))
    base.alpha_composite(im, (cx - w // 2, cy - height // 2))

def wire(base, step, alpha):  # a faint mesh grid, the way a mesh looks
    d = ImageDraw.Draw(base, "RGBA")
    w, h = base.size
    for x in range(-h, w, step):
        d.line([(x, h), (x + h, 0)], fill=(120, 200, 230, alpha), width=max(1, step // 60))
    for x in range(0, w + h, step):
        d.line([(x, h), (x - h, 0)], fill=(120, 200, 230, alpha), width=max(1, step // 60))

TOP, BOT = (14, 24, 40), (26, 52, 74)
# icon: the lantern and the name
icon = gradient(512, 512, TOP, BOT, glow=(0.5, 0.42), glow_rgb=(50, 140, 165))
place(icon, obj, 380, 256, 226)
d = ImageDraw.Draw(icon)
t = "PS5MG"; f = font(64)
tw = d.textlength(t, font=f)
d.text(((512 - tw) / 2, 410), t, font=f, fill=(238, 244, 248, 255))
icon.convert("RGB").save(os.path.join(out, "icon0.png"))

# pic0: the title's background (text left, object right); pic1: the same without text, darker
def wall(text):
    w, h = 3840, 2160
    im = gradient(w, h, TOP, BOT, glow=(0.72, 0.5), glow_rgb=(45, 130, 160))
    wire(im, 240, 5)
    place(im, obj, 1500, 2780, 1080)
    if text:
        d = ImageDraw.Draw(im)
        d.text((330, 800), "PS5MG", font=font(300), fill=(238, 244, 248, 255))
        d.text((340, 1150), "3D Mesh Generation on PS5", font=font(110, False), fill=(150, 205, 225, 255))
        d.text((340, 1330), "picture to textured mesh, on the console's own GPU", font=font(70, False), fill=(120, 160, 180, 255))
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
