#!/usr/bin/env bash
# Pixal3D flows and the q4 1024 flows to /data/PS5MG/models/q4 (same folder: the families share one model directory).
set -u
H=${PS5_HOST:?set PS5_HOST to the console address}
cd "$(dirname "$0")/.." || exit 1
put() { s=$(date +%s); curl -sS --ftp-create-dirs -T "$1" "ftp://$H:2121$2" && echo "ok $2 $(( $(date +%s) - s )) s" || echo "FAIL $2"; }
for f in models/pixal3d/*.gguf; do put "$f" "/data/PS5MG/models/q4/$(basename "$f")"; done
for f in shape_flow_1024 tex_flow_1024; do put "models/q4/$f.gguf" "/data/PS5MG/models/q4/$f.gguf"; done
echo PUT-DONE
