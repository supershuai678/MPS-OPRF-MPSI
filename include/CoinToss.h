#pragma once
// 抛硬币协议: 两方安全生成共同随机数

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include <coproto/Socket/Socket.h>

namespace mpsoprf {

using namespace osuCrypto;

class CoinToss {
public:
    // 承诺: Commit(v, r) = H(v || r)
    static block commit(const block& value, const block& randomness);
    
    // 验证承诺
    static bool verify(const block& commitment, const block& value, const block& randomness);
    
    // 执行两方抛硬币协议
    // isInitiator: true=发起方(先发送承诺), false=响应方
    // 返回: 双方获得相同的随机数 w
    static macoro::task<block> execute(
        coproto::Socket& channel,
        PRNG& prng,
        bool isInitiator
    );
};

} // namespace mpsoprf
