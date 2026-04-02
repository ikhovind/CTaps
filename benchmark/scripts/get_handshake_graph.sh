#!/bin/bash
python3 ../run_benchmarks.py --binary-dir ../../out/Debug/benchmark/  --ip1-rtts 50 100 150 200 --bandwidth 50 --runs 200 --output ../results/handshake_graph.json
# How to know output file??
python3 ../visualize.py ../results/handshake_graph.json --dual-handshake --output --handshake ../plots
