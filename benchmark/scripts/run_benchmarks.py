#!/usr/bin/env python3
"""
Automated benchmark test harness for TCP and QUIC implementations.
Runs benchmarks with network emulation and saves results to JSON files.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional, Tuple


class BenchmarkRunner:
    def __init__(self, config: Dict):
        self.config = config
        self.script_dir = Path(__file__).parent
        self.benchmark_dir = self.script_dir.parent
        self.build_dir = Path(config['build_dir'])
        self.results_dir = Path(config['results_dir'])
        self.network_script = self.script_dir / 'setup_network.sh'

        # Test configuration
        self.timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        self.test_id = (f"rtt{config['rtt_ms']}_bw{config['bandwidth_mbit']}_"
                       f"loss{config['loss_percent']}_delay{config['delay_sec']}")

        # Create results directory
        self.results_dir.mkdir(parents=True, exist_ok=True)

    def print_header(self, text: str):
        """Print a formatted header"""
        print(f"\n{'='*50}")
        print(f"{text}")
        print(f"{'='*50}\n")

    def print_success(self, text: str):
        """Print success message"""
        print(f"✓ {text}")

    def print_error(self, text: str):
        """Print error message"""
        print(f"✗ {text}", file=sys.stderr)

    def print_info(self, text: str):
        """Print info message"""
        print(f"ℹ {text}")

    def verify_executables(self) -> bool:
        """Verify that all required executables exist"""
        executables = ['tcp_server', 'tcp_client', 'quic_server', 'quic_client']

        for exe in executables:
            exe_path = self.build_dir / exe
            if not exe_path.exists():
                self.print_error(f"Executable not found: {exe_path}")
                return False

        return True

    def setup_network(self) -> bool:
        """Setup network emulation using tc/netem"""
        if not self.config['network_emulation']:
            self.print_info("Network emulation disabled")
            return True

        self.print_header("Setting Up Network Emulation")

        cmd = [
            'sudo', str(self.network_script), 'setup', 'lo',
            str(self.config['rtt_ms']),
            str(self.config['bandwidth_mbit']),
            str(self.config['loss_percent'])
        ]

        try:
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                self.print_error(f"Failed to setup network emulation: {result.stderr}")
                return False

            print(result.stdout)
            self.print_success("Network emulation configured")
            time.sleep(2)  # Let network stack settle
            return True

        except Exception as e:
            self.print_error(f"Failed to setup network emulation: {e}")
            return False

    def teardown_network(self):
        """Remove network emulation"""
        if not self.config['network_emulation']:
            return

        self.print_info("Tearing down network emulation...")

        cmd = ['sudo', str(self.network_script), 'teardown']
        try:
            subprocess.run(cmd, capture_output=True, timeout=10)
        except Exception as e:
            self.print_error(f"Failed to teardown network: {e}")

    def cleanup_processes(self):
        """Kill any running server/client processes"""
        processes = ['tcp_server', 'tcp_client', 'quic_server', 'quic_client']

        for proc in processes:
            try:
                subprocess.run(['killall', proc],
                             capture_output=True,
                             stderr=subprocess.DEVNULL,
                             timeout=5)
            except:
                pass

    def extract_json(self, output: str) -> Optional[Dict]:
        """Extract JSON object from command output"""
        # Find the last line that starts with {
        lines = output.strip().split('\n')
        for line in reversed(lines):
            line = line.strip()
            if line.startswith('{'):
                try:
                    return json.loads(line)
                except json.JSONDecodeError:
                    continue

        return None

    def run_benchmark(self, impl_name: str, server_cmd: str, client_cmd: str) -> Optional[Dict]:
        """Run a single benchmark test"""
        self.print_header(f"Running {impl_name} Benchmark")

        verbose = self.config.get('verbose', False)

        # Start server
        self.print_info(f"Starting {impl_name} server...")
        if verbose:
            self.print_info(f"Server command: {server_cmd}")
            server_process = subprocess.Popen(
                server_cmd,
                shell=True,
                cwd=self.build_dir
            )
        else:
            server_process = subprocess.Popen(
                server_cmd,
                shell=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                cwd=self.build_dir
            )

        time.sleep(2)  # Give server time to start

        try:
            # Run client
            self.print_info(f"Running {impl_name} client...")
            if verbose:
                self.print_info(f"Client command: {client_cmd}")
                # In verbose mode, show output but also capture it
                result = subprocess.run(
                    client_cmd,
                    shell=True,
                    capture_output=True,
                    text=True,
                    cwd=self.build_dir,
                    timeout=120
                )
                # Print the output
                print("\n--- Client Output ---")
                print(result.stdout)
                if result.stderr:
                    print("--- Client Errors ---")
                    print(result.stderr, file=sys.stderr)
                print("--- End Output ---\n")
            else:
                result = subprocess.run(
                    client_cmd,
                    shell=True,
                    capture_output=True,
                    text=True,
                    cwd=self.build_dir,
                    timeout=120
                )

            if result.returncode != 0:
                self.print_error(f"Client failed with code {result.returncode}")
                self.print_error(f"Error: {result.stderr}")
                return None

            # Extract JSON from output
            json_data = self.extract_json(result.stdout)

            if not json_data:
                self.print_error("No JSON output found in client output")
                return None

            # Add metadata
            json_data['metadata'] = {
                'timestamp': self.timestamp,
                'test_id': self.test_id,
                'network_config': {
                    'rtt_ms': self.config['rtt_ms'],
                    'bandwidth_mbit': self.config['bandwidth_mbit'],
                    'loss_percent': self.config['loss_percent'],
                    'delay_sec': self.config['delay_sec'],
                    'emulation_enabled': self.config['network_emulation']
                }
            }

            self.print_success(f"{impl_name} benchmark completed")
            return json_data

        except subprocess.TimeoutExpired:
            self.print_error(f"Client timed out")
            return None

        finally:
            # Kill server
            server_process.terminate()
            try:
                server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server_process.kill()

    def print_summary(self, data: Dict):
        """Print summary of benchmark results"""
        impl = data.get('implementation', 'unknown')
        print(f"\n  Implementation: {impl}")

        if 'handshake_time_ms' in data:
            print(f"  Handshake: {data['handshake_time_ms']:.2f} ms")

        if 'large_file' in data:
            lf = data['large_file']
            if 'connection_time_ms' in lf:
                print(f"  Large file connection: {lf['connection_time_ms']:.2f} ms")
            print(f"  Large file transfer: {lf['transfer_time_ms']:.2f} ms")
            print(f"  Large file throughput: {lf['throughput_mbps']:.2f} Mbps")

        if 'short_file' in data:
            sf = data['short_file']
            if 'connection_time_ms' in sf:
                print(f"  Short file connection: {sf['connection_time_ms']:.2f} ms")
            print(f"  Short file transfer: {sf['transfer_time_ms']:.2f} ms")
            print(f"  Short file throughput: {sf['throughput_mbps']:.2f} Mbps")

    def save_results(self, impl_name: str, data: Dict) -> Path:
        """Save results to JSON file"""
        filename = f"{impl_name.lower()}_{self.test_id}_{self.timestamp}.json"
        filepath = self.results_dir / filename

        with open(filepath, 'w') as f:
            json.dump(data, f, indent=2)

        self.print_success(f"Results saved to: {filepath}")
        return filepath

    def create_combined_results(self, tcp_data: Optional[Dict], quic_data: Optional[Dict]) -> Path:
        """Create combined results file"""
        self.print_header("Generating Combined Results")

        combined = {
            'test_id': self.test_id,
            'timestamp': self.timestamp,
            'network_config': {
                'rtt_ms': self.config['rtt_ms'],
                'bandwidth_mbit': self.config['bandwidth_mbit'],
                'loss_percent': self.config['loss_percent'],
                'delay_sec': self.config['delay_sec'],
                'emulation_enabled': self.config['network_emulation']
            },
            'results': {
                'tcp': tcp_data,
                'quic': quic_data
            }
        }

        filename = f"combined_{self.test_id}_{self.timestamp}.json"
        filepath = self.results_dir / filename

        with open(filepath, 'w') as f:
            json.dump(combined, f, indent=2)

        self.print_success(f"Combined results saved to: {filepath}")
        return filepath

    def run(self) -> int:
        """Run all benchmarks"""
        self.print_header("Benchmark Test Configuration")
        print(f"Build directory:    {self.build_dir}")
        print(f"Results directory:  {self.results_dir}")
        print(f"Test ID:            {self.test_id}")
        print(f"Timestamp:          {self.timestamp}")
        print(f"\nNetwork Configuration:")
        print(f"  RTT:              {self.config['rtt_ms']}ms")
        print(f"  Bandwidth:        {self.config['bandwidth_mbit']} Mbit/s")
        print(f"  Packet Loss:      {self.config['loss_percent']}%")
        print(f"  Delay:            {self.config['delay_sec']}s")
        print(f"  Emulation:        {'Enabled' if self.config['network_emulation'] else 'Disabled'}")

        # Verify executables
        if not self.verify_executables():
            self.print_error("Build verification failed")
            return 1

        try:
            # Setup network emulation
            # if not self.setup_network():
            #     return 1

            # Clean up any existing processes
            self.cleanup_processes()

            # Run TCP benchmark
            tcp_data = self.run_benchmark(
                'TCP',
                f'./tcp_server {self.config["tcp_port"]}',
                f'./tcp_client {self.config["host"]} {self.config["tcp_port"]} {self.config["delay_sec"]}'
            )

            if tcp_data:
                self.print_summary(tcp_data)
                self.save_results('tcp', tcp_data)

            time.sleep(1)
            self.cleanup_processes()

            # Run QUIC benchmark
            quic_data = self.run_benchmark(
                'QUIC',
                f'./quic_server {self.config["quic_port"]}',
                f'./quic_client {self.config["host"]} {self.config["quic_port"]} {self.config["delay_sec"]}'
            )

            if quic_data:
                self.print_summary(quic_data)
                self.save_results('quic', quic_data)

            # Create combined results
            self.create_combined_results(tcp_data, quic_data)

            self.print_header("Test Complete")
            self.print_info(f"Results saved in: {self.results_dir}")

            # List result files
            print("\nGenerated files:")
            for f in sorted(self.results_dir.glob(f"*_{self.timestamp}.json")):
                print(f"  {f.name}")

            return 0

        finally:
            self.cleanup_processes()
            # self.teardown_network()


def main():
    parser = argparse.ArgumentParser(
        description='Run TCP and QUIC benchmarks with network emulation',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument('-r', '--rtt', type=int, default=50,
                       help='Round-trip time in milliseconds (default: 50)')
    parser.add_argument('-b', '--bandwidth', type=int, default=100,
                       help='Bandwidth in Mbit/s (default: 100)')
    parser.add_argument('-l', '--loss', type=float, default=0,
                       help='Packet loss percentage (default: 0)')
    parser.add_argument('-d', '--delay', type=float, default=0.5,
                       help='Delay between transfers in seconds (default: 0.5)')
    parser.add_argument('-o', '--output', type=str, default=None,
                       help='Output directory for results (default: ./results)')
    parser.add_argument('-n', '--no-network', action='store_true',
                       help='Skip network emulation setup')
    parser.add_argument('--build-dir', type=str, default=None,
                       help='Path to build directory (default: ../out/Debug/benchmark)')
    parser.add_argument('-v', '--verbose', action='store_true',
                       help='Show server and client output for debugging')

    args = parser.parse_args()

    # Determine directories
    script_dir = Path(__file__).parent
    benchmark_dir = script_dir.parent

    build_dir = args.build_dir
    if not build_dir:
        build_dir = benchmark_dir.parent / 'out' / 'Debug' / 'benchmark'

    results_dir = args.output
    if not results_dir:
        results_dir = benchmark_dir / 'results'

    # Configuration
    config = {
        'rtt_ms': args.rtt,
        'bandwidth_mbit': args.bandwidth,
        'loss_percent': args.loss,
        'delay_sec': args.delay,
        'network_emulation': not args.no_network,
        'build_dir': build_dir,
        'results_dir': results_dir,
        'host': '127.0.0.1',
        'tcp_port': 8080,
        'quic_port': 4433,
        'verbose': args.verbose
    }

    runner = BenchmarkRunner(config)
    return runner.run()


if __name__ == '__main__':
    sys.exit(main())
