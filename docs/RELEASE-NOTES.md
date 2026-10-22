## PS5MG 0.2.0

### New
- STL and OBJ downloads in the viewer, made for printing: the mesh stands on the plate and is scaled to the size you give (longest side, in millimetres).
- Quality presets: Fast, Balanced and Detailed. A dice button picks a random seed. The detailed preset also sets a bigger texture and a higher face budget.
- A new option for the texture size (512, 1024 or 2048) under More options.
- Drop several pictures at once: each one becomes a job, named after its file.
- "Use a picture from PS5SD": pick one of the pictures PS5SD made on the same console, without copying files.
- An API button in the page header: ready-made curl calls for your console, with a copy button.
- Fixed: with More options open, the form on the left could not be scrolled to its end on a desktop; it now scrolls by itself.

### Unchanged
- Model weights are not included. The title has no network client and downloads nothing.
- The title restarts itself after every job.
- Pixal3D with texture at 1024 can stop without an error message.
