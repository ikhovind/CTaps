#!/bin/bash
git ls-files '*.c' '*.h' | xargs clang-format -i
