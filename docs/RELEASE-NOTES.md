## PS5MG 0.2.4

### New
- **Look at a mesh on the TV.** When the title is idle, press Cross on the controller: the newest mesh appears, textured, and the left
  stick turns it. L2/R2 zoom, L1/R1 pick another mesh, Circle goes back. The screen shows the button hints.
- No job starts while a mesh is on the TV. It closes itself after two minutes without input.

### Before you start: the model weights
PS5MG has no model weights of its own and never downloads any. One command on your PC gets them from their authors and copies them to
the console: see the README (Windows: double-click tools\setup-weights.bat; macOS and Linux: ./tools/setup-weights.sh with your console's address).
About 6.5 GB. TRELLIS.2 and Pixal3D are MIT; DINOv3 has Meta's own licence.

### Unchanged
- The title has no network client and downloads nothing.
- The title restarts itself after every job.
- Pixal3D with texture at 1024 can stop without an error message.
- Not tested for this release: how the view feels with a long list of meshes, and meshes from before 0.2.3 over 64 MB (they are refused).
