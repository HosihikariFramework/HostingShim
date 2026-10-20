#!/usr/bin/env bash
# Build the self-hosted Linux carrier. nethost is fetched from nuget.org by CMake.
set -euo pipefail

cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
