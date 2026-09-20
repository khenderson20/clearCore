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
    -DSPDLOG_USE_STD_FORMAT=ON \
    -DFUZZING_ENGINE="$LIB_FUZZING_ENGINE"

cmake --build build --target fuzz_hex_loader fuzz_elf_loader -j"$(nproc)"

cp build/fuzz_hex_loader build/fuzz_elf_loader "$OUT/"

# Seed corpus for the ELF target. Fuzzing a binary format from an empty corpus
# burns most of the budget rediscovering the magic bytes and header layout;
# ClusterFuzzLite unpacks <target>_seed_corpus.zip automatically. The cd is
# absolute because this script's working directory is not the source tree.
(cd "$SRC/clearCore/tests/fuzz/corpus/elf" && zip -qr "$OUT/fuzz_elf_loader_seed_corpus.zip" .)
