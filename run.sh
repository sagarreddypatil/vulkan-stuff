#!/bin/bash

set -e

cmake -G Ninja -S . -B build \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DSLANGC="${SLANGC:-/opt/shader-slang-bin/bin/slangc}"
cmake --build build
cd build
mangohud ./triangle
