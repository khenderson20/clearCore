#!/bin/bash -euo pipefail

cmake -S "$SRC/clearCore" -B build -G Ninja \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DBUILD_NYXSTONE=OFF \
    -DBUILD_QT6_UI=OFF \
    -DBUILD_QT6_QUICK_UI=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DFUZZING_ENGINE="$LIB_FUZZING_ENGINE"

cmake --build build --target fuzz_hex_loader -j"$(nproc)"

cp build/fuzz_hex_loader "$OUT/"
