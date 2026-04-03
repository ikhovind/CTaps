#!/bin/bash
python3 ../run_benchmarks.py --binary-dir ../../out/Debug/benchmark/  --dual-handshake --ip1-rtts 300 --ip2-rtts 100 --jitter 7 --bandwidth 50 --runs 200 --output ../results/dual_handshake_graph.json

python3 ../visualize.py ../results/dual_handshake_graph.json --dual-handshake --pgf --output ../plots
