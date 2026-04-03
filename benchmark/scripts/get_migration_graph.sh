#!/bin/bash
python3 ../run_benchmarks.py --binary-dir ../../out/Release/benchmark/  --ip1-rtts 50 --ip2-rtts 50 --migration --migration-time 5000 --bandwidth 5 --runs 1 --output ../results/migration_graph.json
# How to know output file??
python3 ../visualize.py ../results/migration_graph.json --migration --path-change-ms 5000 --bucket-duration-ms 100 --pgf --output ../plots
