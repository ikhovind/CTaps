#!/bin/bash
python3 ../run_benchmarks.py --binary-dir ../../out/Debug/benchmark/  --ip1-rtts 300 --ip2-rtts 100 --bandwidth 50 --runs 200 --output ../results/dual_handshake_graph.json
# How to know output file??
python3 ../visualize.py ../results/dual_handshake_graph.json --output ../plots
