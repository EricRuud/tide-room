#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Reproducible standalone build; the older instrument/FX targets remain available.
juce_revision=f72bad64d29715216226685810c5196bd0d79d77
juce_directory="${JUCE_PATH:-$PWD/.cache/JUCE}"
if [ ! -f "$juce_directory/CMakeLists.txt" ]; then
    mkdir -p "$(dirname "$juce_directory")"
    git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git "$juce_directory"
fi
if [ "$(git -C "$juce_directory" rev-parse HEAD)" != "$juce_revision" ]; then
    echo "Expected JUCE 8.0.9 at $juce_revision; inspect $juce_directory." >&2
    exit 1
fi
cmake_tool="${CMAKE_COMMAND:-$(command -v cmake || true)}"
if [ -z "$cmake_tool" ]; then
    cmake_tool="$PWD/.cache/build-tools/cmake/data/bin/cmake"
fi
if [ ! -x "$cmake_tool" ]; then
    echo "Install CMake 3.22 or newer, or set CMAKE_COMMAND to its executable." >&2
    exit 1
fi
if ! xcrun -sdk macosx -f metal >/dev/null 2>&1; then
    echo "Install Xcode and its Metal toolchain, then select it with xcode-select." >&2
    exit 1
fi

room_build="${TIDE_BUILD_DIRECTORY:-$PWD/build-room}"
"$cmake_tool" -S . -B "$room_build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DJUCE_PATH="$juce_directory" \
    -DTIDE_NATIVE_WOOD=ON -DTIDE_CLEAR_ROOM=ON \
    -DTIDE_ROOM_OUTPUT_DIRECTORY="$room_build/app"
"$cmake_tool" --build "$room_build" --target TideRoom_Standalone --parallel "${BUILD_JOBS:-3}"
codesign --force --deep --sign - "$room_build/app/Tide Room.app"
codesign --verify --deep --strict "$room_build/app/Tide Room.app"
echo "Built: $room_build/app/Tide Room.app"
