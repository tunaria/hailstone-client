#!/bin/bash
# Configures (once) and builds a Linux Release inside hailstone-linux-build, with ccache (compose.yaml).
#   linux_build.sh <build dir> [cmake configure args ...]
set -euo pipefail
build=$1
shift
if [ ! -f "$build/CMakeCache.txt" ]; then
    cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache "$@"
elif [ $# -gt 0 ]; then
    # An existing build folder still takes the options given now (a -D added later, such as
    # HAILSTONE_ALLOW_IN_EQ_FOLDER, was ignored before); generator and system name stay as configured.
    cmake -B "$build" "$@"
fi
cmake --build "$build" ${JOBS:+-j "$JOBS"}
ccache -s | sed -n '1,6p'
