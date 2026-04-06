// MPS-OPRF-MPSI 单元测试
// 测试基础密码学原语

#include <iostream>
#include <cassert>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>

#include "HomomorphicPRF.h"
#include "SecretShare.h"

using namespace osuCrypto;
using namespace mpsoprf;

// 测试计数器
int testsPassed = 0;
int testsFailed = 0;

#define TEST(name) \
    std::cout << "Testing: " << #name << "... "; \
    try { test_##name(); testsPassed++; std::cout << "PASSED" << std::endl; } \
    catch (const std::exception& e) { testsFailed++; std::cout << "FAILED: " << e.what() << std::endl; }

// ============== SecretShare 测试 ==============

void test_SecretShare_split_reconstruct() {
    PRNG prng(sysRandomSeed());
    
    block secret = prng.get<block>();
    
    // 测试不同份额数量
    for (size_t n = 1; n <= 10; ++n) {
        auto shares = SecretShare::split(secret, n, prng);
        assert(shares.size() == n);
        
        block reconstructed = SecretShare::reconstruct(shares);
        assert(reconstructed == secret);
    }
}

void test_SecretShare_randomness() {
    PRNG prng(sysRandomSeed());
    
    block secret = prng.get<block>();
    
    // 分割两次，份额应该不同
    auto shares1 = SecretShare::split(secret, 3, prng);
    auto shares2 = SecretShare::split(secret, 3, prng);
    
    // 至少有一个份额不同
    bool different = false;
    for (size_t i = 0; i < 3; ++i) {
        if (shares1[i] != shares2[i]) {
            different = true;
            break;
        }
    }
    assert(different);
    
    // 但重构结果相同
    assert(SecretShare::reconstruct(shares1) == secret);
    assert(SecretShare::reconstruct(shares2) == secret);
}

// ============== HomomorphicPRF 测试 ==============

void test_HomomorphicPRF_hashToGroup() {
    block x1 = toBlock(1);
    block x2 = toBlock(2);
    
    auto h1 = HomomorphicPRF::hashToGroup(x1);
    auto h2 = HomomorphicPRF::hashToGroup(x2);
    
    // 不同输入应该产生不同输出
    assert(!HomomorphicPRF::equalPoints(h1, h2));
    
    // 相同输入应该产生相同输出
    auto h1_again = HomomorphicPRF::hashToGroup(x1);
    assert(HomomorphicPRF::equalPoints(h1, h1_again));
}

void test_HomomorphicPRF_eval() {
    PRNG prng(sysRandomSeed());
    
    auto k = HomomorphicPRF::randomKey(prng);
    block x = prng.get<block>();
    
    auto result1 = HomomorphicPRF::eval(k, x);
    auto result2 = HomomorphicPRF::eval(k, x);
    
    // 相同输入应该产生相同输出
    assert(HomomorphicPRF::equalPoints(result1, result2));
}

void test_HomomorphicPRF_different_keys() {
    PRNG prng(sysRandomSeed());
    
    auto k1 = HomomorphicPRF::randomKey(prng);
    auto k2 = HomomorphicPRF::randomKey(prng);
    block x = prng.get<block>();
    
    auto result1 = HomomorphicPRF::eval(k1, x);
    auto result2 = HomomorphicPRF::eval(k2, x);
    
    // 不同密钥应该产生不同输出
    assert(!HomomorphicPRF::equalPoints(result1, result2));
}

#ifdef ENABLE_SODIUM
void test_HomomorphicPRF_homomorphic_property() {
    PRNG prng(sysRandomSeed());
    
    auto k1 = HomomorphicPRF::randomKey(prng);
    auto k2 = HomomorphicPRF::randomKey(prng);
    block x = prng.get<block>();
    
    // 验证同态性: Hh(k1+k2, x) = Hh(k1, x) * Hh(k2, x)
    assert(HomomorphicPRF::verifyHomomorphic(k1, k2, x));
}

void test_HomomorphicPRF_key_addition() {
    PRNG prng(sysRandomSeed());
    
    auto k1 = HomomorphicPRF::randomKey(prng);
    auto k2 = HomomorphicPRF::randomKey(prng);
    
    auto k_sum = HomomorphicPRF::addKeys(k1, k2);
    
    block x = prng.get<block>();
    
    // Hh(k1+k2, x)
    auto left = HomomorphicPRF::eval(k_sum, x);
    
    // Hh(k1, x) * Hh(k2, x)
    auto h1 = HomomorphicPRF::eval(k1, x);
    auto h2 = HomomorphicPRF::eval(k2, x);
    auto right = HomomorphicPRF::mulPoints(h1, h2);
    
    assert(HomomorphicPRF::equalPoints(left, right));
}
#endif

// ============== 主函数 ==============

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "MPS-OPRF-MPSI Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // SecretShare 测试
    std::cout << "\n--- SecretShare Tests ---" << std::endl;
    TEST(SecretShare_split_reconstruct);
    TEST(SecretShare_randomness);
    
    // HomomorphicPRF 测试
    std::cout << "\n--- HomomorphicPRF Tests ---" << std::endl;
    TEST(HomomorphicPRF_hashToGroup);
    TEST(HomomorphicPRF_eval);
    TEST(HomomorphicPRF_different_keys);
    
#ifdef ENABLE_SODIUM
    std::cout << "\n--- HomomorphicPRF Homomorphic Property Tests (with libsodium) ---" << std::endl;
    TEST(HomomorphicPRF_homomorphic_property);
    TEST(HomomorphicPRF_key_addition);
#else
    std::cout << "\n[INFO] libsodium not enabled, skipping homomorphic property tests" << std::endl;
#endif
    
    // 总结
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << testsPassed << " passed, " << testsFailed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return testsFailed > 0 ? 1 : 0;
}
