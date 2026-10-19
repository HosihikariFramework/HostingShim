#!/usr/bin/env bash
# Build the Endstone carrier. The SDK and nlohmann/json are shallow-cloned by CMake;
# nethost is fetched from nuget.org. Set https_proxy before invoking on networks
# that need one -- like the .bat script, this leaves the proxy to the caller.
set -euo pipefail

cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
