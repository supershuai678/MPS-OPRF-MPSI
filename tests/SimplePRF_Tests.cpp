// 简化PRF vs 同态PRF 性能对比测试
// 对比 F(x) = Decode(K, x) ⊕ w 和 Hh(k, x) = k * H(x)
// 独立测试，不依赖其他模块

#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cstring>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <sodium.h>

using namespace osuCrypto;
using namespace std::chrono;

// ============================================================================
// 简化PRF: F(x) = Decode(K, x) ⊕ w
// 使用XOR作为"加法"运算，不使用椭圆曲线
// ============================================================================

class SimplePRF {
public:
    // F(x) = decoded_value XOR w
    static block eval(const block& decoded_value, const block& w) {
        return decoded_value ^ w;
    }
    
    // 生成随机w
    static block randomW(PRNG& prng) {
        return prng.get<block>();
    }
};

// ============================================================================
// 同态PRF: Hh(k, x) = k * H(x)
// 使用椭圆曲线Ristretto255
// ============================================================================

class HomomorphicPRF {
public:
    // 哈希到群: H(x) -> Point
    static void hashToGroup(unsigned char* result, const block& x) {
        unsigned char hash[crypto_hash_sha512_BYTES];
        crypto_hash_sha512(hash, (const unsigned char*)&x, sizeof(block));
        crypto_core_ristretto255_from_hash(result, hash);
    }
    
    // 同态PRF: Hh(k, x) = k * H(x)
    static void eval(unsigned char* result, const unsigned char* k, const block& x) {
        unsigned char hx[crypto_core_ristretto255_BYTES];
        hashToGroup(hx, x);
        crypto_scalarmult_ristretto255(result, k, hx);
    }
    
    // 生成随机密钥
    static void randomKey(unsigned char* k) {
        crypto_core_ristretto255_scalar_random(k);
    }
};

// ============================================================================
// 性能测试
// ============================================================================

// 测试简化PRF性能
double testSimplePRF(size_t count) {
    PRNG prng(sysRandomSeed());
    
    // 准备数据
    std::vector<block> decoded_values(count);
    block w = SimplePRF::randomW(prng);
    
    for (size_t i = 0; i < count; i++) {
        decoded_values[i] = prng.get<block>();
    }
    
    // 多次运行取平均
    int iterations = 1000;
    auto start = high_resolution_clock::now();
    
    block result = ZeroBlock;
    for (int iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < count; i++) {
            result = result ^ SimplePRF::eval(decoded_values[i], w);
        }
    }
    // 防止优化掉
    if (result == ZeroBlock) std::cout << "";
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count() / 1000.0 / iterations;
}

// 测试同态PRF性能
double testHomomorphicPRF(size_t count) {
    PRNG prng(sysRandomSeed());
    
    // 准备数据
    std::vector<block> inputs(count);
    unsigned char k[crypto_core_ristretto255_SCALARBYTES];
    HomomorphicPRF::randomKey(k);
    
    for (size_t i = 0; i < count; i++) {
        inputs[i] = prng.get<block>();
    }
    
    // 计时
    auto start = high_resolution_clock::now();
    
    unsigned char result[crypto_core_ristretto255_BYTES];
    for (size_t i = 0; i < count; i++) {
        HomomorphicPRF::eval(result, k, inputs[i]);
    }
    
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start).count() / 1000.0;
}

// 测试单次操作耗时
void testSingleOperation() {
    PRNG prng(sysRandomSeed());
    
    // SimplePRF单次操作
    block decoded = prng.get<block>();
    block w = prng.get<block>();
    
    auto start1 = high_resolution_clock::now();
    block r;
    for (int i = 0; i < 100000; i++) {
        r = SimplePRF::eval(decoded, w);
    }
    if (r == ZeroBlock) std::cout << "";
    auto end1 = high_resolution_clock::now();
    double simple_ns = duration_cast<nanoseconds>(end1 - start1).count() / 100000.0;
    
    // HomomorphicPRF单次操作
    unsigned char k[crypto_core_ristretto255_SCALARBYTES];
    HomomorphicPRF::randomKey(k);
    block x = prng.get<block>();
    
    auto start2 = high_resolution_clock::now();
    unsigned char result[crypto_core_ristretto255_BYTES];
    for (int i = 0; i < 10000; i++) {
        HomomorphicPRF::eval(result, k, x);
    }
    auto end2 = high_resolution_clock::now();
    double homo_ns = duration_cast<nanoseconds>(end2 - start2).count() / 10000.0;
    
    std::cout << "Single Operation Time:" << std::endl;
    std::cout << "  SimplePRF (XOR):           " << std::fixed << std::setprecision(1) 
              << simple_ns << " ns" << std::endl;
    std::cout << "  HomomorphicPRF (EC mult):  " << std::fixed << std::setprecision(1) 
              << homo_ns << " ns" << std::endl;
    std::cout << "  Speedup:                   " << std::fixed << std::setprecision(0) 
              << homo_ns / simple_ns << "x" << std::endl;
    std::cout << std::endl;
}

void runComparison() {
    // 初始化libsodium
    if (sodium_init() < 0) {
        std::cerr << "Failed to initialize libsodium" << std::endl;
        return;
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "SimplePRF vs HomomorphicPRF Performance" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    std::cout << "SimplePRF:      F(x) = Decode(K, x) XOR w" << std::endl;
    std::cout << "                (论文简化版本，直接用解码值)" << std::endl;
    std::cout << std::endl;
    std::cout << "HomomorphicPRF: Hh(k, x) = k * H(x)" << std::endl;
    std::cout << "                (椭圆曲线标量乘法)" << std::endl;
    std::cout << std::endl;
    
    // 单次操作测试
    testSingleOperation();
    
    // 批量测试
    std::vector<size_t> counts = {1000, 4096, 10000, 65536};
    
    std::cout << std::setw(10) << "Count" 
              << std::setw(18) << "SimplePRF (ms)"
              << std::setw(18) << "HomoPRF (ms)"
              << std::setw(12) << "Speedup" << std::endl;
    std::cout << std::string(58, '-') << std::endl;
    
    for (size_t count : counts) {
        double simple_time = testSimplePRF(count);
        double homo_time = testHomomorphicPRF(count);
        double speedup = homo_time / simple_time;
        
        std::cout << std::setw(10) << count
                  << std::setw(18) << std::fixed << std::setprecision(3) << simple_time
                  << std::setw(18) << std::fixed << std::setprecision(3) << homo_time
                  << std::setw(10) << std::fixed << std::setprecision(0) << speedup << "x" << std::endl;
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "结论:" << std::endl;
    std::cout << "  - SimplePRF 只使用XOR运算" << std::endl;
    std::cout << "  - HomomorphicPRF 使用椭圆曲线标量乘法" << std::endl;
    std::cout << "  - SimplePRF 快很多，但安全性较低" << std::endl;
    std::cout << "  - 同态PRF是协议安全性的基础" << std::endl;
    std::cout << "========================================" << std::endl;
}

int main() {
    runComparison();
    return 0;
}
