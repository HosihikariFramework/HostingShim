#!/usr/bin/env bash
# Build the Endstone carrier. The SDK and nlohmann/json are shallow-cloned by CMake;
# nethost is fetched from nuget.org. Set https_proxy before invoking on networks
# that need one -- like the .bat script, this leaves the proxy to the caller.
set -euo pipefail

cd "$(dirname "$0")"

# The Endstone SDK forces -stdlib=libc++ on UNIX for BDS ABI compatibility.
# GCC's c++ driver rejects that flag, so Linux builds with Clang and libc++.
compiler=()
if [ "$(uname -s)" = "Linux" ]; then
    compiler=(-DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}")
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "${compiler[@]}"
cmake --build build --config Release --parallel
