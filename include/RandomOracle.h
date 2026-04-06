#pragma once
// 随机预言机 (Random Oracle)
// 用于抵御伪造攻击的安全扩展
// AC-5.1: 使用随机预言机RO替换直接输入

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include <vector>

namespace mpsoprf {

using namespace osuCrypto;

// 随机预言机类
// 实现 RO: {0,1}* → {0,1}^λ
class SecureRandomOracle {
public:
    // 安全参数 λ = 128 bits = 16 bytes
    static constexpr size_t OUTPUT_SIZE = 16;
    
    // 计算随机预言机输出
    // RO(x) → {0,1}^λ
    static block hash(const block& input) {
        RandomOracle ro(OUTPUT_SIZE);
        ro.Update(input);
        block output;
        ro.Final(output);
        return output;
    }
    
    // 带域分离的哈希
    // RO(domain || x) → {0,1}^λ
    static block hashWithDomain(const std::string& domain, const block& input) {
        RandomOracle ro(OUTPUT_SIZE);
        ro.Update((const u8*)domain.data(), domain.size());
        ro.Update(input);
        block output;
        ro.Final(output);
        return output;
    }
    
    // 批量哈希
    static std::vector<block> hashBatch(const std::vector<block>& inputs) {
        std::vector<block> outputs(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            outputs[i] = hash(inputs[i]);
        }
        return outputs;
    }
    
    // 用于MPSI的输入预处理
    // 将原始输入 x 转换为 RO(x)
    // 这样模拟器可以通过RO提取客户端输入 (AC-5.2)
    static block preprocessInput(const block& x) {
        return hashWithDomain("MPSI_INPUT", x);
    }
    
    // 批量预处理
    static std::vector<block> preprocessInputs(const std::vector<block>& inputs) {
        std::vector<block> outputs(inputs.size());
        for (size_t i = 0; i < inputs.size(); ++i) {
            outputs[i] = preprocessInput(inputs[i]);
        }
        return outputs;
    }
};

} // namespace mpsoprf
