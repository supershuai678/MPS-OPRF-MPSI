#pragma once
// 简化伪随机函数 F(x) = Decode(K, x) ⊕ w
// 不使用同态PRF，直接用OKVS解码值
// 用于性能对比测试

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <vector>

namespace mpsoprf {

using namespace osuCrypto;

// 简化PRF类 - 不使用椭圆曲线运算
class SimplePRF {
public:
    // 简化PRF: F(x) = Decode(K, x) ⊕ w
    // 直接返回解码值加上随机数w
    static block eval(const block& decoded_value, const block& w);
    
    // 生成随机w
    static block randomW(PRNG& prng);
    
    // 秘密分享w为n份
    static std::vector<block> shareW(const block& w, size_t n, PRNG& prng);
    
    // 合并w份额
    static block combineShares(const std::vector<block>& shares);
    
    // Fi(x) = Decode(Ci, x) ⊕ wi
    static block evalShare(const block& decoded_value, const block& wi);
};

} // namespace mpsoprf
