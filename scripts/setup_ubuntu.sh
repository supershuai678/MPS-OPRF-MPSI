#!/bin/bash
# ============================================================
# MPS-OPRF-MPSI 一键安装脚本 (Ubuntu 20.04 / 22.04 / 24.04)
#
# 功能:
#   1. 安装系统依赖 (apt)
#   2. 编译 BZS-MPSI 及其全部子依赖
#      (libOTe, cryptoTools, coproto, macoro, boost, libsodium, ...)
#   3. 编译 O-Ring
#   4. 编译主项目 (GF128 模式)
#   5. 编译主项目 (HH 模式)
#   6. 运行冒烟测试
#
# 用法:
#   chmod +x scripts/setup_ubuntu.sh
#   bash scripts/setup_ubuntu.sh          # 完整安装
#   bash scripts/setup_ubuntu.sh --deps   # 仅安装系统依赖
#   bash scripts/setup_ubuntu.sh --build  # 仅编译 (跳过 apt)
#   bash scripts/setup_ubuntu.sh --test   # 仅运行测试
# ============================================================

set -euo pipefail

# ---------------------- 颜色输出 ----------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
fail()  { echo -e "${RED}[FAIL]${NC}  $*"; }
step()  { echo -e "\n${GREEN}========== $* ==========${NC}"; }

# ---------------------- 路径 ----------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BZS_DIR="$PROJECT_DIR/thirdparty_bzs"
ORING_DIR="$PROJECT_DIR/thirdparty_oring"
NPROC="$(nproc 2>/dev/null || echo 2)"

# ---------------------- 参数解析 ----------------------
DO_DEPS=true
DO_BUILD=true
DO_TEST=true

if [ "${1:-}" = "--deps" ]; then
    DO_BUILD=false; DO_TEST=false
elif [ "${1:-}" = "--build" ]; then
    DO_DEPS=false; DO_TEST=false
elif [ "${1:-}" = "--test" ]; then
    DO_DEPS=false; DO_BUILD=false
fi

# ============================================================
# 阶段 0: 环境检查
# ============================================================
step "阶段 0: 环境检查"

if [ ! -f /etc/os-release ]; then
    fail "无法检测操作系统，此脚本仅支持 Ubuntu"
    exit 1
fi

. /etc/os-release
if [ "$ID" != "ubuntu" ] && [ "$ID" != "debian" ]; then
    warn "检测到 $PRETTY_NAME, 此脚本为 Ubuntu 设计, 可能需要调整"
fi
info "操作系统: $PRETTY_NAME"
info "CPU 核心: $NPROC"
info "项目路径: $PROJECT_DIR"

# 检查必要目录
if [ ! -d "$BZS_DIR" ]; then
    fail "thirdparty_bzs/ 不存在, 请确保项目完整"
    exit 1
fi
if [ ! -d "$ORING_DIR" ]; then
    fail "thirdparty_oring/ 不存在, 请确保项目完整"
    exit 1
fi

# ============================================================
# 阶段 1: 安装系统依赖
# ============================================================
if $DO_DEPS; then

step "阶段 1: 安装系统依赖 (apt)"

info "更新软件源..."
sudo apt-get update -qq

# 编译工具链
PACKAGES=(
    build-essential
    g++
    cmake
    ninja-build
    git
    pkg-config
    python3
)

# 密码学 / 网络依赖
PACKAGES+=(
    libsodium-dev
    libssl-dev
)

# Boost (BZS-MPSI 需要 boost_system, boost_thread)
PACKAGES+=(
    libboost-all-dev
)

# 构建依赖
PACKAGES+=(
    autoconf
    automake
    libtool
    wget
    curl
    unzip
)

info "安装 ${#PACKAGES[@]} 个软件包..."
sudo apt-get install -y -qq "${PACKAGES[@]}"

# 检查 cmake 版本
CMAKE_VER=$(cmake --version | head -1 | grep -oP '\d+\.\d+\.\d+')
CMAKE_MAJOR=$(echo "$CMAKE_VER" | cut -d. -f1)
CMAKE_MINOR=$(echo "$CMAKE_VER" | cut -d. -f2)

if [ "$CMAKE_MAJOR" -lt 3 ] || { [ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 15 ]; }; then
    warn "cmake $CMAKE_VER < 3.15, 尝试安装更新版本..."
    sudo apt-get install -y -qq software-properties-common
    # Kitware APT repo for newer cmake
    wget -qO - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | \
        gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
    CODENAME=$(lsb_release -cs)
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $CODENAME main" | \
        sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
    sudo apt-get update -qq
    sudo apt-get install -y -qq cmake
    CMAKE_VER=$(cmake --version | head -1 | grep -oP '\d+\.\d+\.\d+')
fi

# 检查 g++ C++20 支持
GCC_VER=$(g++ -dumpversion | cut -d. -f1)
if [ "$GCC_VER" -lt 10 ]; then
    warn "g++ $GCC_VER 不支持 C++20, 安装 g++-12..."
    sudo apt-get install -y -qq g++-12
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100
    GCC_VER=$(g++ -dumpversion | cut -d. -f1)
fi

ok "系统依赖安装完成"
info "  cmake:    $CMAKE_VER"
info "  g++:      $(g++ --version | head -1)"
info "  ninja:    $(ninja --version 2>/dev/null || echo 'N/A')"
info "  libsodium: $(pkg-config --modversion libsodium 2>/dev/null || echo 'installed')"

fi # DO_DEPS

# ============================================================
# 阶段 2: 编译 BZS-MPSI (包含全部子依赖)
# ============================================================
if $DO_BUILD; then

step "阶段 2: 编译 BZS-MPSI"
info "这一步会自动 fetch 并编译: libOTe, cryptoTools, coproto, macoro,"
info "bitpolymul, sparsehash, libdivide, boost, libsodium 等"
info "首次编译约需 10-30 分钟 (取决于 CPU 和网络)"

cd "$BZS_DIR"

BZS_FRONTEND="$BZS_DIR/out/build/linux/frontend/frontend"

if [ -f "$BZS_FRONTEND" ]; then
    ok "BZS-MPSI 已编译, 跳过 (删除 out/build/linux/ 可强制重编)"
else
    info "执行 cmake --preset linux ..."
    cmake --preset linux 2>&1 | tail -5

    info "编译中 (使用 $NPROC 核心)..."
    cmake --build out/build/linux --parallel "$NPROC" 2>&1 | tail -10

    info "安装..."
    cmake --install out/build/linux 2>&1 | tail -5

    if [ -f "$BZS_FRONTEND" ]; then
        ok "BZS-MPSI 编译成功"
    else
        fail "BZS-MPSI 编译失败, frontend 二进制不存在"
        exit 1
    fi
fi

# ============================================================
# 阶段 3: 编译 O-Ring
# ============================================================
step "阶段 3: 编译 O-Ring"

cd "$PROJECT_DIR"

ORING_BIN="$ORING_DIR/build/mpsi"

if [ -f "$ORING_BIN" ]; then
    ok "O-Ring 已编译, 跳过"
else
    # 应用补丁 CMakeLists.txt (使用项目内相对路径)
    PATCH_FILE="$PROJECT_DIR/patches/oring_CMakeLists.txt"
    if [ -f "$PATCH_FILE" ]; then
        info "应用补丁 CMakeLists.txt (相对路径版本)..."
        cp "$PATCH_FILE" "$ORING_DIR/CMakeLists.txt"
    fi

    mkdir -p "$ORING_DIR/build"
    cd "$ORING_DIR/build"

    info "cmake 配置..."
    cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5

    info "编译中..."
    make -j "$NPROC" 2>&1 | tail -5

    if [ -f "$ORING_BIN" ]; then
        ok "O-Ring 编译成功"
    else
        fail "O-Ring 编译失败"
        exit 1
    fi
fi

# ============================================================
# 阶段 4: 编译主项目 (GF128 模式)
# ============================================================
step "阶段 4: 编译主项目 (GF128 模式)"

cd "$PROJECT_DIR"
mkdir -p build
cd build

info "cmake 配置 (GF128)..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEPS_DIR="$BZS_DIR" \
    -DENABLE_HOMOMORPHIC_HASH=OFF \
    2>&1 | tail -5

info "编译中..."
make -j "$NPROC" 2>&1 | tail -10

# 检查关键二进制
EXPECTED_BINS=(
    perf_network
    mps_oprf_frontend
    mps_oprf_tests
    mpsoprf_tests
    bicentric_tests
    ring_tests
    perf_tests_protocols
)

MISSING=0
for bin in "${EXPECTED_BINS[@]}"; do
    if [ ! -f "$PROJECT_DIR/build/$bin" ]; then
        fail "缺失: build/$bin"
        MISSING=$((MISSING + 1))
    fi
done

if [ "$MISSING" -eq 0 ]; then
    ok "GF128 模式编译成功 (${#EXPECTED_BINS[@]} 个二进制)"
else
    fail "GF128 模式: $MISSING 个二进制缺失"
    exit 1
fi

# ============================================================
# 阶段 5: 编译主项目 (HH 模式)
# ============================================================
step "阶段 5: 编译主项目 (HH 模式)"

cd "$PROJECT_DIR"
mkdir -p build_hh
cd build_hh

info "cmake 配置 (HH)..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEPS_DIR="$BZS_DIR" \
    -DENABLE_HOMOMORPHIC_HASH=ON \
    2>&1 | tail -5

info "编译中..."
make -j "$NPROC" 2>&1 | tail -10

if [ -f "$PROJECT_DIR/build_hh/perf_network" ]; then
    ok "HH 模式编译成功"
else
    warn "HH 模式编译失败 (可能缺少 libsodium)"
    warn "HH 模式为可选项, GF128 模式已可用"
fi

fi # DO_BUILD

# ============================================================
# 阶段 6: 冒烟测试
# ============================================================
if $DO_TEST; then

step "阶段 6: 冒烟测试"

cd "$PROJECT_DIR"

run_test() {
    local name="$1"
    local bin="$2"
    local timeout="${3:-60}"

    if [ ! -f "$bin" ]; then
        warn "$name: 二进制不存在, 跳过"
        return 0
    fi

    info "运行 $name ..."
    if timeout "$timeout" "$bin" >/dev/null 2>&1; then
        ok "$name 通过"
        return 0
    else
        local code=$?
        if [ "$code" -eq 124 ]; then
            warn "$name 超时 (${timeout}s), 可能需要网络连接"
        else
            fail "$name 失败 (exit code: $code)"
        fi
        return 1
    fi
}

PASS=0
TOTAL=0

for test_bin in \
    "MpsOprf_Tests:build/mpsoprf_tests" \
    "Bicentric_Tests:build/bicentric_tests" \
    "Ring_Tests:build/ring_tests" \
    "Security_Tests:build/security_tests"; do

    name="${test_bin%%:*}"
    bin="${test_bin##*:}"
    TOTAL=$((TOTAL + 1))

    if run_test "$name" "$PROJECT_DIR/$bin" 120; then
        PASS=$((PASS + 1))
    fi
done

# BZS-MPSI 单元测试
BZS_FRONTEND="$BZS_DIR/out/build/linux/frontend/frontend"
if [ -f "$BZS_FRONTEND" ]; then
    TOTAL=$((TOTAL + 1))
    info "运行 BZS-MPSI 单元测试..."
    if timeout 120 "$BZS_FRONTEND" -u -mpsi >/dev/null 2>&1; then
        ok "BZS-MPSI 单元测试通过"
        PASS=$((PASS + 1))
    else
        warn "BZS-MPSI 单元测试失败或超时"
    fi
fi

echo ""
if [ "$PASS" -eq "$TOTAL" ]; then
    ok "全部通过: $PASS/$TOTAL"
else
    warn "通过 $PASS/$TOTAL"
fi

fi # DO_TEST

# ============================================================
# 最终汇总
# ============================================================
step "安装完成"

echo ""
info "二进制文件位置:"

check_bin() {
    if [ -f "$1" ]; then
        ok "  $1"
    else
        warn " $1 (不存在)"
    fi
}

check_bin "$BZS_DIR/out/build/linux/frontend/frontend"
check_bin "$ORING_DIR/build/mpsi"
check_bin "$ORING_DIR/build/oringt1"
check_bin "$PROJECT_DIR/build/perf_network"
check_bin "$PROJECT_DIR/build_hh/perf_network"
check_bin "$PROJECT_DIR/build/mps_oprf_frontend"

echo ""
info "常用命令:"
echo "  # GF128 性能测试"
echo "  cd $PROJECT_DIR/build && ./perf_tests_protocols"
echo ""
echo "  # HH 模式性能测试"
echo "  cd $PROJECT_DIR/build_hh && ./perf_tests_protocols"
echo ""
echo "  # BZS-MPSI 3方性能测试"
echo "  FE=$BZS_DIR/out/build/linux/frontend/frontend"
echo "  \$FE -perf -mpsi -nu 3 -id 0 -nn 12 &"
echo "  \$FE -perf -mpsi -nu 3 -id 1 -nn 12 &"
echo "  \$FE -perf -mpsi -nu 3 -id 2 -nn 12"
echo ""
echo "  # O-Ring 3方测试"
echo "  cd $ORING_DIR/build"
echo "  ./mpsi 3 1 10 0 0 0 & ./mpsi 3 1 10 1 0 0 & ./mpsi 3 1 10 2 0 0"
echo ""
echo "  # Table 5 完整基准测试"
echo "  bash $PROJECT_DIR/scripts/run_table5.sh"
echo ""
ok "全部完成"
