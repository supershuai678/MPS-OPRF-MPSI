// MPS-OPRF-MPSI Performance Test - Simplified
#include <iostream>
#include <chrono>
#include <iomanip>
#include <set>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include "HomomorphicPRF.h"
#include "SecretShare.h"

using namespace osuCrypto;
using namespace std::chrono;

struct BenchmarkResult {
    double total_ms;
    double prf_ms;
    size_t comm_bytes;
};

BenchmarkResult benchmark_PRF(size_t numParties, size_t setSize, PRNG& prng) {
    BenchmarkResult result = {};
    
    // 生成测试数据
    std::vector<block> testInputs(setSize);
    for (size_t i = 0; i < setSize; ++i) {
        testInputs[i] = prng.get<block>();
    }
    
    auto total_start = high_resolution_clock::now();
    
    // 生成密钥
    mpsoprf::Scalar w = mpsoprf::HomomorphicPRF::randomKey(prng);
    
    // 分割密钥
    block w_block = w.toBlock();  // 只取前16字节，用于秘密分享
    auto w_shares = mpsoprf::SecretShare::split(w_block, numParties, prng);
    
    // 计算PRF值
    auto prf_start = high_resolution_clock::now();
    std::vector<mpsoprf::Point> prfValues(setSize);
    for (size_t j = 0; j < setSize; ++j) {
        prfValues[j] = mpsoprf::HomomorphicPRF::eval(w, testInputs[j]);
    }
    auto prf_end = high_resolution_clock::now();
    result.prf_ms = duration_cast<microseconds>(prf_end - prf_start).count() / 1000.0;
    
    auto total_end = high_resolution_clock::now();
    result.total_ms = duration_cast<milliseconds>(total_end - total_start).count();
    
    // 通信量估算
    result.comm_bytes = numParties * setSize * crypto_core_ristretto255_BYTES;
    
    return result;
}

void run_quick_benchmark() {
    PRNG prng(sysRandomSeed());
    
    std::cout << "========================================" << std::endl;
    std::cout << "MPS-OPRF-MPSI Quick Benchmark" << std::endl;
    std::cout << "(Elliptic Curve: Ristretto255)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::vector<size_t> partyCounts = {3, 4, 5};
    std::vector<size_t> setSizes = {64, 256, 1024};
    
    std::cout << std::left << std::setw(6) << "n" 
              << std::setw(8) << "m" 
              << std::setw(12) << "PRF(ms)" 
              << std::setw(12) << "Total(ms)"
              << std::setw(12) << "Comm(KB)" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    for (size_t m : setSizes) {
        for (size_t n : partyCounts) {
            auto result = benchmark_PRF(n, m, prng);
            std::cout << std::left << std::setw(6) << n 
                      << std::setw(8) << m 
                      << std::setw(12) << std::fixed << std::setprecision(1) << result.prf_ms
                      << std::setw(12) << result.total_ms
                      << std::setw(12) << result.comm_bytes / 1024 << std::endl;
        }
    }
    
    std::cout << std::string(50, '=') << std::endl;
    std::cout << "\nNote: Using Ristretto255 elliptic curve for homomorphic PRF" << std::endl;
    std::cout << "PRF: H_h(k,x) = k * H(x) where H maps to curve point" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "\nMPS-OPRF-MPSI Performance Benchmark" << std::endl;
    std::cout << "Homomorphic PRF: Elliptic Curve (Ristretto255)\n" << std::endl;
    
    run_quick_benchmark();
    
    return 0;
}
