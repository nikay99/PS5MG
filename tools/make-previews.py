"""Renders compare/ps5_<name>.png (three views) for every out/ps5_<name>.glb that has none or an older one.
usage: python make-previews.py [name ...]      Pixal3D meshes of the first build (b02) lie on their back: rotated here."""
import glob
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
names = sys.argv[1:]
for glb in sorted(glob.glob(os.path.join(root, "out", "ps5_*.glb"))):
    name = os.path.basename(glb)[4:-4]
    if names and name not in names:
        continue
    png = os.path.join(root, "compare", f"ps5_{name}.png")
    if os.path.exists(png) and os.path.getmtime(png) >= os.path.getmtime(glb) and not names:
        continue
    env = dict(os.environ)
    if "pixal" in name:
        env["PREVIEW_TWOSIDED"] = "1"  # raw Pixal3D meshes have unclean winding: draw both sides
    if name == "b02_lantern_pixal":
        env["PREVIEW_ROTX"] = "90"
    r = subprocess.run([sys.executable, os.path.join(here, "preview.py"), glb, png], env=env, capture_output=True, text=True)
    print(name, (r.stdout.strip().splitlines() or [r.stderr.strip()[-120:]])[-1])
