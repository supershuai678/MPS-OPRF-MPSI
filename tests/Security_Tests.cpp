// 安全扩展测试
// 测试US-5: 抵御伪造攻击的安全扩展

#include <iostream>
#include <cassert>
#include <set>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>

#include "RandomOracle.h"
#include "BicentricMpsi.h"
#include "RingMpsi.h"

using namespace osuCrypto;
using namespace mpsoprf;

// 测试随机预言机基本功能
void test_random_oracle_basic() {
    std::cout << "Testing Random Oracle basic... " << std::flush;
    
    block input1 = toBlock(12345);
    block input2 = toBlock(67890);
    
    // 相同输入应该产生相同输出
    block output1a = SecureRandomOracle::hash(input1);
    block output1b = SecureRandomOracle::hash(input1);
    assert(output1a == output1b);
    
    // 不同输入应该产生不同输出
    block output2 = SecureRandomOracle::hash(input2);
    assert(output1a != output2);
    
    std::cout << "PASSED" << std::endl;
}

// 测试带域分离的哈希
void test_random_oracle_domain() {
    std::cout << "Testing Random Oracle with domain separation... " << std::flush;
    
    block input = toBlock(12345);
    
    // 不同域应该产生不同输出
    block output1 = SecureRandomOracle::hashWithDomain("DOMAIN1", input);
    block output2 = SecureRandomOracle::hashWithDomain("DOMAIN2", input);
    assert(output1 != output2);
    
    // 相同域相同输入应该产生相同输出
    block output1b = SecureRandomOracle::hashWithDomain("DOMAIN1", input);
    assert(output1 == output1b);
    
    std::cout << "PASSED" << std::endl;
}

// 测试输入预处理
void test_preprocess_inputs() {
    std::cout << "Testing input preprocessing... " << std::flush;
    
    PRNG prng(sysRandomSeed());
    std::vector<block> inputs(100);
    for (size_t i = 0; i < inputs.size(); ++i) {
        inputs[i] = prng.get<block>();
    }
    
    // 预处理输入
    auto processed = SecureRandomOracle::preprocessInputs(inputs);
    
    // 检查大小相同
    assert(processed.size() == inputs.size());
    
    // 检查预处理后的值不同于原始值
    for (size_t i = 0; i < inputs.size(); ++i) {
        assert(processed[i] != inputs[i]);
    }
    
    // 检查预处理是确定性的
    auto processed2 = SecureRandomOracle::preprocessInputs(inputs);
    for (size_t i = 0; i < inputs.size(); ++i) {
        assert(processed[i] == processed2[i]);
    }
    
    std::cout << "PASSED" << std::endl;
}

// 测试安全扩展开关
void test_security_extension_toggle() {
    std::cout << "Testing security extension toggle... " << std::flush;
    
    // 默认应该启用
    assert(BicentricMpsi::enableSecurityExtension == true);
    assert(RingMpsi::enableSecurityExtension == true);
    
    // 可以禁用
    BicentricMpsi::enableSecurityExtension = false;
    RingMpsi::enableSecurityExtension = false;
    
    assert(BicentricMpsi::enableSecurityExtension == false);
    assert(RingMpsi::enableSecurityExtension == false);
    
    // 恢复默认
    BicentricMpsi::enableSecurityExtension = true;
    RingMpsi::enableSecurityExtension = true;
    
    std::cout << "PASSED" << std::endl;
}

// 测试批量哈希
void test_batch_hash() {
    std::cout << "Testing batch hash... " << std::flush;
    
    PRNG prng(sysRandomSeed());
    std::vector<block> inputs(50);
    for (size_t i = 0; i < inputs.size(); ++i) {
        inputs[i] = prng.get<block>();
    }
    
    // 批量哈希
    auto outputs = SecureRandomOracle::hashBatch(inputs);
    
    // 检查大小
    assert(outputs.size() == inputs.size());
    
    // 检查每个输出与单独哈希一致
    for (size_t i = 0; i < inputs.size(); ++i) {
        block single = SecureRandomOracle::hash(inputs[i]);
        assert(outputs[i] == single);
    }
    
    std::cout << "PASSED" << std::endl;
}

// 测试抗碰撞性（统计测试）
void test_collision_resistance() {
    std::cout << "Testing collision resistance... " << std::flush;
    
    PRNG prng(sysRandomSeed());
    const size_t numTests = 1000;
    std::set<block> outputs;
    
    for (size_t i = 0; i < numTests; ++i) {
        block input = prng.get<block>();
        block output = SecureRandomOracle::hash(input);
        outputs.insert(output);
    }
    
    // 所有输出应该不同（极高概率）
    assert(outputs.size() == numTests);
    
    std::cout << "PASSED (" << numTests << " unique outputs)" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Security Extension Tests (US-5)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        test_random_oracle_basic();
        test_random_oracle_domain();
        test_preprocess_inputs();
        test_security_extension_toggle();
        test_batch_hash();
        test_collision_resistance();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "All Security Extension tests PASSED!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "\nAC-5.1: Random Oracle implementation ✓" << std::endl;
        std::cout << "AC-5.2: Input preprocessing for simulation ✓" << std::endl;
        std::cout << "AC-5.3: Security extension integrated ✓" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
