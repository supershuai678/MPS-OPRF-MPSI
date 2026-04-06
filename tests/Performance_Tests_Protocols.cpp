// MPS-OPRF-MPSI 协议性能测试
// 测试 Bicentric 和 Ring 两种MPSI协议结构
//
// 协议说明:
// 1. Bicentric (双中心): 所有发送方直接发送OKVS给接收方
// 2. Ring (环形): OKVS沿环形传递，累积PRF值
//
// When ENABLE_HOMOMORPHIC_HASH is defined:
//   EC overhead is injected inside evalPRF (see MpsOprf.cpp)
//   This benchmark measures the additional cost of EC operations
//
#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <set>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <volePSI/Paxos.h>
#include "HomomorphicPRF.h"
#include "SecretShare.h"
#include "BicentricMpsi.h"
#include "RingMpsi.h"

using namespace osuCrypto;
using namespace mpsoprf;
using namespace std::chrono;

struct ProtocolBenchmarkResult {
    std::string protocol;
    size_t n;           // 参与方数量
    size_t m;           // 集合大小
    double total_ms;    // 总时间
    double prf_ms;      // PRF计算时间
    double okvs_ms;     // OKVS编解码时间
    size_t comm_kb;     // 通信量(KB)
    size_t intersection_found;   // 交集大小
    size_t intersection_expected; // 期望交集大小
};

// 生成测试集合
std::vector<block> generateSet(size_t size, PRNG& prng, const std::vector<block>& intersection) {
    std::vector<block> set;
    std::set<block> seen;
    for (const auto& elem : intersection) {
        set.push_back(elem);
        seen.insert(elem);
    }
    while (set.size() < size) {
        block r = prng.get<block>();
        if (seen.find(r) == seen.end()) {
            set.push_back(r);
            seen.insert(r);
        }
    }
    return set;
}

// Bicentric协议性能测试（本地模拟）
ProtocolBenchmarkResult benchmark_bicentric(size_t numParties, size_t setSize, PRNG& prng) {
    ProtocolBenchmarkResult result = {};
    result.protocol = "MPS-OPRF-Bicentric";
    result.n = numParties;
    result.m = setSize;

    // 生成交集
    size_t intersectionSize = setSize / 10;
    std::vector<block> intersection;
    for (size_t i = 0; i < intersectionSize; ++i) {
        intersection.push_back(prng.get<block>());
    }

    // 生成各方集合
    std::vector<std::vector<block>> sets(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        sets[i] = generateSet(setSize, prng, intersection);
    }

    auto total_start = high_resolution_clock::now();

    // 1. 生成密钥并分割 (GF128 block keys)
    block w_block = prng.get<block>();
    auto w_shares = SecretShare::split(w_block, numParties, prng);

    // 2. PRF计算（每个发送方计算自己集合的PRF值）
    auto prf_start = high_resolution_clock::now();
    std::vector<std::vector<block>> prfValues(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        prfValues[i].resize(setSize);
        for (size_t j = 0; j < setSize; ++j) {
            // GF128 PRF: F(x) = H(x) XOR w_i
            block hx = GF128PRF::hashToField(sets[i][j]);
            prfValues[i][j] = hx ^ w_shares[i];
#ifdef ENABLE_HOMOMORPHIC_HASH
            // EC overhead simulation
            HomomorphicPRF::eval(Scalar(w_shares[i]), sets[i][j]);
#endif
        }
    }
    auto prf_end = high_resolution_clock::now();
    result.prf_ms = duration_cast<microseconds>(prf_end - prf_start).count() / 1000.0;

    // 3. OKVS编解码
    auto okvs_start = high_resolution_clock::now();

    volePSI::Baxos paxos;
    paxos.init(setSize, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, block(0, 0));

    // 每个发送方编码OKVS
    std::vector<std::vector<block>> T(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        T[i].resize(paxos.size());
        paxos.solve<block>(span<const block>(sets[i]), span<const block>(prfValues[i]),
                    span<block>(T[i]), &prng, 1);
    }

    // 接收方解码所有OKVS并检查交集
    // XOR of numParties copies of H(y) = H(y) if numParties is odd, 0 if even
    // XOR of all w_shares = w_block
    // So expected = ((numParties % 2 == 1) ? H(y) : 0) ^ w_block
    result.intersection_found = 0;
    result.intersection_expected = intersectionSize;
    for (size_t j = 0; j < setSize; ++j) {
        block accumulated = oc::ZeroBlock;
        for (size_t i = 0; i < numParties; ++i) {
            block decoded;
            paxos.decode<block>(span<const block>(&sets[0][j], 1), span<block>(&decoded, 1),
                        span<const block>(T[i]), 1);
            accumulated = accumulated ^ decoded;
        }

        block hx = GF128PRF::hashToField(sets[0][j]);
        block expected = (numParties % 2 == 1) ? (hx ^ w_block) : w_block;
        if (accumulated == expected) {
            result.intersection_found++;
        }
    }

    auto okvs_end = high_resolution_clock::now();
    result.okvs_ms = duration_cast<microseconds>(okvs_end - okvs_start).count() / 1000.0;

    auto total_end = high_resolution_clock::now();
    result.total_ms = duration_cast<milliseconds>(total_end - total_start).count();

    // 通信量: 每个发送方发送1个OKVS给接收方
    size_t okvs_size = (size_t)(1.3 * setSize * 16);
    result.comm_kb = (numParties * okvs_size) / 1024;

    return result;
}

// Ring协议性能测试（本地模拟）
ProtocolBenchmarkResult benchmark_ring(size_t numParties, size_t setSize, PRNG& prng) {
    ProtocolBenchmarkResult result = {};
    result.protocol = "MPS-OPRF-Ring";
    result.n = numParties;
    result.m = setSize;

    // 生成交集
    size_t intersectionSize = setSize / 10;
    std::vector<block> intersection;
    for (size_t i = 0; i < intersectionSize; ++i) {
        intersection.push_back(prng.get<block>());
    }

    // 生成各方集合
    std::vector<std::vector<block>> sets(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        sets[i] = generateSet(setSize, prng, intersection);
    }

    auto total_start = high_resolution_clock::now();

    // 1. 生成密钥并分割 (GF128 block keys)
    block w_block = prng.get<block>();
    auto w_shares = SecretShare::split(w_block, numParties, prng);

    // 2. PRF计算
    auto prf_start = high_resolution_clock::now();
    std::vector<std::vector<block>> prfValues(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        prfValues[i].resize(setSize);
        for (size_t j = 0; j < setSize; ++j) {
            block hx = GF128PRF::hashToField(sets[i][j]);
            prfValues[i][j] = hx ^ w_shares[i];
#ifdef ENABLE_HOMOMORPHIC_HASH
            // EC overhead simulation
            HomomorphicPRF::eval(Scalar(w_shares[i]), sets[i][j]);
#endif
        }
    }
    auto prf_end = high_resolution_clock::now();
    result.prf_ms = duration_cast<microseconds>(prf_end - prf_start).count() / 1000.0;

    // 3. 环形OKVS传递
    auto okvs_start = high_resolution_clock::now();

    volePSI::Baxos paxos;
    paxos.init(setSize, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, block(0, 0));

    // Sn编码初始OKVS
    std::vector<block> T_current(paxos.size());
    paxos.solve<block>(span<const block>(sets[numParties-1]), span<const block>(prfValues[numParties-1]),
                span<block>(T_current), &prng, 1);

    // 环形传递: S_{n-1} -> S_{n-2} -> ... -> S_1
    for (size_t i = numParties - 2; i > 0; --i) {
        std::vector<block> values(setSize);
        for (size_t j = 0; j < setSize; ++j) {
            block decoded;
            paxos.decode<block>(span<const block>(&sets[i][j], 1), span<block>(&decoded, 1),
                        span<const block>(T_current), 1);
            // GF128: XOR accumulation
            values[j] = prfValues[i][j] ^ decoded;
        }

        std::vector<block> T_new(paxos.size());
        paxos.solve<block>(span<const block>(sets[i]), span<const block>(values),
                    span<block>(T_new), &prng, 1);
        T_current = T_new;
    }

    // S_1最后处理
    {
        std::vector<block> values(setSize);
        for (size_t j = 0; j < setSize; ++j) {
            block decoded;
            paxos.decode<block>(span<const block>(&sets[0][j], 1), span<block>(&decoded, 1),
                        span<const block>(T_current), 1);
            values[j] = prfValues[0][j] ^ decoded;
        }
        paxos.solve<block>(span<const block>(sets[0]), span<const block>(values),
                    span<block>(T_current), &prng, 1);
    }

    // 接收方解码最终OKVS并检查交集
    // 环形传递后, 对于交集元素: Decode(T_1, y) = ((n%2==1) ? H(y) : 0) ^ w
    result.intersection_found = 0;
    result.intersection_expected = intersectionSize;
    for (size_t j = 0; j < setSize; ++j) {
        block decoded;
        paxos.decode<block>(span<const block>(&sets[0][j], 1), span<block>(&decoded, 1),
                    span<const block>(T_current), 1);

        block hx = GF128PRF::hashToField(sets[0][j]);
        block expected = (numParties % 2 == 1) ? (hx ^ w_block) : w_block;
        if (decoded == expected) {
            result.intersection_found++;
        }
    }

    auto okvs_end = high_resolution_clock::now();
    result.okvs_ms = duration_cast<microseconds>(okvs_end - okvs_start).count() / 1000.0;

    auto total_end = high_resolution_clock::now();
    result.total_ms = duration_cast<milliseconds>(total_end - total_start).count();

    size_t okvs_size = (size_t)(1.3 * setSize * 16);
    result.comm_kb = (numParties * okvs_size) / 1024;

    return result;
}

void run_protocol_benchmark() {
    PRNG prng(sysRandomSeed());

    std::cout << "========================================" << std::endl;
    std::cout << "MPS-OPRF MPSI协议性能测试" << std::endl;
    std::cout << "(Bicentric vs Ring)" << std::endl;
#ifdef ENABLE_HOMOMORPHIC_HASH
    std::cout << "[HH mode: EC overhead enabled]" << std::endl;
#else
    std::cout << "[GF128 mode: no EC overhead]" << std::endl;
#endif
    std::cout << "========================================" << std::endl;

    std::vector<size_t> partyCounts = {3, 4, 10, 15, 20};
    std::vector<size_t> setSizes = {4096, 65536};

    std::vector<ProtocolBenchmarkResult> results;

    std::cout << "\n正在运行测试，请稍候...\n" << std::endl;

    for (size_t m : setSizes) {
        std::cout << "测试集合大小 m=" << m << std::endl;
        for (size_t n : partyCounts) {
            std::cout << "  n=" << n << std::endl;

            std::cout << "    Bicentric... " << std::flush;
            auto bc_result = benchmark_bicentric(n, m, prng);
            results.push_back(bc_result);
            std::cout << bc_result.total_ms << "ms" << std::endl;

            std::cout << "    Ring... " << std::flush;
            auto ring_result = benchmark_ring(n, m, prng);
            results.push_back(ring_result);
            std::cout << ring_result.total_ms << "ms" << std::endl;
        }
        std::cout << std::endl;
    }

    // 打印结果表格
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试结果" << std::endl;
    std::cout << "========================================\n" << std::endl;

    std::cout << std::left
              << std::setw(22) << "Protocol"
              << std::setw(6) << "n"
              << std::setw(10) << "m"
              << std::setw(12) << "Total(ms)"
              << std::setw(12) << "PRF(ms)"
              << std::setw(12) << "OKVS(ms)"
              << std::setw(12) << "Comm(KB)"
              << std::setw(16) << "Intersection" << std::endl;
    std::cout << std::string(100, '-') << std::endl;

    for (const auto& r : results) {
        std::string inter_str = std::to_string(r.intersection_found) + "/" + std::to_string(r.intersection_expected);
        std::cout << std::left
                  << std::setw(22) << r.protocol
                  << std::setw(6) << r.n
                  << std::setw(10) << r.m
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.total_ms
                  << std::setw(12) << std::setprecision(1) << r.prf_ms
                  << std::setw(12) << std::setprecision(1) << r.okvs_ms
                  << std::setw(12) << r.comm_kb
                  << std::setw(16) << inter_str << std::endl;
    }

    // 输出CSV格式
    std::cout << "\n========================================" << std::endl;
    std::cout << "CSV格式输出" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "protocol,parties,set_size,total_ms,prf_ms,okvs_ms,comm_kb,intersection_found,intersection_expected" << std::endl;
    for (const auto& r : results) {
        std::cout << r.protocol << "," << r.n << "," << r.m << ","
                  << std::fixed << std::setprecision(1) << r.total_ms << ","
                  << r.prf_ms << "," << r.okvs_ms << ","
                  << r.comm_kb << "," << r.intersection_found << "," << r.intersection_expected << std::endl;
    }

    // 保存到文件
    std::ofstream csv_file("mps_oprf_protocols_benchmark.csv");
    if (csv_file.is_open()) {
        csv_file << "protocol,parties,set_size,total_ms,prf_ms,okvs_ms,comm_kb,intersection_found,intersection_expected" << std::endl;
        for (const auto& r : results) {
            csv_file << r.protocol << "," << r.n << "," << r.m << ","
                     << std::fixed << std::setprecision(1) << r.total_ms << ","
                     << r.prf_ms << "," << r.okvs_ms << ","
                     << r.comm_kb << "," << r.intersection_found << "," << r.intersection_expected << std::endl;
        }
        csv_file.close();
        std::cout << "\n结果已保存到: mps_oprf_protocols_benchmark.csv" << std::endl;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "测试完成" << std::endl;
    std::cout << "========================================" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "\nMPS-OPRF MPSI Protocol Performance Benchmark" << std::endl;
    std::cout << "Bicentric vs Ring Structure\n" << std::endl;

    run_protocol_benchmark();

    return 0;
}
