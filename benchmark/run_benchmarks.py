#!/usr/bin/env python3
"""
Benchmark runner for CTaps project.
Runs all server/client combinations and aggregates results into a single JSON file.
Supports multiple runs per configuration and sweeping over RTT values.
"""

import subprocess
import pathlib
import json
import time
import signal
import sys
import argparse
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple


class BenchmarkRunner:
    """Runs benchmark tests for different protocol implementations."""

    def __init__(self, benchmark_dir: Path, bin_dir: Path,
                 interface: str = "lo", rtt_ms: int = 50, bandwidth_mbit: int = 100):
        self.benchmark_dir = benchmark_dir
        self.project_dir = benchmark_dir.parent
        self.bin_dir = bin_dir
        self.results_dir = benchmark_dir / "results"
        self.results_dir.mkdir(exist_ok=True)

        self.setup_script = benchmark_dir / "scripts" / "setup_network.sh"

        self.network_config = {
            "interface": interface,
            "rtt_ms": rtt_ms,
            "bandwidth_mbit": bandwidth_mbit
        }

        self.tests = [
            {
                "name": "tcp_native",
                "server": "tcp_benchmark_server",
                "client": "tcp_benchmark_client",
                "port": 8080,
                "description": "Native TCP implementation"
            },
            {
                "name": "quic_native",
                "server": "quic_benchmark_server",
                "client": "quic_benchmark_client",
                "port": 8080,
                "description": "Pure picoquic implementation"
            },
            {
                "name": "taps_tcp",
                "server": "tcp_benchmark_server",
                "client": "taps_benchmark_tcp_client",
                "port": 8080,
                "description": "TAPS API with TCP backend"
            },
            {
                "name": "taps_quic",
                "server": "quic_benchmark_server",
                "client": "taps_benchmark_quic_client",
                "port": 8080,
                "description": "TAPS API with QUIC backend"
            },
            {
                "name": "taps_racing_tcp",
                "server": "tcp_benchmark_server",
                "client": "taps_benchmark_racing_client",
                "port": 8080,
                "description": "TAPS Racing client with TCP server (TCP wins race)"
            },
            {
                "name": "taps_racing_quic",
                "server": "quic_benchmark_server",
                "client": "taps_benchmark_racing_client",
                "port": 8080,
                "description": "TAPS Racing client with QUIC server (QUIC wins race)"
            },
        ]

    def setup_network_emulation(self) -> bool:
        if not self.setup_script.exists():
            print(f"✗ Network setup script not found at {self.setup_script}", file=sys.stderr)
            return False
        try:
            result = subprocess.run(
                [
                    str(self.setup_script),
                    "setup",
                    self.network_config["interface"],
                    str(self.network_config["rtt_ms"]),
                    str(self.network_config["bandwidth_mbit"])
                ],
                capture_output=True, text=True, timeout=30
            )
            if result.returncode != 0:
                print("✗ Network setup failed!", file=sys.stderr)
                print(result.stderr, file=sys.stderr)
                return False
            print(result.stdout)
            print("✓ Network emulation setup successful\n")
            return True
        except subprocess.TimeoutExpired:
            print("✗ Network setup timed out", file=sys.stderr)
            return False
        except Exception as e:
            print(f"✗ Network setup error: {e}", file=sys.stderr)
            return False

    def teardown_network_emulation(self) -> None:
        if not self.setup_script.exists():
            return
        try:
            result = subprocess.run(
                [str(self.setup_script), "teardown", self.network_config["interface"]],
                capture_output=True, text=True, timeout=30
            )
            print(result.stdout)
            if result.returncode != 0:
                print("Warning: Network teardown had issues", file=sys.stderr)
                print(result.stderr, file=sys.stderr)
            else:
                print("✓ Network emulation teardown successful")
        except Exception as e:
            print(f"Warning: Network teardown error: {e}", file=sys.stderr)

    def start_server(self, server_cmd: str, port: int) -> Optional[subprocess.Popen]:
        try:
            server_path = self.bin_dir / server_cmd
            process = subprocess.Popen(
                [str(server_path), str(port)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_IGN)
            )
            time.sleep(0.5)
            return process
        except Exception as e:
            print(f"Error starting server {server_cmd}: {e}", file=sys.stderr)
            return None

    def stop_server(self, process: subprocess.Popen) -> None:
        if process:
            try:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            except Exception as e:
                print(f"Error stopping server: {e}", file=sys.stderr)

    def run_client(self, client_cmd: str, port: int) -> Tuple[bool, Optional[Dict]]:
        try:
            client_path = self.bin_dir / client_cmd
            result = subprocess.run(
                [str(client_path), "127.0.0.1", str(port), "--json"],
                capture_output=True, text=True, timeout=15
            )
            stdout = result.stdout.strip()
            if stdout == "ERROR" or result.returncode != 0:
                print(f"  ✗ Client returned error", file=sys.stderr)
                return False, None
            try:
                data = json.loads(stdout)
                return True, data
            except json.JSONDecodeError as e:
                print(f"  ✗ Failed to parse JSON: {e}", file=sys.stderr)
                print(f"    Output: {stdout[:200]}", file=sys.stderr)
                return False, None
        except subprocess.TimeoutExpired:
            print(f"  ✗ Client timed out", file=sys.stderr)
            return False, None
        except Exception as e:
            print(f"  ✗ Error running client: {e}", file=sys.stderr)
            return False, None

    def run_test(self, test: Dict) -> Optional[Dict]:
        server_process = self.start_server(test['server'], test['port'])
        if not server_process:
            return None
        try:
            success, result = self.run_client(test['client'], test['port'])
            if success and result:
                result['test_name'] = test['name']
                result['description'] = test['description']
                result['timestamp'] = datetime.utcnow().isoformat() + 'Z'
                return result
            return None
        finally:
            self.stop_server(server_process)
            time.sleep(0.2)

    def run_all_tests(self, runs: int = 1) -> List[Dict]:
        """Run all benchmark tests `runs` times, returning a flat list of results."""
        all_results = []
        for run_idx in range(runs):
            print(f"\n  Run {run_idx + 1}/{runs}")
            for test in self.tests:
                print(f"    {test['name']}... ", end="", flush=True)
                result = self.run_test(test)
                if result:
                    result['run'] = run_idx + 1
                    all_results.append(result)
                    print("✓")
                else:
                    print("✗")
                    return []  # Abort on failure, consistent with original behaviour
        return all_results

    def save_results(self, results: Dict) -> Path:
        timestamp = datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
        filename = f"benchmark-{timestamp}.json"
        filepath = self.results_dir / filename
        with open(filepath, 'w') as f:
            json.dump(results, f, indent=2)
        return filepath


def run_rtt_sweep(benchmark_dir: Path, bin_dir: Path, rtt_values: List[int],
                  bandwidth_mbit: int, runs: int, interface: str) -> Dict:
    """Run the full benchmark suite for each RTT value."""
    output = {
        "benchmark_suite": "CTaps Protocol Performance",
        "run_timestamp": datetime.utcnow().isoformat() + 'Z',
        "bin_dir": bin_dir.absolute().as_posix(),
        "runs_per_rtt": runs,
        "bandwidth_mbit": bandwidth_mbit,
        "rtt_sweep": []
    }

    for rtt in rtt_values:
        print("\n" + "=" * 60)
        print(f"RTT: {rtt}ms  ({runs} run(s) per test)")
        print("=" * 60)

        runner = BenchmarkRunner(benchmark_dir, bin_dir,
                                 interface=interface,
                                 rtt_ms=rtt,
                                 bandwidth_mbit=bandwidth_mbit)

        if not runner.setup_network_emulation():
            print(f"Network setup failed for RTT={rtt}ms, aborting", file=sys.stderr)
            sys.exit(1)

        try:
            results = runner.run_all_tests(runs=runs)
        finally:
            runner.teardown_network_emulation()

        if not results:
            print(f"No valid results for RTT={rtt}ms, aborting", file=sys.stderr)
            sys.exit(1)

        output["rtt_sweep"].append({
            "rtt_ms": rtt,
            "tests": results
        })

    return output


def main():
    parser = argparse.ArgumentParser(
        description="Run CTaps benchmark suite",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Single RTT, 10 runs
  %(prog)s --binary-dir out/Release/benchmark --runs 10

  # Sweep over multiple RTT values, 10 runs each
  %(prog)s --binary-dir out/Release/benchmark --runs 10 --rtt-values 10 50 100 200
        """
    )
    parser.add_argument('--binary-dir', type=pathlib.Path, required=True,
                        help='Directory containing benchmark binaries')
    parser.add_argument('--runs', type=int, default=1,
                        help='Number of runs per test per RTT value (default: 1)')
    parser.add_argument('--rtt-values', type=int, nargs='+', default=[50],
                        metavar='MS',
                        help='RTT values in ms to sweep over (default: 50)')
    parser.add_argument('--bandwidth', type=int, default=100,
                        metavar='MBIT',
                        help='Bandwidth in Mbit/s (default: 100)')
    parser.add_argument('--interface', type=str, default='lo',
                        help='Network interface for emulation (default: lo)')

    args = parser.parse_args()
    script_dir = Path(__file__).parent.resolve()

    results = run_rtt_sweep(
        benchmark_dir=script_dir,
        bin_dir=args.binary_dir,
        rtt_values=args.rtt_values,
        bandwidth_mbit=args.bandwidth,
        runs=args.runs,
        interface=args.interface,
    )

    runner = BenchmarkRunner(script_dir, args.binary_dir)
    output_file = runner.save_results(results)

    total_tests = sum(len(rtt["tests"]) for rtt in results["rtt_sweep"])
    print("\n" + "=" * 60)
    print(f"Results saved to: {output_file}")
    print(f"Total test results: {total_tests}")
    print("=" * 60)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
