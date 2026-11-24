#!/bin/sh
# Build the SourceKrillion native app (macOS / Linux). Needs CMake 3.24+, a C++20 compiler and git.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
if [ -d build/SourceKrillion.app ]; then echo "Built build/SourceKrillion.app"; else echo "Built build/SourceKrillion"; fi
