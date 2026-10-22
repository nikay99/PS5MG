"""Build-time patch of trellis.cpp's src/trellis_cli.cpp: a job without texture skips the whole post step, the face
budget with it, and wrote the decoded mesh as it is (about 4.7 million faces, 30 to 130 MB a GLB). Writing that much
after every job is what made the console slow after about 15 jobs in a row. Now such a mesh goes through the same
steps as a textured one before the bake (weld, fill holes, narrow-band remesh, drop floaters, QEM to --faces or the
default, weld, drop floaters), without the bake. Idempotent."""
import sys

path = sys.argv[1]
s = open(path, newline="").read()
if "QEM simplify (no texture)" in s:
    sys.exit(0)
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
old = "    if (!textured)\n        trellis::write_glb(outglb.c_str(), mesh.verts.data()"
new = """    if (!do_tex && mesh.F() > trellis::qem_face_target(cfg)) {
        trellis::weld_vertices(mesh.verts, mesh.faces, nullptr, 1.0f / ((float)so.res * 8.0f));
        trellis::fill_small_holes(mesh.faces);
        trellis::TriBvh bvh = trellis::TriBvh::build(mesh.verts.data(), mesh.V(), mesh.faces.data(), mesh.F());
        trellis::Mesh rm = trellis::remesh_narrow_band_dc(mesh.verts.data(), mesh.V(), mesh.faces.data(), mesh.F(), bvh, so.res,
                                                          cfg.band > 0 ? cfg.band : std::max(1, so.res / 512));
        if (rm.F() > 0) {
            trellis::clean_mesh(rm.V(), rm.faces);
            trellis::drop_small_components(rm.verts, rm.faces, 0.02f);
        }
        const std::vector<float>& sverts = rm.F() > 0 ? rm.verts : mesh.verts;
        const std::vector<int32_t>& sfaces = rm.F() > 0 ? rm.faces : mesh.faces;
        const int qem_target = trellis::qem_face_target(cfg);
        printf("  QEM simplify (no texture): F=%d -> target %d faces\\n", (int)sfaces.size()/3, qem_target);
        fflush(stdout);
        std::vector<float> dv; std::vector<int32_t> df;
        trellis::decimate_qem(sverts, (int)sverts.size()/3, sfaces, (int)sfaces.size()/3, qem_target, dv, df);
        trellis::weld_vertices(dv, df, nullptr, 1.0f / ((float)so.res * 8.0f));
        trellis::fill_small_holes(df);
        trellis::drop_small_components(dv, df, 0.03f);
        mesh.verts.swap(dv); mesh.faces.swap(df);
        colors.clear();  // per vertex of the dense mesh, no longer matching
    }
""" + old
if s.count(old) != 1:
    sys.exit("patch-notex-faces: the GLB write is not where it was expected")
s = s.replace(old, new)
if crlf:
    s = s.replace("\n", "\r\n")
open(path, "w", newline="").write(s)
print("patched the face budget without texture in", path)
