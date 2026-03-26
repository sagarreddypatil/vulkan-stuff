#!/bin/bash

set -e

cmake -G Ninja -S . -B build \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DSLANGC="${SLANGC:-/opt/shader-slang-bin/bin/slangc}"
cmake --build build
cd build

export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_shader_object
./triangle
