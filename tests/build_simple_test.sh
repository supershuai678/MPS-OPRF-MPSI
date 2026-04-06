#!/bin/bash
# 独立编译SimplePRF测试

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BZS_PATH="$SCRIPT_DIR/../thirdparty_bzs"
INSTALL_PATH="$BZS_PATH/out/install/linux"

g++ -std=c++20 -O3 -maes -mpclmul -msse4.1 -mavx -mavx2 \
    -I$INSTALL_PATH/include \
    -I$BZS_PATH \
    -L$INSTALL_PATH/lib \
    -L$BZS_PATH/out/libsodium/src/libsodium/.libs \
    SimplePRF_Tests.cpp \
    -o simple_prf_test \
    -lcryptoTools -lsodium -lpthread

echo "Build complete: ./simple_prf_test"
