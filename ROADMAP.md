# Roadmap

## 0.1.0 (this release)
Picture to textured mesh on the console, web interface, PS5 screen, job queue, TRELLIS.2 (q4/q8) and Pixal3D.

## 0.1.1
- Optional key for the web interface and API; no wildcard CORS; cross-site POSTs refused.
- A failed job shows the last lines of its log in the page.

## 0.2.0
- Text input in the web interface: picture from PS5SD, then mesh, with no PC involved.
- Cancel a running job; clearer error messages in the page.
- Pixal3D 1024 with texture for large objects (it stops without an error message on the sword and shield pictures).
- Find out why a long-lived process gets slower after the first job (the title restarts itself after each job today).

## Later: PS5 AI Studio
One platform from PS5SD (pictures, video), PS5MG (meshes) and PS5LM (language), with one web interface and chains
(text to picture to mesh). One model uses the GPU at a time and phases switch by restart, so the open question is
whether one title can start another from the console.

## Ideas
- Quad meshes (trellis.cpp has quad refinement), PBR texture output options, multi-view input.
- Upstream the speedups (deform conv, QEM decimation, BiRefNet upscale) to trellis.cpp.
- A catalog entry for the PS5 homebrew catalog.
