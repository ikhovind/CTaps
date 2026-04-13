# Benchmark scripts

In order to aid reproducibility of the graphs
featured in my thesis, I have written four scripts
to run the associated benchmark and generate a pgf file
with the results. 

These scripts assume you are on Linux
and that you have root access, which is required because of the
network emulation.

This also assumes that you have the required dependencies
for CTaps installed and can compile it successfully. See the
main README.

Each .sh file produces one of the four graphs featured
in the thesis.

## Setup

The script files assume that you have built CTaps in
a directory named ``out/Release/``, that you have
an activated python environment with the required
packages installed (found in ``benchmark/requirements.txt``)
and that you are running the scripts from the ``benchmark/scripts/`` folder,
since the scripts use relative paths.

The generated files will place a pgf file in ``benchmark/plots/``
if everything succeeds.

Debugging support is not optimal, but you can look at
``run_benchmarks.py`` to see how the specific test is
run, and try to run the associated binaries in ``out/Release/benchmark/``
manually if you encounter any errors.

## Running benchmarks
Starting from root of CTaps:

```bash
mkdir -p out/Release
cmake -B out/Release -DCMAKE_BUILD_TYPE=Release
cmake --build out/Release
python3 -m venv venv     # If you don't want to install packages globally
source venv/bin/activate
pip install -r benchmark/requirements.txt
cd benchmark/scripts/
```

After setup is done run one of the scripts:

```bash
# Shows the benefit of connection racing when a fast and slow endpoint are available
./get_dual_handshake_graph.sh
# Shows handshake time by RTT
./get_handshake_graph.sh
# Shows CTaps and picoquic recovering after a path change
./get_migration_graph.sh
# Shows CWND reuse by QUIC multistreaming
./get_small_file_graph.sh
```
