#!/usr/bin/env python3
"""
Benchmark runner for CTaps project.
Runs all server/client combinations and aggregates results into a single JSON file.
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
from typing import Dict, Optional, Tuple


class BenchmarkRunner:
    """Runs benchmark tests for different protocol implementations."""

    def __init__(self, benchmark_dir: Path, bin_dir: Path,
                 interface: str = "lo", rtt_ms: int = 50, bandwidth_mbit: int = 100):
        self.benchmark_dir = benchmark_dir
        self.project_dir = benchmark_dir.parent
        self.bin_dir = bin_dir
        self.results_dir = benchmark_dir / "results"
        self.results_dir.mkdir(exist_ok=True)

        # Network setup script path
        self.setup_script = benchmark_dir / "scripts" / "setup_network.sh"

        # Network emulation parameters
        self.network_config = {
            "interface": interface,
            "rtt_ms": rtt_ms,
            "bandwidth_mbit": bandwidth_mbit
        }

        # Define test configurations
        self.tests = [
            {
                "name": "tcp_native",
                "server": "tcp_server",
                "client": "tcp_client",
                "port": 8080,
                "description": "Native TCP implementation"
            },
            {
                "name": "quic_native",
                "server": "quic_server",
                "client": "quic_client",
                "port": 8080,
                "description": "Pure picoquic implementation"
            },
            {
                "name": "taps_tcp",
                "server": "tcp_server",
                "client": "taps_tcp_client",
                "port": 8080,
                "description": "TAPS API with TCP backend"
            },
            {
                "name": "taps_quic",
                "server": "quic_server",
                "client": "taps_quic_client",
                "port": 8080,
                "description": "TAPS API with QUIC backend"
            }
        ]

    def setup_network_emulation(self) -> bool:
        """Set up network emulation using setup_network.sh script."""
        print("=" * 60)
        print("Setting up network emulation...")
        print("=" * 60)

        if not self.setup_script.exists():
            print(f"✗ Network setup script not found at {self.setup_script}", file=sys.stderr)
            return False

        try:
            # Pass network configuration to the setup script
            result = subprocess.run(
                [
                    str(self.setup_script),
                    "setup",
                    self.network_config["interface"],
                    str(self.network_config["rtt_ms"]),
                    str(self.network_config["bandwidth_mbit"])
                ],
                capture_output=True,
                text=True,
                timeout=30
            )

            if result.returncode != 0:
                print("✗ Network setup failed!", file=sys.stderr)
                print(result.stderr, file=sys.stderr)
                print(result.stdout, file=sys.stderr)
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
        """Tear down network emulation using setup_network.sh script."""
        print("\n" + "=" * 60)
        print("Tearing down network emulation...")
        print("=" * 60)

        if not self.setup_script.exists():
            print(f"Warning: Network setup script not found at {self.setup_script}")
            return

        try:
            result = subprocess.run(
                [
                    str(self.setup_script),
                    "teardown",
                    self.network_config["interface"]
                ],
                capture_output=True,
                text=True,
                timeout=30
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
        """Start a server process in the background."""
        try:
            server_path = self.bin_dir / server_cmd
            # Redirect stdout/stderr to devnull to suppress server output
            process = subprocess.Popen(
                [str(server_path), str(port)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_IGN)
            )
            # Give server time to start
            time.sleep(0.5)
            return process
        except Exception as e:
            print(f"Error starting server {server_cmd}: {e}", file=sys.stderr)
            return None

    def stop_server(self, process: subprocess.Popen) -> None:
        """Stop a server process gracefully."""
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
        """Run a client and parse its JSON output."""
        try:
            client_path = self.bin_dir / client_cmd
            result = subprocess.run(
                [str(client_path), "127.0.0.1", str(port), "--json"],
                capture_output=True,
                text=True,
                timeout=15
            )

            stdout = result.stdout.strip()

            # Check if we got an error
            if stdout == "ERROR" or result.returncode != 0:
                print(f"  ✗ Client returned error", file=sys.stderr)
                return False, None

            # Parse JSON output
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
        """Run a single benchmark test."""
        print(f"Running {test['name']}... ", end="", flush=True)

        # Start server
        server_process = self.start_server(test['server'], test['port'])
        if not server_process:
            print("✗ Failed to start server")
            return None

        try:
            # Run client
            success, result = self.run_client(test['client'], test['port'])

            if success and result:
                print("✓")
                # Add metadata
                result['test_name'] = test['name']
                result['description'] = test['description']
                result['timestamp'] = datetime.utcnow().isoformat() + 'Z'
                return result
            else:
                print("✗")
                return None

        finally:
            # Always stop the server
            self.stop_server(server_process)
            time.sleep(0.2)  # Brief pause between tests

    def run_all_tests(self) -> Dict:
        """Run all benchmark tests and aggregate results."""
        print("=" * 60)
        print("CTaps Benchmark Suite")
        print("=" * 60)
        print()

        results = {
            "benchmark_suite": "CTaps Protocol Performance",
            "run_timestamp": datetime.utcnow().isoformat() + 'Z',
            "bin_dir": self.bin_dir.absolute().as_posix(),
            "network_config": {
                "interface": self.network_config["interface"],
                "rtt_ms": self.network_config["rtt_ms"],
                "bandwidth_mbit": self.network_config["bandwidth_mbit"]
            },
            "tests": []
        }

        for test in self.tests:
            result = self.run_test(test)
            if result:
                results['tests'].append(result)
            else:
                return {}

        return results

    def save_results(self, results: Dict) -> Path:
        """Save results to a timestamped JSON file."""
        timestamp = datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
        filename = f"benchmark-{timestamp}.json"
        filepath = self.results_dir / filename

        with open(filepath, 'w') as f:
            json.dump(results, f, indent=2)

        return filepath


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Run CTaps benchmark suite",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        '--binary-dir',
        type=pathlib.Path,
        help='Directory containing binaries to benchmark',
        required=True
    )

    args = parser.parse_args()

    # Get the benchmark directory (same as script location)
    script_dir = Path(__file__).parent.resolve()

    # Initialize runner
    runner = BenchmarkRunner(script_dir, bin_dir=args.binary_dir)

    # Set up network emulation
    if not runner.setup_network_emulation():
        print("\nNetwork setup failed, aborting benchmarks", file=sys.stderr)
        sys.exit(1)

    network_setup_done = True
    results = {}

    try:
        # Run benchmarks
        results = runner.run_all_tests()

        # Save results
        if len(results) > 0:
            output_file = runner.save_results(results)
            print()
            print("=" * 60)
            print(f"Results saved to: {output_file}")
            print("=" * 60)

            successful = sum(1 for test in results['tests'] if test.get('status') != 'failed')
            total = len(results['tests'])
            print(f"\nSummary: {successful}/{total} tests passed")
        else:
            print("Not saving json, no valid results")

    except KeyboardInterrupt:
        print("\n\nBenchmark interrupted by user", file=sys.stderr)
        raise

    finally:
        # Always tear down network emulation, even on error
        if network_setup_done:
            runner.teardown_network_emulation()

    # Exit with error code if no results
    if len(results) == 0:
        sys.exit(1)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
