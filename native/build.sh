#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [ ! -f .cache/JUCE/CMakeLists.txt ]; then
    mkdir -p .cache
    git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git .cache/JUCE
fi
revision=$(git -C .cache/JUCE rev-parse HEAD)
if [ "$revision" != f72bad64d29715216226685810c5196bd0d79d77 ]; then
    echo "Expected JUCE 8.0.9 at the pinned revision; inspect .cache/JUCE before building." >&2
    exit 1
fi
cmake_tool=$(command -v cmake || true)
if [ -z "$cmake_tool" ]; then
    cmake_tool="$PWD/.cache/build-tools/cmake/data/bin/cmake"
fi
if [ ! -x "$cmake_tool" ]; then
    echo "CMake 3.22 or later is required." >&2
    exit 1
fi
if [ -x .cache/build-tools/bin/ninja ]; then
    "$cmake_tool" -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM="$PWD/.cache/build-tools/bin/ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
else
    "$cmake_tool" -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
fi
"$cmake_tool" --build build --parallel 6
echo "Built app, AU and VST3 in build/Tide_artefacts/Release/"
