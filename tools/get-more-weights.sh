#!/usr/bin/env bash
# Optional weights: the q8 TRELLIS.2 set (ilintar/trellis2-gguf) and Pixal3D (vegax87/Pixal3D). Pinned revisions,
# every file checked against tools/weights.sha256, resumable.
set -u
cd "$(dirname "$0")/.." && . tools/weights-lib.sh && mkdir -p models/pixal3d models/q8
for f in pixal3d_naf pixal3d_ss_flow pixal3d_shape_flow_512 pixal3d_shape_flow_1024 pixal3d_tex_flow_1024; do
  fetch vegax87/Pixal3D $f.gguf models/pixal3d/$f.gguf || exit 1
done
for f in birefnet dinov3 ss_flow ss_dec shape_flow_512 shape_dec tex_flow_512 tex_dec shape_flow_1024 tex_flow_1024; do
  fetch ilintar/trellis2-gguf q8/$f.gguf models/q8/$f.gguf || exit 1
done
echo MORE-DONE
