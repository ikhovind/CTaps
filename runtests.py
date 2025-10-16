#!/usr/bin/env python3

import subprocess
import sys
import os

def run_command(command, cwd=None):
    """Runs a command and checks for errors."""
    print(f"Running command: {' '.join(command)}")
    result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode != 0:
        print(f"Error running command: {' '.join(command)}")
        sys.exit(1)

def main():
    """Builds and runs tests."""
    build_dir = "cmake-build-debug-coverage"
    project_root = os.path.dirname(os.path.abspath(__file__))
    build_path = os.path.join(project_root, build_dir)

    # Build the project
    build_command = ["cmake", "--build", build_path, "--target", "all", "-j", "6"]
    run_command(build_command)

    # Run tests
    test_command = ["ctest", "--output-on-failure"]
    if len(sys.argv) > 1:
        test_name = sys.argv[1]
        test_command.extend(["-R", test_name])

    run_command(test_command, cwd=build_path)

if __name__ == "__main__":
    main()
