#!/usr/bin/env bash
# WSL: compile the original and the fast kernel and compare them.
set -e
cd "$(dirname "$0")"
TR=/mnt/k/github/PS5MG/src/trellis.cpp
g++ -O3 -mavx2 -mfma -std=c++17 -I. -I$TR/include -Ddeform_conv2d_cpu=deform_conv2d_cpu_ref -Ddeform_conv2d_run=deform_conv2d_run_ref -c $TR/src/deform_conv_cpu.cpp -o ref.o
g++ -O3 -mavx2 -mfma -std=c++17 -I. -I$TR/include -c ../../ps5/deform_conv_fast.cpp -o fast.o
g++ -O3 -std=c++17 -I$TR/include -c test.cpp -o test.o
g++ -o t test.o ref.o fast.o -pthread
./t
