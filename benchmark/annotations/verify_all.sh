#!/bin/bash
echo "Verifying QUIC client..."
python3 compare_annotated.py quic_annotated.c ../src/client/quic_benchmark_client.c
echo "Verifying TCP client..."
python3 compare_annotated.py tcp_annotated.c ../src/client/tcp_benchmark_client.c
echo "Verifying TAPS client..."
python3 compare_annotated.py taps_annotated.c ../src/client/taps_benchmark_racing_client.c
echo "Counting TCP annotations..."
python3 count_annotated.py tcp_annotated.c
echo "Counting QUIC annotations..."
python3 count_annotated.py quic_annotated.c
echo "Counting TAPS annotations..."
python3 count_annotated.py taps_annotated.c
