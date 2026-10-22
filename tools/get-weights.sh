#!/usr/bin/env bash
# Downloads the q4 GGUF set of TRELLIS.2 (ilintar/trellis2-gguf, about 6.5 GB) into models/q4: a pinned revision,
# every file checked against tools/weights.sha256, resumable. The weights are not part of this repository; they stay
# under their own licences (README: Licences).
set -u
cd "$(dirname "$0")/.." && . tools/weights-lib.sh && { accept_licenses "$@" || exit 1; } && mkdir -p models/q4
for f in birefnet dinov3 ss_flow ss_dec shape_flow_512 shape_dec tex_flow_512 tex_dec shape_flow_1024 tex_flow_1024; do
  fetch ilintar/trellis2-gguf q4/$f.gguf models/q4/$f.gguf || exit 1
done
echo ALL-DONE
