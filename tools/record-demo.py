"""Records the demo clip of the web interface on a real console: drop a picture, make the mesh (the wait as a time-lapse), turn it.
usage: python tools/record-demo.py <console-ip> [picture]      needs: Playwright (tools/.uitest), ffmpeg on PATH, Pillow
Writes docs/media/demo.gif and docs/media/demo.mp4. Frames go to out/demo/."""
import json
import os
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageDraw, ImageFont
from playwright.sync_api import sync_playwright

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.10"
picture = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "samples", "g_gargoyle.png")
U = f"http://{host}:7870/"
frames_dir = os.path.join(root, "out", "demo")
shutil.rmtree(frames_dir, ignore_errors=True)
os.makedirs(frames_dir)
os.makedirs(os.path.join(root, "docs", "media"), exist_ok=True)
frames = []  # (file, seconds)


def font(size):
    for f in (r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


BAR = 64  # the caption has its own strip under the page, so it never covers a button


def snap(pg, hold, caption=None):
    path = os.path.join(frames_dir, f"f{len(frames):04d}.png")
    pg.screenshot(path=path)
    shot = Image.open(path).convert("RGB")
    im = Image.new("RGB", (shot.width, shot.height + BAR), (12, 16, 24))
    im.paste(shot, (0, 0))
    if caption:
        d = ImageDraw.Draw(im)
        f = font(30)
        w = d.textlength(caption, font=f)
        d.text(((im.width - w) / 2, shot.height + 11), caption, font=f, fill=(240, 246, 250))
    im.save(path)
    frames.append((path, hold))


with sync_playwright() as p:
    b = p.chromium.launch(args=["--use-gl=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"])
    pg = b.new_context(viewport={"width": 1120, "height": 700}, color_scheme="dark").new_page()
    pg.goto(U)
    pg.wait_for_timeout(3000)
    snap(pg, 1.6, "Open the page on any device in your network")
    pg.set_input_files("#file", picture)
    pg.wait_for_timeout(900)
    snap(pg, 1.8, "Drop a picture")
    pg.click("#go")
    pg.wait_for_timeout(1500)
    snap(pg, 1.0, "Make mesh")
    t0 = time.time()
    while time.time() - t0 < 600:  # the wait, a frame every few seconds, shown fast
        pg.wait_for_timeout(4200)
        snap(pg, 0.16, "Time-lapse: the real job, on the console")
        if pg.inner_text("#state") == "Ready":
            break
    pg.wait_for_timeout(2500)
    snap(pg, 1.4, "Done: a textured mesh")
    pg.click(".job .btn >> text=View >> nth=0")
    for _ in range(40):
        pg.wait_for_timeout(500)
        if "faces" in pg.inner_text("#vinfo"):
            break
    pg.wait_for_timeout(1200)
    snap(pg, 1.0, "Turn it around")
    box = pg.locator("#view").bounding_box()
    cx, cy = box["x"] + box["width"] / 2, box["y"] + box["height"] / 2
    pg.mouse.move(cx - 140, cy)
    pg.mouse.down()
    for i in range(1, 37):
        pg.mouse.move(cx - 140 + i * 8, cy)
        pg.wait_for_timeout(90)
        snap(pg, 0.09, "Turn it around")
    pg.mouse.up()
    pg.wait_for_timeout(700)
    snap(pg, 2.0, "Download GLB, STL or OBJ")
    b.close()

# ffmpeg: a concat list with a duration per frame, then a good palette
lst = os.path.join(frames_dir, "list.txt")
with open(lst, "w") as f:
    for path, hold in frames:
        f.write(f"file '{os.path.basename(path)}'\nduration {hold}\n")
    f.write(f"file '{os.path.basename(frames[-1][0])}'\n")
gif = os.path.join(root, "docs", "media", "demo.gif")
mp4 = os.path.join(root, "docs", "media", "demo.mp4")
vf = "fps=12,scale=800:-1:flags=lanczos"
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-vf", vf + ",split[a][b];[a]palettegen=max_colors=160:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle", gif], check=True, cwd=frames_dir)
subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-vf", "fps=30,scale=1120:-2:flags=lanczos,format=yuv420p", "-c:v", "libx264", "-crf", "24", "-movflags", "+faststart", mp4], check=True, cwd=frames_dir)
total = sum(h for _, h in frames)
print("frames", len(frames), "| length %.1f s" % total, "| gif %.1f MB" % (os.path.getsize(gif) / 1048576), "| mp4 %.1f MB" % (os.path.getsize(mp4) / 1048576))
