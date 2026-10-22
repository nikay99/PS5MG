# Roadmap

PS5MG stays small on purpose: a picture goes in, a mesh comes out, on the console, with nothing else to run. No cloud, no
accounts, no weights hosted here. Every item below should be something you can show in a short clip.

## Done
- **0.1.0:** picture to textured mesh on the console, web interface, TV dashboard, job queue, TRELLIS.2 (q4 and q8) and Pixal3D.
- **0.1.1:** optional access key for the page and API, no wildcard CORS, cross-site POSTs refused, a failed job shows its log.

## 0.2.0: easier to start, more fun to use
- **STL and OBJ downloads** next to GLB, made in the browser, set up for printing (standing on the plate, scaled to the size you give).
- **Quality presets** (Fast, Balanced, Detailed) instead of a wall of settings, and a dice for a random seed.
- **Several pictures at once:** drop a handful, each becomes a job.
- **Pick a picture PS5SD made** on the same console, instead of copying files around. (Only a picker: PS5MG and PS5SD stay separate apps.)
- A gallery page with turnable examples, a quick start with requirements, and issue templates.

## 0.2.1: setting up the weights is easier to understand
- The README says where the weights come from, which files do what and which one is optional.
- The page shows which model files are missing and where they go, instead of only an error.

## 0.3.0: rock solid
- The soak test (`tools/jobs-soak.txt`) showed that the restart after every job holds up (37 of 37 jobs finished) but that jobs get about 2.6 times slower
  from job 17 on and stay slow. Find out why: heat, system memory, or the restarts (see `docs/RESULTS.md`).
- A stale `quit` file makes a freshly started title end at once: remove it at start.
- Find why a second job in the same process runs slower, so the restart can go.
- Pixal3D with texture at 1024 for large objects (it stops without an error message on the sword and shield pictures).
- Cancel a running job; clearer error messages in the page.

## Ideas, not promises
- Quad meshes (trellis.cpp has quad refinement), more texture options, several views as input.
- Upstream the speedups (deform conv, QEM decimation, BiRefNet upscale) to trellis.cpp.
- A small preview of the latest mesh on the TV.
