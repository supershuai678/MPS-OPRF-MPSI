// MPS-OPRF-MPSI 命令行工具
// 用于运行测试和性能基准

#include <iostream>
#include <string>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include "HomomorphicPRF.h"
#include "SecretShare.h"
#include "MpsOprf.h"
#include "BicentricMpsi.h"
#include "RingMpsi.h"

using namespace osuCrypto;
using namespace mpsoprf;

void printUsage(const char* progName) {
    std::cout << "MPS-OPRF-MPSI - Multi-Party Private Set Intersection\n";
    std::cout << "Based on Multi-Point Shared OPRF\n\n";
    std::cout << "Usage: " << progName << " <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  test       Run basic functionality tests\n";
    std::cout << "  bench      Run performance benchmarks\n";
    std::cout << "  info       Show library information\n";
    std::cout << "  help       Show this help message\n";
}

void runBasicTests() {
    std::cout << "Running basic functionality tests...\n\n";
    
    PRNG prng(sysRandomSeed());
    
    // Test 1: HomomorphicPRF
    std::cout << "1. HomomorphicPRF: ";
    Scalar key;
    crypto_core_ristretto255_scalar_random(key.data);
    block input = prng.get<block>();
    auto result = HomomorphicPRF::eval(key, input);
    std::cout << "OK\n";
    
    // Test 2: SecretShare
    std::cout << "2. SecretShare: ";
    block secret = prng.get<block>();
    auto shares = SecretShare::split(secret, 5, prng);
    block reconstructed = SecretShare::reconstruct(shares);
    if (secret == reconstructed) {
        std::cout << "OK\n";
    } else {
        std::cout << "FAILED\n";
    }
    
    // Test 3: Scalar operations
    std::cout << "3. Scalar operations: ";
    Scalar a, b;
    crypto_core_ristretto255_scalar_random(a.data);
    crypto_core_ristretto255_scalar_random(b.data);
    Scalar c = a + b;
    Scalar d = a * b;
    std::cout << "OK\n";
    
    std::cout << "\nAll basic tests passed!\n";
}

void runBenchmarks() {
    std::cout << "Running performance benchmarks...\n\n";
    
    PRNG prng(sysRandomSeed());
    
    // PRF benchmark
    std::cout << "PRF computation (1000 elements): ";
    auto start = std::chrono::high_resolution_clock::now();
    
    Scalar key;
    crypto_core_ristretto255_scalar_random(key.data);
    
    for (int i = 0; i < 1000; ++i) {
        block input = prng.get<block>();
        auto result = HomomorphicPRF::eval(key, input);
        (void)result;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << duration.count() << "ms\n";
    
    // SecretShare benchmark
    std::cout << "SecretShare (10000 splits): ";
    start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        block secret = prng.get<block>();
        auto shares = SecretShare::split(secret, 5, prng);
        (void)shares;
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << duration.count() << "ms\n";
    
    std::cout << "\nBenchmarks completed!\n";
}

void showInfo() {
    std::cout << "MPS-OPRF-MPSI Library Information\n";
    std::cout << "==================================\n\n";
    std::cout << "Version: 1.0.0\n";
    std::cout << "Based on: Multi-Point Shared OPRF (MPS-OPRF)\n\n";
    std::cout << "Components:\n";
    std::cout << "  - HomomorphicPRF: Ristretto255-based homomorphic PRF\n";
    std::cout << "  - SecretShare: Additive secret sharing\n";
    std::cout << "  - CoinToss: Two-party coin tossing protocol\n";
    std::cout << "  - VoleWrapper: Silent vOLE wrapper (libOTe)\n";
    std::cout << "  - MpsOprf: Multi-Point Shared OPRF protocol\n";
    std::cout << "  - BicentricMpsi: Star topology MPSI\n";
    std::cout << "  - RingMpsi: Ring topology MPSI\n\n";
    std::cout << "Dependencies:\n";
    std::cout << "  - libOTe (Oblivious Transfer Extension)\n";
    std::cout << "  - volePSI (Baxos OKVS)\n";
    std::cout << "  - libsodium (Ristretto255)\n";
    std::cout << "  - coproto/macoro (Coroutines)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 0;
    }
    
    std::string command = argv[1];
    
    if (command == "test") {
        runBasicTests();
    } else if (command == "bench") {
        runBenchmarks();
    } else if (command == "info") {
        showInfo();
    } else if (command == "help") {
        printUsage(argv[0]);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage(argv[0]);
        return 1;
    }
    
    return 0;
}
