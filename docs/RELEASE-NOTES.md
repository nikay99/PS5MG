## PS5MG 0.2.3

### Fixed
- **No more slowdown after many jobs in a row.** Up to 0.2.2 a job took about 2.6 times as long after 15 jobs. In a test of 80 jobs in a row
  every job finished at the same speed.
- Jobs without texture are simplified to the face budget like textured ones: about 0.3 MB a GLB instead of 30 to 130 MB.
- The log is written by a thread of its own, so a slow write no longer holds up the job, and jobs print far fewer lines.
- A `quit` file left from an earlier run no longer ends a freshly started title at once.

### Before you start: the model weights
PS5MG has no model weights of its own and never downloads any. One command on your PC gets them from their authors and copies them to
the console: see the README (Windows: double-click tools\setup-weights.bat; macOS and Linux: ./tools/setup-weights.sh with your console's address).
About 6.5 GB. TRELLIS.2 and Pixal3D are MIT; DINOv3 has Meta's own licence.

### Unchanged
- The title has no network client and downloads nothing.
- The title restarts itself after every job.
- Pixal3D with texture at 1024 can stop without an error message.
