## PS5MG 0.2.2

### Before you start: the model weights
PS5MG has no model weights of its own and never downloads any. One command on your PC gets them from their authors and copies them to the console.
- Get the PS5MG tools: on GitHub (nikay99/PS5MG) press Code, then Download ZIP, and unzip it.
- Windows: double-click tools\setup-weights.bat and type your console's address. macOS and Linux: run ./tools/setup-weights.sh with your console's address.
- It shows the licences and asks you to type yes. Then it downloads the files (about 6.5 GB, from Hugging Face), checks each one and copies it to /data/PS5MG/models/q4 on the console. If it stops, run it again: it continues where it was.
- Licences in short: TRELLIS.2 and Pixal3D are MIT. DINOv3, which is part of the set, has Meta's own licence.

### New
- A one-command tool for the weights on Windows, macOS and Linux: download, check, copy to the console over FTP.
- When model files are missing, the page now leads you through it: three short steps, the command ready to copy with your console's address, and a progress line (for example 3 of 7 files). The long file list is folded away.
- The README starts with a short quick start for each system, and every model file has a direct download link on a fixed version.

### Unchanged
- The title has no network client and downloads nothing.
- The title restarts itself after every job, and after many jobs in a row the console gets slower (see the README, Known problems).
- Pixal3D with texture at 1024 can stop without an error message.
