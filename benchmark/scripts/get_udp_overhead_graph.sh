#!/bin/bash
#!/usr/bin/env bash
set -euo pipefail

SERVER_CORE=0
CLIENT_CORE=4

SERVER_BIN=../../out/Release/benchmark/udp_server
BASELINE_CLIENT_BIN=../../out/Release/benchmark/baseline_udp_rtt_client

CTAPS_CLIENT_BIN=../../out/Release/benchmark/ctaps_udp_rtt_client

# Optional: where the clients write their output
OUT_FOLDER=../results

run_pair() {
    local server_bin="$1"
    local client_bin="$2"
    local label="$3"

    if [[ ! -x "$server_bin" ]]; then
        echo "Error: $server_bin not found or not executable" >&2
        exit 1
    fi
    if [[ ! -x "$client_bin" ]]; then
        echo "Error: $client_bin not found or not executable" >&2
        exit 1
    fi

    echo "[$label] Starting server on core $SERVER_CORE: $server_bin"
    taskset -c "$SERVER_CORE" "$server_bin" &
    local server_pid=$!
    echo "[$label] Server PID: $server_pid"

    # Give server a moment to bind and start
    sleep 0.5

    echo "[$label] Running client on core $CLIENT_CORE: $client_bin $OUT_FOLDER"
    taskset -c "$CLIENT_CORE" "$client_bin" "$OUT_FOLDER"

    echo "[$label] Client finished, stopping server..."
    kill "$server_pid" || true
    wait "$server_pid" 2>/dev/null || true

    echo "[$label] Done."
    echo
}

echo "=== Running BASELINE (UDP) experiment ==="
run_pair "$SERVER_BIN" "$BASELINE_CLIENT_BIN" "BASELINE"

echo "=== Running CTaps experiment ==="
run_pair "$SERVER_BIN" "$CTAPS_CLIENT_BIN" "CTAPS"

echo "All experiments completed."
echo "Expected output folder:"
echo "  $OUT_FOLDER"

python3 ../visualize.py ../results/rtt_baseline_ns.txt --udp-overhead ../results/rtt_ctaps_ns.txt --pgf --output ../plots
