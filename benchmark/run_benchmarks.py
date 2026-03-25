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


# Tests included in the migration benchmark suite.
MIGRATION_TEST_NAMES = {"quic_native", "taps_racing_quic", "tcp_native"}


class BenchmarkRunner:
    """Runs benchmark tests for different protocol implementations."""

    def __init__(self,
                 benchmark_dir: Path,
                 bin_dir: Path,
                 interface: str,
                 ip1_rtt: int,
                 ip2_rtt: int,
                 bandwidth_mbit: int):
        self.benchmark_dir = benchmark_dir
        self.project_dir = benchmark_dir.parent
        self.bin_dir = bin_dir
        self.results_dir = benchmark_dir / "results"
        self.results_dir.mkdir(exist_ok=True)

        self.setup_script = benchmark_dir / "scripts" / "setup_network.sh"

        self.network_config = {
            "interface": interface,
            "ip1_rtt": ip1_rtt,
            "ip2_rtt": ip2_rtt,
            "bandwidth_mbit": bandwidth_mbit
        }

        self.single_ip_tests = [
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

        self.dual_ip_tests = [
            {
                "name": "quic_native_dual_ip",
                "server": "quic_benchmark_server",
                "client": "quic_benchmark_handshake_client",
                "port": 8080,
                "description": "Pure picoquic implementation"
            },
            {
                "name": "taps_racing_quic_dual_ip",
                "server": "quic_benchmark_server",
                "client": "taps_benchmark_handshake_client",
                "port": 8080,
                "description": "TAPS Racing client which closes after handshake"
            },
        ]

    @property
    def migration_tests(self) -> List[Dict]:
        return [t for t in self.single_ip_tests if t["name"] in MIGRATION_TEST_NAMES]

    def setup_migration_addresses(self) -> bool:
        try:
            result = subprocess.run(
                ["sudo", "ip", "addr", "add", "127.0.0.2/8", "dev", "lo"],
                capture_output=True, text=True
            )
            # exit 2 means EEXIST — address already present, that's fine
            if result.returncode not in (0, 2):
                print(f"✗ Failed to add 127.0.0.2: {result.stderr.strip()}", file=sys.stderr)
                return False
            print("✓ Migration loopback address 127.0.0.2 ready")
            return True
        except Exception as e:
            print(f"✗ Error adding migration address: {e}", file=sys.stderr)
            return False

    def teardown_migration_addresses(self) -> None:
        try:
            subprocess.run(
                ["sudo", "ip", "addr", "del", "127.0.0.2/8", "dev", "lo"],
                capture_output=True, text=True
            )
        except Exception as e:
            print(f"Warning: Failed to remove 127.0.0.2: {e}", file=sys.stderr)

    def setup_network_emulation(self) -> bool:
        if not self.setup_script.exists():
            print(f"✗ Network setup script not found at {self.setup_script}", file=sys.stderr)
            return False
        try:

            setup_args = [
                str(self.setup_script),
                "setup",
                self.network_config["interface"],
                str(self.network_config["bandwidth_mbit"]),
                "127.0.0.1:" + str(self.network_config["ip1_rtt"])
            ]
            if self.network_config["ip2_rtt"] is not None:
                setup_args.append("127.0.0.2:" + str(self.network_config["ip2_rtt"]))
            result = subprocess.run(
                setup_args,
                capture_output=True, text=True, timeout=100
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
                if stdout:
                    print(f"    stdout: {stdout[:500]}", file=sys.stderr)
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
            test_list = self.dual_ip_tests if self.network_config["ip2_rtt"] is not None else self.single_ip_tests
            for test in test_list:
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

    def run_all_migration_tests(self, runs: int, path_change_ms: int,
                                block_src: str = "127.0.0.1",
                                block_port: int = 8080) -> List[Dict]:
        """Run migration tests `runs` times, returning a flat list of results.

        Unlike run_all_tests, a timeout (TCP hanging after path loss) is treated
        as an expected result rather than an abort condition.
        """
        all_results = []
        for run_idx in range(runs):
            print(f"\n  Run {run_idx + 1}/{runs}")
            for test in self.migration_tests:
                print(f"    {test['name']}... ", end="", flush=True)
                result = self.run_migration_test(
                    test, path_change_ms, block_src, block_port
                )
                entry = {
                    "test_name": test["name"],
                    "description": test["description"],
                    "run": run_idx + 1,
                    "path_change_ms": path_change_ms,
                    "timestamp": datetime.utcnow().isoformat() + 'Z',
                }
                if result is not None:
                    entry.update(result)
                    entry["timed_out"] = False
                    print("✓")
                else:
                    entry["timed_out"] = True
                    print("✗ (timed out / failed)")
                all_results.append(entry)
        return all_results

    def run_migration_test(self, test: Dict, path_change_ms: int,
                           block_src: str, block_port: int) -> Optional[Dict]:
        server_process = self.start_server(test['server'], test['port'])
        if not server_process:
            return None

        iptables_added = False
        try:
            client_path = self.bin_dir / test['client']
            client_process = subprocess.Popen(
                [str(client_path), "127.0.0.1", str(test['port']), "--json"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )

            # Wait for fixed offset then block the path
            time.sleep(path_change_ms / 1000.0)
            print("Blocking path");
            # sudo iptables -A INPUT -s 127.0.0.1 -d 127.0.0.1 -p udp --dport 8080 -j REJECT
            subprocess.run(["sudo", "iptables", "-A", "INPUT",
                            "-d", "127.0.0.1",
                            "-s", "127.0.0.1", "-p", "udp",
                            "--dport", "8080", "-j", "REJECT"], check=True)

            # sudo iptables -A OUTPUT -s 127.0.0.1 -d 127.0.0.1 -p udp --dport 8080 -j REJECT
            subprocess.run(["sudo", "iptables", "-A", "OUTPUT",
                            "-s", "127.0.0.1",
                            "-d", "127.0.0.1", "-p", "udp",
                            "--dport", "8080", "-j", "REJECT"], check=True)

            subprocess.run(["sudo", "iptables", "-A", "INPUT",
                            "-d", "127.0.0.1",
                            "-s", "127.0.0.1", "-p", "tcp",
                            "--dport", "8080", "-j", "REJECT"], check=True)

            subprocess.run(["sudo", "iptables", "-A", "OUTPUT",
                            "-s", "127.0.0.1",
                            "-d", "127.0.0.1", "-p", "tcp",
                            "--dport", "8080", "-j", "REJECT"], check=True)
            iptables_added = True


            try:
                stdout, _ = client_process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                client_process.kill()
                print(f"  ✗ Migration client timed out", file=sys.stderr)
                return None

            try:
                data = json.loads(stdout.strip())
                data['test_name'] = test['name']
                data['description'] = test['description']
                data['timestamp'] = datetime.utcnow().isoformat() + 'Z'
                data['path_change_ms'] = path_change_ms
                return data
            except json.JSONDecodeError:
                # No valid JSON — only now treat non-zero exit as a hard failure
                if client_process.returncode != 0:
                    print(f"  ✗ Migration client failed (exit {client_process.returncode})", file=sys.stderr)
                    if stdout.strip():
                        print(f"    stdout: {stdout.strip()[:500]}", file=sys.stderr)
                else:
                    print(f"  ✗ Failed to parse JSON", file=sys.stderr)
                return None

        finally:
            if iptables_added:
                subprocess.run(["sudo", "iptables", "-D", "INPUT",
                                "-d", "127.0.0.1",
                                "-s", "127.0.0.1", "-p", "udp",
                                "--dport", "8080", "-j", "REJECT"], check=True)

                # sudo iptables -A OUTPUT -s 127.0.0.1 -d 127.0.0.1 -p udp --dport 8080 -j REJECT
                subprocess.run(["sudo", "iptables", "-D", "OUTPUT",
                                "-s", "127.0.0.1",
                                "-d", "127.0.0.1", "-p", "udp",
                                "--dport", "8080", "-j", "REJECT"], check=True)

                subprocess.run(["sudo", "iptables", "-D", "INPUT",
                                "-d", "127.0.0.1",
                                "-s", "127.0.0.1", "-p", "tcp",
                                "--dport", "8080", "-j", "REJECT"], check=True)

                subprocess.run(["sudo", "iptables", "-D", "OUTPUT",
                                "-s", "127.0.0.1",
                                "-d", "127.0.0.1", "-p", "tcp",
                                "--dport", "8080", "-j", "REJECT"], check=True)

            self.stop_server(server_process)
            time.sleep(0.2)


def run_rtt_sweep(benchmark_dir: Path,
                  bin_dir: Path,
                  ip1_rtts: List[int],
                  ip2_rtts: List[int],
                  bandwidth_mbit: int,
                  runs: int,
                  interface: str) -> Dict:
    """Run the full benchmark suite for each RTT value."""
    output = {
        "benchmark_suite": "CTaps Protocol Performance",
        "run_timestamp": datetime.utcnow().isoformat() + 'Z',
        "bin_dir": bin_dir.absolute().as_posix(),
        "runs_per_rtt": runs,
        "bandwidth_mbit": bandwidth_mbit,
        "rtt_sweep": []
    }

    for ix, rtt in enumerate(ip1_rtts):
        print("\n" + "=" * 60)
        print(f"RTT: {rtt}ms  ({runs} run(s) per test)")
        print("=" * 60)

        runner = BenchmarkRunner(benchmark_dir, bin_dir,
                                 interface=interface,
                                 ip1_rtt=rtt,
                                 ip2_rtt=ip2_rtts[ix] if ip2_rtts else None,
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

def run_bad_ip_good_ip(benchmark_dir: Path, bin_dir: Path, ip_rtts: Dict[str, int], runs: int, interface: str) -> Dict:
    """Run the full benchmark suite for each RTT value."""
    output = {
        "benchmark_suite": "CTaps Protocol Performance",
        "run_timestamp": datetime.utcnow().isoformat() + 'Z',
        "bin_dir": bin_dir.absolute().as_posix(),
        "runs_per_rtt": runs,
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


def run_migration_sweep(benchmark_dir: Path, bin_dir: Path, ip1_rtts: List[int],
                        bandwidth_mbit: int, runs: int, interface: str,
                        path_change_ms: int) -> Dict:
    """Run the migration benchmark suite for each RTT value."""
    output = {
        "benchmark_suite": "CTaps Connection Migration",
        "run_timestamp": datetime.utcnow().isoformat() + 'Z',
        "bin_dir": bin_dir.absolute().as_posix(),
        "runs_per_rtt": runs,
        "bandwidth_mbit": bandwidth_mbit,
        "path_change_ms": path_change_ms,
        "rtt_sweep": []
    }

    for rtt in rtt_values:
        print("\n" + "=" * 60)
        print(f"RTT: {rtt}ms  |  path blocked after {path_change_ms}ms  ({runs} run(s) per test)")
        print("=" * 60)

        runner = BenchmarkRunner(benchmark_dir, bin_dir,
                                 interface=interface,
                                 ip1_rtt=rtt,
                                 ip2_rtt=None,
                                 bandwidth_mbit=bandwidth_mbit)

        if not runner.setup_network_emulation():
            print(f"Network setup failed for RTT={rtt}ms, aborting", file=sys.stderr)
            sys.exit(1)

        if not runner.setup_migration_addresses():
            runner.teardown_network_emulation()
            print(f"Migration address setup failed for RTT={rtt}ms, aborting", file=sys.stderr)
            sys.exit(1)

        try:
            results = runner.run_all_migration_tests(
                runs=runs, path_change_ms=path_change_ms
            )
        finally:
            runner.teardown_migration_addresses()
            runner.teardown_network_emulation()

        output["rtt_sweep"].append({
            "rtt_ms": rtt,
            "tests": results
        })

    return output

def save_results(results: Dict, results_dir: Path, filename = None) -> Path:
    timestamp = datetime.now().strftime("%Y-%m-%dT%H-%M-%S")
    if not filename:
        filename = f"benchmark-{timestamp}.json"
    filepath = results_dir / filename
    with open(filepath, 'w') as f:
        json.dump(results, f, indent=2)
    return filepath



def main():
    parser = argparse.ArgumentParser(
        description="Run CTaps benchmark suite",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Sweep over multiple RTT values, 10 runs each
  %(prog)s --binary-dir out/Release/benchmark --runs 10 --ip1-delays 10 50 100 200

  # Migration test: sweep delays, block path after 500ms
  %(prog)s --binary-dir out/Release/benchmark --runs 5 --ip1-delays 10 50 100 --migration 500
        """
    )
    parser.add_argument('--binary-dir', type=pathlib.Path, required=True,
                        help='Directory containing benchmark binaries')
    parser.add_argument('--runs', type=int, default=1,
                        help='Number of runs per test per RTT value (default: 1)')
    parser.add_argument('--ip1-rtts', type=int, nargs='+', required=True, metavar='MS',
                        help='Fixed delay on 127.0.0.1 for racing sweep (default: 50)')
    parser.add_argument('--ip2-rtts', type=int, nargs='+', default=None,
                        metavar='MS',
                        help='Delay values to sweep on 127.0.0.2')
    parser.add_argument('--bandwidth', type=int, required=True,
                        metavar='MBIT',
                        help='Bandwidth in Mbit/s (default: 100)')
    parser.add_argument('--interface', type=str, default='lo',
                        help='Network interface for emulation (default: lo)')
    parser.add_argument('--migration', type=int, default=None,
                        metavar='MS',
                        help='Run migration tests, blocking the path after MS milliseconds')
    parser.add_argument('--output-file', type=pathlib.Path, default=None,
                        help='Path to save results JSON (default: results/benchmark-<timestamp>.json)')


    args = parser.parse_args()
    script_dir = Path(__file__).parent.resolve()

    if args.ip2_rtts and len(args.ip2_rtts) != len(args.ip1_rtts):
        print("Error: --ip2-rtts must have the same number of values as --ip1-rtts", file=sys.stderr)
        sys.exit(1)

    if args.migration is not None:
        results = run_migration_sweep(
            benchmark_dir=script_dir,
            bin_dir=args.binary_dir,
            ip1_rtts=args.ip1_rtts,
            bandwidth_mbit=args.bandwidth,
            runs=args.runs,
            interface=args.interface,
            path_change_ms=args.migration,
        )
    else:
        results = run_rtt_sweep(
            benchmark_dir=script_dir,
            bin_dir=args.binary_dir,
            ip1_rtts=args.ip1_rtts,
            ip2_rtts=args.ip2_rtts,
            bandwidth_mbit=args.bandwidth,
            runs=args.runs,
            interface=args.interface,
        )

    output_file = save_results(results, script_dir / "results", filename=args.output_file)

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
