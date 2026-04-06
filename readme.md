# MPS-OPRF-MPSI 配置与运行文档

## 1. 项目概述
论文《Multi-Party Private Set Intersection Protocol Based on Multi-Party Shared OPRF》代码实现

## 2. 系统要求

### 2.1 操作系统

- **推荐**: Ubuntu 22.04 LTS
- **兼容**: Ubuntu 20.04 / 24.04、其他 Debian 系

### 2.2 硬件要求

| 组件 | 最低 | 推荐 |
|:---|:---|:---|
| CPU | 4 核 (需支持 AES-NI, AVX2) | 8 核以上 |
| 内存 | 8 GB | 16 GB |
| 磁盘 | 5 GB | 10 GB |

验证 CPU 指令集支持：

```bash
grep -o 'aes\|avx2\|sse4_1\|pclmuldq' /proc/cpuinfo | sort -u
# 应输出: aes avx2 pclmuldq sse4_1
```

### 2.3 软件依赖

| 类别 | 依赖 |
|:---|:---|
| 编译工具 | GCC 10+ (推荐 g++-12)、CMake 3.15+、Ninja/Make、Git |
| 密码学库 | libssl-dev、libsodium-dev、libboost-all-dev |
| 其他 | pkg-config、Python3 |


---

## 3. 环境安装

### 方式一：一键安装（推荐）

```bash
cd mps-oprf-mpsi
chmod +x scripts/setup_ubuntu.sh
bash scripts/setup_ubuntu.sh
```

### 方式二：分阶段安装

```bash
# 仅安装系统依赖
bash scripts/setup_ubuntu.sh --deps

# 仅编译 (跳过 apt 安装)
bash scripts/setup_ubuntu.sh --build

# 仅运行测试 (跳过编译)
bash scripts/setup_ubuntu.sh --test
```

### 方式三：离线安装

```bash
bash scripts/build_offline.sh
```

---

## 4. 编译构建

```bash
cd mps-oprf-mpsi
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```
---

## 5. 运行测试

**通用命令：**

```bash
bash scripts/run_benchmark.sh <协议> <n> <t> <nn> [网络] [端口]
```

**参数说明：**

| 参数 | 说明 | 取值 |
|:---|:---|:---|
| `协议` | 协议名称 | oprf, bc, ring|
| `n` | 参与方数量 | 3, 4, 5, 10, 15, ... |
| `t` | 恶意门限 | 1 ≤ t < n |
| `nn` | 集合大小的 log₂ | 12 (=4096), 16 (=65536), 20 (=1048576) |
| `端口` | 基础端口 (可选) | 默认 20000，仅本文协议使用 |

**示例：**

```bash
# 本文 OPRF：4方, t=1, 2^12 元素
bash scripts/run_benchmark.sh oprf 4 1 12

# BCMPSI：15方, t=1, 2^20 元素
bash scripts/run_benchmark.sh bc 15 1 20

# RMPSI：10方, t=1, 2^16 元素
bash scripts/run_benchmark.sh ring 10 1 16

```

---

## 6. 完整复现

### 6.1 运行全部测试

```bash
bash scripts/run_table5.sh
```

**测试范围：**

- **协议 (3)**: oprf, bc,, ring
- **集合大小 (3)**: 2^12, 2^16, 2^20
- **参与方/门限组合 (9)**: (4,1) (4,3) (10,1) (10,4) (10,9) (15,1) (15,4) (15,7) (15,14)
- **网络环境 (1)**: LAN WAN


```bash
# GF128 模式
./build/perf_network -nu <n> -t <t> -id <id> -nn <nn> -proto <proto> -net <net> -port <port>

```

| 参数 | 说明 |
|:---|:---|
| `-nu` | 参与方数量 |
| `-t` | 恶意门限 |
| `-id` | 当前方 ID |
| `-nn` | 集合大小 log₂ |
| `-proto` | 协议: oprf / bc / ring |
| `-net` | 网络: lan / wan |
| `-port` | 基础 TCP 端口 |

**手动启动示例** (3 方 OPRF)：

```bash
./build/perf_network -nu 3 -t 1 -id 0 -nn 12 -proto oprf -port 20000 &
./build/perf_network -nu 3 -t 1 -id 1 -nn 12 -proto oprf -port 20000 &
./build/perf_network -nu 3 -t 1 -id 2 -nn 12 -proto oprf -port 20000
```
---

