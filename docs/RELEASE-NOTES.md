## PS5MG 0.1.0, first release

Turns a picture into a textured 3D mesh (GLB) on the console's own GPU.

### What it does
- Web interface on port 7870: upload a picture, pick settings, watch the queue, view the mesh in the browser, download the GLB.
- The TV shows the state, the web address with a QR code, the running job and its progress.
- TRELLIS.2 with q4 or q8 weights; Pixal3D as an experimental second model.
- A textured mesh with 150k faces takes about 1 to 3 minutes per object.

### Before you start
- A console that can run homebrew titles (ShadowMountPlus) and an FTP server to copy files.
- The model weights are not included. Download them with the scripts in the repository (about 6.5 GB for q4) and copy them to /data/PS5MG/models/q4.
- The title is GPL-3.0. DINOv3, which is part of the weights, has its own Meta licence: read the README before you sell what you make.

### Known problems
- No password on the web interface: use it on a network you trust.
- The title restarts itself after every job.
- Pixal3D with texture at 1024 can stop without an error message.
