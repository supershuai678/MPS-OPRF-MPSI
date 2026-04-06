// 抛硬币协议实现
// 两方安全生成共同随机数

#include "CoinToss.h"

namespace mpsoprf {

block CoinToss::commit(const block& value, const block& randomness) {
    // Commit(v, r) = H(v || r)
    RandomOracle ro(sizeof(block));
    ro.Update(value);
    ro.Update(randomness);
    block commitment;
    ro.Final(commitment);
    return commitment;
}

bool CoinToss::verify(const block& commitment, const block& value, const block& randomness) {
    block expected = commit(value, randomness);
    return commitment == expected;
}

macoro::task<block> CoinToss::execute(
    coproto::Socket& channel,
    PRNG& prng,
    bool isInitiator
) {
    block myValue = prng.get<block>();
    block myRandomness = prng.get<block>();
    block otherValue;
    
    if (isInitiator) {
        // 发起方流程:
        // 1. 发送承诺
        block myCommitment = commit(myValue, myRandomness);
        co_await channel.send(myCommitment);
        
        // 2. 接收对方的值
        co_await channel.recv(otherValue);
        
        // 3. 发送打开信息 (value, randomness)
        co_await channel.send(myValue);
        co_await channel.send(myRandomness);
    } else {
        // 响应方流程:
        // 1. 接收承诺
        block otherCommitment;
        co_await channel.recv(otherCommitment);
        
        // 2. 发送自己的值
        co_await channel.send(myValue);
        
        // 3. 接收打开信息
        block otherRandomness;
        co_await channel.recv(otherValue);
        co_await channel.recv(otherRandomness);
        
        // 4. 验证承诺
        if (!verify(otherCommitment, otherValue, otherRandomness)) {
            throw std::runtime_error("CoinToss: commitment verification failed");
        }
    }
    
    // 双方计算相同的结果: w = myValue XOR otherValue
    block result = myValue ^ otherValue;
    co_return result;
}

} // namespace mpsoprf
