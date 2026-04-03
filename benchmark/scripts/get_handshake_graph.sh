#!/bin/bash
# The ip2-rtts are a hack to ensure that we test the handshakek over 127.0.0.1
python3 ../run_benchmarks.py --binary-dir ../../out/Release/benchmark/ --handshake --ip1-rtts 50 100 150 200 --ip2-rtts 500 500 500 500 --bandwidth 50 --runs 1 --output ../results/handshake_graph.json

python3 ../visualize.py ../results/handshake_graph.json  --handshake --handshake --pgf --output ../plots
