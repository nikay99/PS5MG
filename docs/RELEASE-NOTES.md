## PS5MG 0.2.1

### Before you start: the model weights
PS5MG has no model weights of its own and never downloads any. You get them once from their authors and copy them to the console with FTP.
- What: the TRELLIS.2 files, about 6.5 GB ("q4"), from Hugging Face: ilintar/trellis2-gguf, folder q4.
- Where: /data/PS5MG/models/q4 on the console. The page finds them by itself.
- The list of files, what each one is for, the licences and a script that downloads and checks them are in the README on GitHub (nikay99/PS5MG), section "Where the weights come from".
- Licences in short: TRELLIS.2 and Pixal3D are MIT. DINOv3, which is part of the set, has Meta's own licence.

### New
- When the model files are missing, the page now says which ones, with their sizes, and where to put them. Under More options, "Model files" shows whether the set is complete.
- The README explains where the weights come from, which files do what, and which one is optional (the cut-out model).

### Unchanged
- The title has no network client and downloads nothing.
- The title restarts itself after every job.
- Pixal3D with texture at 1024 can stop without an error message.
