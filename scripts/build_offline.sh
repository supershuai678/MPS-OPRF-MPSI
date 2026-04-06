#!/bin/bash
# ============================================================
# 离线构建脚本 (无需联网)
#
# 依赖库已预编译在 thirdparty_bzs/out/install/linux/
# 此脚本跳过 fetch 阶段，直接编译 BZS-MPSI 和主项目
#
# 用法:
#   bash scripts/build_offline.sh
# ============================================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
fail()  { echo -e "${RED}[FAIL]${NC}  $*"; }
step()  { echo -e "\n${GREEN}========== $* ==========${NC}"; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BZS_DIR="$PROJECT_DIR/thirdparty_bzs"
ORING_DIR="$PROJECT_DIR/thirdparty_oring"
NPROC="$(nproc 2>/dev/null || echo 2)"

# ============================================================
# 阶段 0: 检查预编译依赖
# ============================================================
step "阶段 0: 检查预编译依赖"

INSTALL_DIR="$BZS_DIR/out/install/linux"

if [ ! -d "$INSTALL_DIR/lib" ] || [ ! -d "$INSTALL_DIR/include" ]; then
    fail "预编译依赖不存在: $INSTALL_DIR"
    fail "请确保 thirdparty_bzs/out/install/linux/ 目录完整"
    exit 1
fi

info "预编译依赖路径: $INSTALL_DIR"
info "库文件数量: $(ls "$INSTALL_DIR/lib/"*.a 2>/dev/null | wc -l) 个 .a 文件"
info "头文件目录: $(ls -d "$INSTALL_DIR/include/"*/ 2>/dev/null | wc -l) 个"
ok "预编译依赖检查通过"

# ============================================================
# 阶段 1: 编译 BZS-MPSI (关闭 FETCH_AUTO)
# ============================================================
step "阶段 1: 编译 BZS-MPSI (离线模式)"

cd "$BZS_DIR"
BZS_BUILD_DIR="$BZS_DIR/out/build/linux"
BZS_FRONTEND="$BZS_BUILD_DIR/frontend/frontend"

if [ -f "$BZS_FRONTEND" ]; then
    ok "BZS-MPSI 已编译, 跳过"
else
    mkdir -p "$BZS_BUILD_DIR"
    cd "$BZS_BUILD_DIR"

    info "cmake 配置 (FETCH_AUTO=OFF) ..."
    cmake "$BZS_DIR" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DFETCH_AUTO=OFF \
        -DFETCH_SPARSEHASH=OFF \
        -DFETCH_LIBOTE=OFF \
        -DVOLE_PSI_ENABLE_BOOST=ON \
        -DVOLE_PSI_ENABLE_ASAN=OFF \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        2>&1

    info "编译中 (使用 $NPROC 核心)..."
    cmake --build . --parallel "$NPROC" 2>&1

    info "安装..."
    cmake --install . 2>&1

    if [ -f "$BZS_FRONTEND" ]; then
        ok "BZS-MPSI 编译成功"
    else
        fail "BZS-MPSI 编译失败"
        fail "检查上方错误信息"
        exit 1
    fi
fi

# ============================================================
# 阶段 2: 编译 O-Ring
# ============================================================
step "阶段 2: 编译 O-Ring"

cd "$PROJECT_DIR"
ORING_BIN="$ORING_DIR/build/mpsi"

if [ -f "$ORING_BIN" ]; then
    ok "O-Ring 已编译, 跳过"
else
    PATCH_FILE="$PROJECT_DIR/patches/oring_CMakeLists.txt"
    if [ -f "$PATCH_FILE" ]; then
        info "应用补丁 CMakeLists.txt..."
        cp "$PATCH_FILE" "$ORING_DIR/CMakeLists.txt"
    fi

    mkdir -p "$ORING_DIR/build"
    cd "$ORING_DIR/build"

    info "cmake 配置..."
    cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1

    info "编译中..."
    make -j "$NPROC" 2>&1

    if [ -f "$ORING_BIN" ]; then
        ok "O-Ring 编译成功"
    else
        fail "O-Ring 编译失败"
        exit 1
    fi
fi

# ============================================================
# 阶段 3: 编译主项目 (GF128 模式)
# ============================================================
step "阶段 3: 编译主项目 (GF128 模式)"

cd "$PROJECT_DIR"
mkdir -p build
cd build

info "cmake 配置 (GF128)..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEPS_DIR="$BZS_DIR" \
    -DENABLE_HOMOMORPHIC_HASH=OFF \
    2>&1

info "编译中..."
make -j "$NPROC" 2>&1

if [ -f "$PROJECT_DIR/build/perf_network" ]; then
    ok "GF128 模式编译成功"
else
    fail "GF128 模式编译失败"
    exit 1
fi

# ============================================================
# 阶段 4: 编译主项目 (HH 模式)
# ============================================================
step "阶段 4: 编译主项目 (HH 模式)"

cd "$PROJECT_DIR"
mkdir -p build_hh
cd build_hh

info "cmake 配置 (HH)..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEPS_DIR="$BZS_DIR" \
    -DENABLE_HOMOMORPHIC_HASH=ON \
    2>&1

info "编译中..."
make -j "$NPROC" 2>&1

if [ -f "$PROJECT_DIR/build_hh/perf_network" ]; then
    ok "HH 模式编译成功"
else
    warn "HH 模式编译失败 (需要 libsodium-dev)"
fi

# ============================================================
# 阶段 5: 编译第三方基线 (mPSI-paxos, MultipartyPSI)
# ============================================================
step "阶段 5: 编译第三方基线 (mPSI-paxos, MultipartyPSI)"

info "调用 build_thirdparty.sh ..."
bash "$SCRIPT_DIR/build_thirdparty.sh" all 2>&1 || warn "第三方基线编译出错，详见上方日志"

# ============================================================
# 汇总
# ============================================================
step "构建完成"

check_bin() {
    if [ -f "$1" ]; then
        ok "  $1"
    else
        warn " $1 (不存在)"
    fi
}

echo ""
info "二进制文件:"
check_bin "$BZS_FRONTEND"
check_bin "$ORING_DIR/build/mpsi"
check_bin "$ORING_DIR/build/oringt1"
check_bin "$PROJECT_DIR/build/perf_network"
check_bin "$PROJECT_DIR/build_hh/perf_network"

source "$SCRIPT_DIR/config.sh"
check_bin "$BIN_MPSI_PAXOS"
check_bin "$BIN_MULTIPARTYPSI"

echo ""
ok "离线构建完成！"
