// 简化伪随机函数实现
// F(x) = Decode(K, x) ⊕ w
// 不使用椭圆曲线运算，直接用block异或

#include "SimplePRF.h"

namespace mpsoprf {

// 简化PRF: F(x) = Decode(K, x) ⊕ w
// 使用XOR作为"加法"运算
block SimplePRF::eval(const block& decoded_value, const block& w) {
    return decoded_value ^ w;
}

// 生成随机w
block SimplePRF::randomW(PRNG& prng) {
    return prng.get<block>();
}

// 秘密分享w为n份: w = w1 ^ w2 ^ ... ^ wn
std::vector<block> SimplePRF::shareW(const block& w, size_t n, PRNG& prng) {
    std::vector<block> shares(n);
    block sum = ZeroBlock;
    
    // 生成n-1个随机份额
    for (size_t i = 0; i < n - 1; i++) {
        shares[i] = prng.get<block>();
        sum = sum ^ shares[i];
    }
    
    // 最后一个份额使得 w1 ^ w2 ^ ... ^ wn = w
    shares[n - 1] = sum ^ w;
    
    return shares;
}

// 合并w份额: w = w1 ^ w2 ^ ... ^ wn
block SimplePRF::combineShares(const std::vector<block>& shares) {
    block result = ZeroBlock;
    for (const auto& share : shares) {
        result = result ^ share;
    }
    return result;
}

// Fi(x) = Decode(Ci, x) ⊕ wi
block SimplePRF::evalShare(const block& decoded_value, const block& wi) {
    return decoded_value ^ wi;
}

} // namespace mpsoprf
