// MPS-OPRF-MPSI Large Scale Performance Test
// 测试大规模数据: n=3,4,10,15,20,50,80,110,140 和 m=4096,65536
#include <iostream>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include "HomomorphicPRF.h"
#include "SecretShare.h"

using namespace osuCrypto;
using namespace std::chrono;

struct BenchmarkResult {
    size_t n;           // 参与方数量
    size_t m;           // 集合大小
    double total_ms;    // 总时间
    double prf_ms;      // PRF计算时间
    double okvs_encode_ms;  // OKVS编码时间
    double okvs_decode_ms;  // OKVS解码时间
    size_t comm_kb;     // 通信量(KB)
};

BenchmarkResult benchmark_full(size_t numParties, size_t setSize, PRNG& prng) {
    BenchmarkResult result = {};
    result.n = numParties;
    result.m = setSize;
    
    // 生成测试数据
    std::vector<block> testInputs(setSize);
    for (size_t i = 0; i < setSize; ++i) {
        testInputs[i] = prng.get<block>();
    }
    
    auto total_start = high_resolution_clock::now();
    
    // 1. 生成密钥
    mpsoprf::Scalar w = mpsoprf::HomomorphicPRF::randomKey(prng);
    
    // 2. 分割密钥给所有参与方
    block w_block = w.toBlock();
    auto w_shares = mpsoprf::SecretShare::split(w_block, numParties, prng);
    
    // 3. 计算PRF值 (同态PRF - 椭圆曲线运算)
    auto prf_start = high_resolution_clock::now();
    std::vector<mpsoprf::Point> prfValues(setSize);
    for (size_t j = 0; j < setSize; ++j) {
        prfValues[j] = mpsoprf::HomomorphicPRF::eval(w, testInputs[j]);
    }
    auto prf_end = high_resolution_clock::now();
    result.prf_ms = duration_cast<microseconds>(prf_end - prf_start).count() / 1000.0;

    
    // 4. 模拟OKVS编码
    auto okvs_encode_start = high_resolution_clock::now();
    for (size_t i = 0; i < numParties; ++i) {
        for (size_t j = 0; j < setSize; ++j) {
            volatile block dummy = testInputs[j] ^ prng.get<block>();
            (void)dummy;
        }
    }
    auto okvs_encode_end = high_resolution_clock::now();
    result.okvs_encode_ms = duration_cast<microseconds>(okvs_encode_end - okvs_encode_start).count() / 1000.0;
    
    // 5. 模拟OKVS解码
    auto okvs_decode_start = high_resolution_clock::now();
    for (size_t i = 0; i < numParties; ++i) {
        for (size_t j = 0; j < setSize; ++j) {
            volatile block dummy = testInputs[j] ^ prng.get<block>();
            (void)dummy;
        }
    }
    auto okvs_decode_end = high_resolution_clock::now();
    result.okvs_decode_ms = duration_cast<microseconds>(okvs_decode_end - okvs_decode_start).count() / 1000.0;
    
    auto total_end = high_resolution_clock::now();
    result.total_ms = duration_cast<milliseconds>(total_end - total_start).count();
    
    // 通信量估算
    size_t okvs_size = (size_t)(1.3 * setSize * 16);
    result.comm_kb = (numParties * okvs_size + numParties * 64) / 1024;
    
    return result;
}

void run_large_benchmark() {
    PRNG prng(sysRandomSeed());
    
    std::cout << "========================================" << std::endl;
    std::cout << "MPS-OPRF-MPSI Large Scale Benchmark" << std::endl;
    std::cout << "(Elliptic Curve: Ristretto255)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<size_t> partyCounts = {3, 4, 10, 15, 20, 50, 80, 110, 140};
    std::vector<size_t> setSizes = {4096, 65536};
    
    std::vector<BenchmarkResult> results;
    
    std::cout << "\n正在运行大规模测试...\n" << std::endl;
    
    for (size_t m : setSizes) {
        std::cout << "测试集合大小 m=" << m << std::endl;
        for (size_t n : partyCounts) {
            std::cout << "  n=" << n << "... " << std::flush;
            auto result = benchmark_full(n, m, prng);
            results.push_back(result);
            std::cout << "完成 (" << result.total_ms << "ms)" << std::endl;
        }
        std::cout << std::endl;
    }
    
    // 打印结果
    std::cout << "\n========================================" << std::endl;
    std::cout << "测试结果" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    std::cout << std::left 
              << std::setw(6) << "n" 
              << std::setw(10) << "m" 
              << std::setw(12) << "Total(ms)" 
              << std::setw(12) << "PRF(ms)"
              << std::setw(12) << "Comm(KB)" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    for (const auto& r : results) {
        std::cout << std::left 
                  << std::setw(6) << r.n 
                  << std::setw(10) << r.m 
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.total_ms
                  << std::setw(12) << std::setprecision(1) << r.prf_ms
                  << std::setw(12) << r.comm_kb << std::endl;
    }
    
    std::cout << "\n测试完成" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "\nMPS-OPRF-MPSI Large Scale Performance Benchmark\n" << std::endl;
    run_large_benchmark();
    return 0;
}
