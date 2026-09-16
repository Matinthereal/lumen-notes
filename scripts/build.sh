#!/usr/bin/env bash
# Configure, build and test. Usage: scripts/build.sh [Debug|RelWithDebInfo|Release]
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD_TYPE="${1:-RelWithDebInfo}"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build --parallel
ctest --test-dir build --output-on-failure
