#!/usr/bin/env bash
# The q8 TRELLIS.2 set to /data/PS5MG/models/q8 (512 path first, 1024 flows last).
set -u
H=${PS5_HOST:?set PS5_HOST to the console address}
cd "$(dirname "$0")/.." || exit 1
put() { s=$(date +%s); curl -sS --ftp-create-dirs -T "$1" "ftp://$H:2121$2" && echo "ok $2 $(( $(date +%s) - s )) s" || echo "FAIL $2"; }
for f in birefnet dinov3 ss_flow ss_dec shape_flow_512 shape_dec tex_flow_512 tex_dec shape_flow_1024 tex_flow_1024; do
  put "models/q8/$f.gguf" "/data/PS5MG/models/q8/$f.gguf"
done
echo PUT-Q8-DONE
