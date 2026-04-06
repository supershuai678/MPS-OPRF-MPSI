#!/bin/bash
# Build all binaries for Table 5 reproduction
#
# BZS-MPSI 和 O-Ring 已内置于项目 thirdparty_bzs/ 和 thirdparty_oring/
# 直接运行即可: bash scripts/build_all.sh
#
# Output binaries:
#   thirdparty_bzs/out/build/linux/frontend/frontend   (BZS-MPSI)
#   thirdparty_oring/build/mpsi                         (O-Ring t>=2)
#   thirdparty_oring/build/oringt1                      (O-Ring t=1)
#   build/perf_network                                   (Our GF128)
#   build_hh/perf_network                                (Our HH)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/config.sh"

echo "========================================"
echo "Building all Table 5 binaries"
echo "========================================"
echo "Project dir: $PROJECT_DIR"
echo "BZS dir:     $BZS_DIR"
echo "O-Ring dir:  $ORING_DIR"
echo ""

# --- Step 1: BZS-MPSI ---
echo "=== [1/4] Building BZS-MPSI ==="
if [ ! -d "$BZS_DIR" ]; then
    echo "[ERROR] BZS-MPSI-main not found at $BZS_DIR"
    exit 1
fi

cd "$BZS_DIR"
if [ ! -f "$BIN_BZS" ]; then
    cmake --preset linux
    cmake --build out/build/linux --parallel "$(nproc)"
    cmake --install out/build/linux
    echo "[OK] BZS-MPSI built"
else
    echo "[SKIP] BZS-MPSI binary already exists"
fi

# --- Step 2: O-Ring ---
echo ""
echo "=== [2/4] Building O-Ring ==="
if [ ! -d "$ORING_DIR" ]; then
    echo "[ERROR] O-Ring source not found at $ORING_DIR"
    exit 1
fi

# Apply patched CMakeLists.txt (remove hardcoded paths, use BZS_DIR)
PATCH_FILE="$PROJECT_DIR/patches/oring_CMakeLists.txt"
if [ -f "$PATCH_FILE" ]; then
    echo "[INFO] Applying patched CMakeLists.txt to O-Ring..."
    cp "$PATCH_FILE" "$ORING_DIR/CMakeLists.txt"
fi

mkdir -p "$ORING_DIR/build"
cd "$ORING_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j "$(nproc)"
echo "[OK] O-Ring built (mpsi + oringt1)"

# --- Step 3: Main project GF128 mode ---
echo ""
echo "=== [3/4] Building main project (GF128 mode) ==="
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Release -DDEPS_DIR="$BZS_DIR"
make perf_network -j "$(nproc)"
echo "[OK] GF128 perf_network built"

# --- Step 4: Main project HH mode ---
echo ""
echo "=== [4/4] Building main project (HH mode) ==="

if ! pkg-config --exists libsodium 2>/dev/null; then
    echo "[WARN] libsodium not found via pkg-config, HH build may fail"
    echo "  Install with: sudo apt-get install libsodium-dev"
fi

mkdir -p "$PROJECT_DIR/build_hh"
cd "$PROJECT_DIR/build_hh"
cmake .. -DCMAKE_BUILD_TYPE=Release -DDEPS_DIR="$BZS_DIR" -DENABLE_HOMOMORPHIC_HASH=ON
make perf_network -j "$(nproc)"
echo "[OK] HH perf_network built"

# --- Summary ---
echo ""
echo "========================================"
echo "Build summary"
echo "========================================"

check_bin() {
    if [ -f "$1" ]; then
        echo "  [OK] $1"
    else
        echo "  [MISSING] $1"
    fi
}

check_bin "$BIN_BZS"
check_bin "$BIN_ORING"
check_bin "$BIN_ORING_T1"
check_bin "$BIN_GF128"
check_bin "$BIN_HH"

echo ""
echo "Done. Run 'bash scripts/run_table5.sh' to start benchmarks."
