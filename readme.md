# MPS-OPRF-MPSI 配置与运行文档

## 目录

- [1. 项目概述](#1-项目概述)
- [2. 系统要求](#2-系统要求)
- [3. 目录结构](#3-目录结构)
- [4. 环境安装](#4-环境安装)
- [5. 编译构建](#5-编译构建)
- [6. 运行测试](#6-运行测试)
- [7. Table 5 完整复现](#7-table-5-完整复现)
- [8. 结果解析](#8-结果解析)
- [9. 各协议二进制用法](#9-各协议二进制用法)
- [10. 常见问题](#10-常见问题)
- [11. 快速检查清单](#11-快速检查清单)

---

## 1. 项目概述

本项目实现了多种多方隐私集合求交 (Multi-Party PSI) 协议的性能基准测试框架，用于复现论文 Table 5 的实验数据。

**包含的协议（共 11 种）：**

| 协议 | 来源 | 二进制 | 模式 |
|:---|:---|:---|:---|
| OPRF | 本文 | `build/perf_network` | GF128 |
| BC (Bicentric) | 本文 | `build/perf_network` | GF128 |
| Ring | 本文 | `build/perf_network` | GF128 |
| OPRF-HH | 本文 | `build_hh/perf_network` | Homomorphic Hash |
| BC-HH | 本文 | `build_hh/perf_network` | Homomorphic Hash |
| Ring-HH | 本文 | `build_hh/perf_network` | Homomorphic Hash |
| BZS-MPSI | CCS 2024 | `thirdparty_bzs/.../frontend` | — |
| O-Ring (Ring) | USENIX Sec 2024 | `thirdparty_oring/build/mpsi` | Ring 拓扑 |
| O-Ring (Star) | USENIX Sec 2024 | `thirdparty_oring/build/mpsi` | Star 拓扑 |
| mPSI-Paxos | CCS 2021 | `thirdparty_mpsi_paxos/bin/frontend.exe` | 基线对比 |
| MultipartyPSI | CCS 2017 | `thirdparty_multipartypsi/bin/frontend.exe` | 基线对比 |

---

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
| 密码学库 | libssl-dev、libsodium-dev (HH 模式)、libboost-all-dev |
| 其他 | pkg-config、Python3 |

---

## 3. 目录结构

```
mps-oprf-mpsi/
├── scripts/                        # 脚本目录
│   ├── config.sh                   #   路径配置 (自动推导，无需手动修改)
│   ├── setup_ubuntu.sh             #   一键安装脚本 (6 阶段)
│   ├── build_all.sh                #   快速构建脚本
│   ├── build_offline.sh            #   离线构建 (无需网络)
│   ├── build_thirdparty.sh         #   编译第三方基线 (mPSI-Paxos, MultipartyPSI)
│   ├── run_benchmark.sh            #   运行单个协议测试
│   ├── run_table5.sh               #   运行完整 Table 5
│   ├── parse_results.sh            #   解析日志生成 CSV
│   └── setup_network.sh            #   LAN/WAN 网络模拟
│
├── src/                            # 源代码
├── include/                        # 头文件
├── frontend/
│   └── perf_network.cpp            # 主测试入口 (OPRF/BC/Ring)
├── tests/                          # 单元测试 (18 个测试文件)
│
├── thirdparty_bzs/                 # BZS-MPSI (CCS 2024)
│   └── out/build/linux/frontend/frontend
├── thirdparty_oring/               # O-Ring (USENIX Security 2024)
│   └── build/{mpsi, oringt1}
├── thirdparty_mpsi_paxos/          # mPSI-Paxos (CCS 2021)
│   └── bin/frontend.exe
├── thirdparty_multipartypsi/       # MultipartyPSI (CCS 2017)
│   └── bin/frontend.exe
│
├── build/                          # GF128 模式编译输出
│   └── perf_network
├── build_hh/                       # HH 模式编译输出
│   └── perf_network
├── logs/                           # 测试日志 (自动创建)
├── patches/                        # O-Ring CMake 补丁
│
├── CMakeLists.txt                  # 主 CMake 配置
└── table5_results.csv              # 解析后的实验结果
```

---

## 4. 环境安装

### 方式一：一键安装（推荐）

```bash
cd mps-oprf-mpsi
chmod +x scripts/setup_ubuntu.sh
bash scripts/setup_ubuntu.sh
```

**执行阶段：**

| 阶段 | 内容 | 耗时 |
|:---|:---|:---|
| 0 | 检查 Ubuntu 系统，检测 CPU 核数 | <1 分钟 |
| 1 | 安装系统依赖 (34 个 apt 包) | 3-5 分钟 |
| 2 | 编译 BZS-MPSI 及子依赖 (libOTe, cryptoTools 等) | 5-15 分钟 |
| 3 | 编译 O-Ring，应用 CMake 补丁 | 2-5 分钟 |
| 4 | 编译本项目 GF128 模式 | 1-3 分钟 |
| 5 | 编译本项目 HH 模式 (可选，需 libsodium) | 1-3 分钟 |
| 6 | 运行 5 项冒烟测试 | 2-5 分钟 |

安装成功后输出：

```
thirdparty_bzs/out/build/linux/frontend/frontend   ✓
thirdparty_oring/build/mpsi                         ✓
thirdparty_oring/build/oringt1                      ✓
build/perf_network                                   ✓
build_hh/perf_network                                ✓
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

适用于无法访问外网的环境。前提：`thirdparty_bzs/out/install/linux/` 下已有预编译的 `.a` 文件。

```bash
bash scripts/build_offline.sh
```

---

## 5. 编译构建

### 5.1 编译本项目

**GF128 模式（默认）：**

```bash
cd mps-oprf-mpsi
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**HH 模式（需要 libsodium）：**

```bash
mkdir -p build_hh && cd build_hh
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_HOMOMORPHIC_HASH=ON
make -j$(nproc)
```

### 5.2 编译第三方基线

```bash
# 编译 mPSI-Paxos 和 MultipartyPSI
bash scripts/build_thirdparty.sh all

# 或单独编译
bash scripts/build_thirdparty.sh mpsi-paxos
bash scripts/build_thirdparty.sh multipartypsi
```

编译完成后二进制位置：

| 协议 | 二进制路径 |
|:---|:---|
| mPSI-Paxos | `thirdparty_mpsi_paxos/bin/frontend.exe` |
| MultipartyPSI | `thirdparty_multipartypsi/bin/frontend.exe` |

### 5.3 编译 BZS-MPSI（已包含在一键安装中）

```bash
cd thirdparty_bzs
cmake --preset linux
cmake --build out/build/linux --parallel $(nproc)
cmake --install out/build/linux
```

### 5.4 编译 O-Ring（已包含在一键安装中）

```bash
cd thirdparty_oring
cp ../patches/oring_CMakeLists.txt CMakeLists.txt   # 应用补丁
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 5.5 验证所有二进制

```bash
echo "=== 检查所有二进制 ==="
for bin in \
  build/perf_network \
  build_hh/perf_network \
  thirdparty_bzs/out/build/linux/frontend/frontend \
  thirdparty_oring/build/mpsi \
  thirdparty_oring/build/oringt1 \
  thirdparty_mpsi_paxos/bin/frontend.exe \
  thirdparty_multipartypsi/bin/frontend.exe; do
  if [ -x "$bin" ]; then
    echo "  [OK] $bin"
  else
    echo "  [MISSING] $bin"
  fi
done
```

---

## 6. 运行测试

### 6.1 单元测试

```bash
# 本项目测试
cd build
./mpsoprf_tests          # MPS-OPRF 核心测试
./bicentric_tests        # Bicentric-MPSI 拓扑测试
./ring_tests             # Ring-MPSI 拓扑测试
./security_tests         # 安全性验证
./cointoss_tests         # 硬币投掷协议
./vole_tests             # vOLE 包装器

# BZS-MPSI 单元测试
thirdparty_bzs/out/build/linux/frontend/frontend -u -mpsi
```

### 6.2 单协议性能测试

**通用命令：**

```bash
bash scripts/run_benchmark.sh <协议> <n> <t> <nn> [网络] [端口]
```

**参数说明：**

| 参数 | 说明 | 取值 |
|:---|:---|:---|
| `协议` | 协议名称 | oprf, bc, ring, oprf-hh, bc-hh, ring-hh, bzs, oring-ring, oring-star, mpsi-paxos, multipartypsi |
| `n` | 参与方数量 | 3, 4, 5, 10, 15, ... |
| `t` | 恶意门限 | 1 ≤ t < n |
| `nn` | 集合大小的 log₂ | 12 (=4096), 16 (=65536), 20 (=1048576) |
| `端口` | 基础端口 (可选) | 默认 20000，仅本文协议使用 |

**示例：**

```bash
# 本文 OPRF：4方, t=1, 2^12 元素
bash scripts/run_benchmark.sh oprf 4 1 12

# 本文 Ring-HH：10方, t=4, 2^16 元素
bash scripts/run_benchmark.sh ring-hh 10 4 16

# BZS-MPSI：15方, t=1, 2^20 元素
bash scripts/run_benchmark.sh bzs 15 1 20

# O-Ring (ring 拓扑)：10方, t=1, 2^16 元素
bash scripts/run_benchmark.sh oring-ring 10 1 16

# O-Ring (star 拓扑)：5方, t=2, 2^12 元素
bash scripts/run_benchmark.sh oring-star 5 2 12

# mPSI-Paxos：4方, t=1, 2^12 元素
bash scripts/run_benchmark.sh mpsi-paxos 4 1 12

# MultipartyPSI：4方, t=1, 2^12 元素
bash scripts/run_benchmark.sh multipartypsi 4 1 12
```

---

## 7. Table 5 完整复现

### 7.1 运行全部测试

```bash
bash scripts/run_table5.sh
```

**测试范围：**

- **协议 (11)**: oprf, oprf-hh, bc, bc-hh, ring, ring-hh, bzs, oring-ring, oring-star, mpsi-paxos, multipartypsi
- **集合大小 (3)**: 2^12, 2^16, 2^20
- **参与方/门限组合 (9)**: (4,1) (4,3) (10,1) (10,4) (10,9) (15,1) (15,4) (15,7) (15,14)
- **网络环境 (1)**: LAN

**总计**: 11 × 3 × 9 = **297 组测试**

**预计耗时**: 3-6 小时 (取决于 CPU 性能)

**注意事项：**
- BZS、O-Ring、mPSI-Paxos、MultipartyPSI 使用固定端口，测试串行执行
- 测试之间有 2 秒间隔以释放端口

### 7.2 日志输出

日志自动保存到 `logs/` 目录：

```
logs/
├── lan_oprf_n4_t1_m12.log
├── lan_oprf_n4_t3_m12.log
├── lan_bc_n4_t1_m12.log
├── lan_bzs_n4_t1_m12.log
├── lan_mpsi-paxos_n4_t1_m12.log
├── lan_multipartypsi_n4_t1_m12.log
└── ...
```

文件名格式：`{lan|wan}_{协议}_n{参与方数}_t{门限}_m{集合大小log2}.log`

### 7.3 部分运行

如只想跑特定协议或参数，修改 `run_table5.sh` 中的变量：

```bash
# 只跑两个新增基线
PROTOCOLS="mpsi-paxos multipartypsi"

# 只跑小集合
SET_SIZES="12"

# 只跑 4 方
NT_COMBOS="4,1 4,3"
```

---

## 8. 结果解析

### 8.1 生成 CSV

```bash
bash scripts/parse_results.sh
# 默认输入: logs/
# 默认输出: table5_results.csv
```

自定义路径：

```bash
bash scripts/parse_results.sh /path/to/logs /path/to/output.csv
```

### 8.2 CSV 格式

```csv
Setting,Protocol,m,n,t,Receiver_Time_s,Receiver_Active_Time_s,Receiver_Comm_KB,Leader_Time_s,Leader_Comm_KB,Max_Sender_Time_s,Max_Sender_ClientActive_Time_s,Max_Sender_Comm_KB,Total_Time_s,Total_Comm_KB
lan,oprf,2^12,4,1,0.034,N/A,92.50,0.032,88.25,0.031,0.004,85.75,0.034,266.50
lan,bc,2^12,4,1,0.085,0.061,120.00,0.082,115.50,0.079,0.052,110.25,0.085,345.75
lan,mpsi-paxos,2^12,4,1,0.120,N/A,150.00,0.118,145.00,0.115,N/A,140.00,0.120,435.00
...
```

**列说明 (15 列)：**

| 列 | 说明 |
|:---|:---|
| Setting | 网络环境: lan / wan |
| Protocol | 协议名称 |
| m | 集合大小 (2^nn) |
| n | 参与方数量 |
| t | 恶意门限 |
| Receiver_Time_s | 接收方时间 (秒) |
| Receiver_Active_Time_s | 接收方有效处理时间（仅我们的 bc/ring 协议有值） |
| Receiver_Comm_KB | 接收方通信量 (KB) |
| Leader_Time_s | Leader 时间 (秒) |
| Leader_Comm_KB | Leader 通信量 (KB) |
| Max_Sender_Time_s | 最慢 Sender 时间 (秒) |
| Max_Sender_ClientActive_Time_s | 最慢 Sender 的客户口径时间（仅我们的协议有值） |
| Max_Sender_Comm_KB | 最大 Sender 通信量 (KB) |
| Total_Time_s | 总时间 = max(所有方) |
| Total_Comm_KB | 总通信量 = sum(所有方) |

对于本文协议 (`oprf / bc / ring`)，所有角色都会先完成本地链路建立，再通过 leader 协调的全局 `READY/START` barrier 统一起表；barrier 本身的控制字节不计入 `Comm`。

### 8.3 各协议的时间/通信量取法

| 协议 | Total_Time 取法 | 说明 |
|:---|:---|:---|
| 本文 (oprf/bc/ring) | max(Receiver, Leader, Sender) | 所有角色在全局链路就绪并完成 ready barrier 后统一起表，取最慢方 |
| BZS-MPSI | Receiver 时间 | Receiver 最后完成，计算交集 |
| O-Ring | party 0 时间 | 客户口径：总时间取 ID=0，单用户时间取 ID=1 |
| mPSI-Paxos | last party 时间 | last party (n-1) 计算最终交集 |
| MultipartyPSI | Leader 时间 | 日志按 Client/Leader 分块提取，单用户时间取 Client，Total 取 Leader |

---

## 9. 各协议二进制用法

### 9.1 本文协议 (GF128 / HH)

```bash
# GF128 模式
./build/perf_network -nu <n> -t <t> -id <id> -nn <nn> -proto <proto> -net <net> -port <port>

# HH 模式
./build_hh/perf_network -nu <n> -t <t> -id <id> -nn <nn> -proto <proto> -net <net> -port <port>
```

| 参数 | 说明 |
|:---|:---|
| `-nu` | 参与方数量 |
| `-t` | 恶意门限 |
| `-id` | 当前方 ID (0~n-3: Sender, n-2: Leader, n-1: Receiver) |
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

### 9.2 BZS-MPSI

```bash
./thirdparty_bzs/out/build/linux/frontend/frontend -perf -mpsi -nu <n> -id <id> -nn <nn>
```

| 参数 | 说明 |
|:---|:---|
| `-perf` | 性能测试模式 |
| `-mpsi` | MPSI 协议 |
| `-nu` | 参与方数量 |
| `-id` | 当前方 ID (n-1: Receiver, n-2: Leader/Pivot, 0~n-3: Client) |
| `-nn` | 集合大小 log₂ |

> **注意**: BZS 使用固定端口 (10000/10100/10500 系列)，不可并行运行多个实例。

### 9.3 O-Ring

```bash
# t >= 2 使用 mpsi
./thirdparty_oring/build/mpsi <n> <t> <nn> <party_id> <topology> <net>

# t = 1 使用 oringt1 (专用优化版本)
./thirdparty_oring/build/oringt1 <n> <t> <nn> <party_id> <topology> <net>
```

| 参数 | 说明 |
|:---|:---|
| `n` | 参与方数量 |
| `t` | 恶意门限 |
| `nn` | 集合大小 log₂ |
| `party_id` | 当前方 ID (0-indexed) |
| `topology` | 0 = Ring 拓扑, 1 = Star 拓扑 |
| `net` | 0 = LAN, 1 = WAN |

### 9.4 mPSI-Paxos (CCS 2021)

```bash
./thirdparty_mpsi_paxos/bin/frontend.exe -m <nn> -n <n> -t <t> -p <party_id>
```

| 参数 | 说明 |
|:---|:---|
| `-m` | 集合大小 log₂ (必须在最前) |
| `-n` | 参与方数量 |
| `-t` | 恶意门限 |
| `-p` | 当前方 ID |

> **参数顺序固定**: 必须严格按 `-m -n -t -p` 的顺序传参 (位置参数解析，非 getopt)。

### 9.5 MultipartyPSI (CCS 2017)

```bash
./thirdparty_multipartypsi/bin/frontend.exe -n <n> -t <t> -m <nn> -p <party_id>
```

| 参数 | 说明 |
|:---|:---|
| `-n` | 参与方数量 (必须在最前) |
| `-t` | 恶意门限 |
| `-m` | 集合大小 log₂ |
| `-p` | 当前方 ID |

> **参数顺序固定**: 必须严格按 `-n -t -m -p` 的顺序传参。注意与 mPSI-Paxos 的顺序不同。

---

## 10. 常见问题

### Q1: CMake 版本过低

```
CMake Error: CMAKE_CXX_STANDARD is set to 20, which is not supported
```

**解决**: 脚本会自动从 Kitware APT 源安装最新 CMake，或手动安装：

```bash
sudo apt-get install -y software-properties-common
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | sudo apt-key add -
sudo apt-add-repository 'deb https://apt.kitware.com/ubuntu/ jammy main'
sudo apt-get update && sudo apt-get install cmake
```

### Q2: 编译器不支持 C++20

**解决**:

```bash
sudo apt-get install g++-12
export CXX=g++-12
```

### Q3: 端口冲突 ("Address already in use")

```bash
# 使用不同端口
bash scripts/run_benchmark.sh oprf 4 1 12 lan 25000

# 或清理残留进程
pkill -f perf_network
pkill -f frontend
pkill -f mpsi
```

### Q4: 内存不足 (m=2^20 大集合)

- 减小集合: 先用 nn=12 或 nn=16 测试
- 增加 swap:
  ```bash
  sudo fallocate -l 8G /swapfile
  sudo chmod 600 /swapfile
  sudo mkswap /swapfile
  sudo swapon /swapfile
  ```
- 减少并发方数 (n)

### Q5: BZS-MPSI 编译失败 (网络超时)

libOTe 等子依赖需要从 GitHub 拉取。

```bash
# 重试
cd thirdparty_bzs
cmake --preset linux
cmake --build out/build/linux --parallel $(nproc)

# 或使用离线模式 (需预编译的 .a 文件)
cd ..
bash scripts/build_offline.sh
```

### Q6: HH 模式编译失败 (libsodium 未找到)

```bash
sudo apt-get install libsodium-dev

cd build_hh
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_HOMOMORPHIC_HASH=ON
make -j$(nproc)
```

HH 模式为可选功能，GF128 模式不依赖 libsodium。

### Q7: mPSI-Paxos / MultipartyPSI 编译依赖缺失

这两个项目依赖 Boost、NTL、Miracl。确保已安装：

```bash
sudo apt-get install libboost-all-dev libntl-dev
```

Miracl 库通常已包含在 `thirdparty/` 子目录中。如编译仍报错，检查 CMakeLists.txt 中的库路径是否正确。

---

## 11. 快速检查清单

```
[ ] 1. 将 mps-oprf-mpsi/ 目录传到 Linux VM
       scp -r mps-oprf-mpsi/ user@vm_ip:~/

[ ] 2. 一键安装
       cd ~/mps-oprf-mpsi
       bash scripts/setup_ubuntu.sh

[ ] 3. 编译第三方基线
       bash scripts/build_thirdparty.sh all

[ ] 4. 验证所有二进制存在
       build/perf_network                                    ✓
       build_hh/perf_network                                 ✓
       thirdparty_bzs/out/build/linux/frontend/frontend      ✓
       thirdparty_oring/build/mpsi                           ✓
       thirdparty_oring/build/oringt1                        ✓
       thirdparty_mpsi_paxos/bin/frontend.exe                ✓
       thirdparty_multipartypsi/bin/frontend.exe             ✓

[ ] 5. 单协议冒烟测试
       bash scripts/run_benchmark.sh oprf 4 1 12
       bash scripts/run_benchmark.sh bzs 4 1 12
       bash scripts/run_benchmark.sh mpsi-paxos 4 1 12
       bash scripts/run_benchmark.sh multipartypsi 4 1 12

[ ] 6. 运行完整 Table 5
       bash scripts/run_table5.sh

[ ] 7. 解析结果
       bash scripts/parse_results.sh
       # 输出: table5_results.csv
```
