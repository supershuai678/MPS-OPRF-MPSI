#!/bin/bash
# 编译正确性验证测试

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BZS_PATH="$SCRIPT_DIR/../thirdparty_bzs"
INSTALL_PATH="$BZS_PATH/out/install/linux"

g++ -std=c++20 -O2 -maes -mpclmul -msse4.1 -mavx -mavx2 \
    -I$INSTALL_PATH/include \
    -I$BZS_PATH \
    -L$INSTALL_PATH/lib \
    Correctness_Tests.cpp \
    -o correctness_test \
    -lcryptoTools -lpthread

echo "Build complete: ./correctness_test"
