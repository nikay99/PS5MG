#!/usr/bin/env bash
# WSL: the original and the fast QEM simplifier on one mesh.   run.sh <mesh.bin> [target] [fast-only]
set -e
cd "$(dirname "$0")"
TR=/mnt/k/github/PS5MG/src/trellis.cpp
g++ -O2 -std=c++17 -I$TR/include -Ddecimate_qem=decimate_qem_orig -c $TR/src/decimate_qem.cpp -o orig.o
g++ -O2 -std=c++17 -I$TR/include -I../../ps5 -c ../../ps5/decimate_qem_fast.cpp -o fast.o
g++ -O2 -std=c++17 -I$TR/include -c test.cpp -o test.o
g++ -O2 -std=c++17 -I$TR/include -c $TR/src/tri_bvh.cpp -o bvh.o
g++ -o t test.o orig.o fast.o bvh.o -pthread
./t "$@"
