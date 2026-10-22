"""Quick look at a GLB: three views drawn with a painter's algorithm (no GPU, no OpenGL).
usage: python preview.py model.glb out.png [flip_v]
Flat shading from the face normal; the colour of a face is the base colour texture at its UV centroid."""
import os
import sys

import numpy as np
import trimesh
from PIL import Image, ImageDraw

glb, out = sys.argv[1], sys.argv[2]
flip_v = len(sys.argv) > 3 and sys.argv[3] == "flip_v"
scene = trimesh.load(glb, force="scene")
mesh = trimesh.util.concatenate(list(scene.geometry.values()))
tex = getattr(getattr(mesh.visual, "material", None), "baseColorTexture", None)
tex = np.asarray(tex.convert("RGB")) if tex is not None else None
# PREVIEW_ROTX=-90 turns the mesh about x first (Pixal3D meshes are not y-up)
rot = np.radians(float(os.environ.get("PREVIEW_ROTX", "0")))
R = np.array([[1, 0, 0], [0, np.cos(rot), -np.sin(rot)], [0, np.sin(rot), np.cos(rot)]])
mesh.vertices = mesh.vertices @ R.T
# glTF is y-up; this renderer is z-up
V, F = mesh.vertices @ np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=float), mesh.faces
if tex is not None and getattr(mesh.visual, "uv", None) is not None:
    uv = mesh.visual.uv[F].mean(axis=1)
    h, w = tex.shape[:2]
    x = np.clip((uv[:, 0] * w).astype(int), 0, w - 1)
    y = (uv[:, 1] if flip_v else 1 - uv[:, 1]) * h
    colors = tex[np.clip(y.astype(int), 0, h - 1), x].astype(float)
elif getattr(mesh.visual, "kind", None) == "vertex":
    colors = np.asarray(mesh.visual.vertex_colors)[:, :3][F].mean(axis=1)
else:
    colors = np.full((len(F), 3), 200.0)

N = mesh.face_normals
center = (mesh.bounds[0] + mesh.bounds[1]) / 2
scale = float(np.max(mesh.bounds[1] - mesh.bounds[0]))
S = 640


def view(az_deg, el_deg):
    az, el = np.radians(az_deg), np.radians(el_deg)
    rz = np.array([[np.cos(az), -np.sin(az), 0], [np.sin(az), np.cos(az), 0], [0, 0, 1]])
    rx = np.array([[1, 0, 0], [0, np.cos(el), -np.sin(el)], [0, np.sin(el), np.cos(el)]])
    r = rx @ rz
    p = (V - center) @ r.T
    n = N @ r.T
    # looking along +y of the rotated frame; the screen is x (right) and z (up)
    sx = S / 2 + p[:, 0] / scale * S * 0.85
    sy = S / 2 - p[:, 2] / scale * S * 0.85
    depth = p[F, 1].mean(axis=1)
    facing = np.ones(len(n), bool) if os.environ.get("PREVIEW_TWOSIDED") else n[:, 1] < 0.0  # normals toward the viewer
    light = np.clip(0.35 + 0.65 * np.abs(n @ np.array([0.3, -0.8, 0.5])), 0, 1)
    img = Image.new("RGB", (S, S), (235, 235, 235))
    d = ImageDraw.Draw(img)
    for i in np.argsort(-depth):
        if not facing[i]:
            continue
        c = tuple(int(v) for v in np.clip(colors[i] * light[i], 0, 255))
        d.polygon([(sx[j], sy[j]) for j in F[i]], fill=c)
    return img


views = [view(35, 25), view(215, 25), view(35, 80)]
sheet = Image.new("RGB", (S * 3, S))
for i, v in enumerate(views):
    sheet.paste(v, (i * S, 0))
sheet.save(out)
print("saved", out, "faces", len(F), "texture", None if tex is None else tex.shape[:2])
