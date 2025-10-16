#!/bin/bash

/home/ikhovind/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake --build /home/ikhovind/Documents/Skole/taps/cmake-build-debug-coverage --target all -j 6

cd cmake-build-debug-coverage
cmake ..
make
ctest
