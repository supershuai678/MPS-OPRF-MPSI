#!/bin/bash
# ============================================================
# Table 5 路径配置 — 全部使用项目内相对路径
# ============================================================

# 本项目根目录 (mps-oprf-mpsi/)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# 第三方依赖（已内置在项目内）
export BZS_DIR="$PROJECT_DIR/thirdparty_bzs"
export ORING_DIR="$PROJECT_DIR/thirdparty_oring"
export MPSI_PAXOS_DIR="$PROJECT_DIR/thirdparty_mpsi_paxos"
export MULTIPARTYPSI_DIR="$PROJECT_DIR/thirdparty_multipartypsi"

# 二进制路径
export BIN_GF128="$PROJECT_DIR/build/perf_network"
export BIN_HH="$PROJECT_DIR/build_hh/perf_network"
export BIN_BZS="$BZS_DIR/out/build/linux/frontend/frontend"
export BIN_ORING="$ORING_DIR/build/mpsi"
export BIN_ORING_T1="$ORING_DIR/build/oringt1"
export BIN_MPSI_PAXOS="$MPSI_PAXOS_DIR/bin/frontend.exe"
export BIN_MULTIPARTYPSI="$MULTIPARTYPSI_DIR/bin/frontend.exe"
