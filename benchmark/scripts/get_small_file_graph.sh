#!/bin/bash
python3 ../run_benchmarks.py --binary-dir ../../out/Release/benchmark/  --small-file --ip1-rtts 50 100 150 200 --bandwidth 50 --runs 1 --output ../results/small_file_graph.json

python3 ../visualize.py ../results/small_file_graph.json --small-file --pgf --output ../plots
