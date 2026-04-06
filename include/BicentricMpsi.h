#pragma once
// 双中心结构MPSI协议
// 基于MPS-OPRF实现多方隐私集合交集

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/Socket.h>
#include <vector>

#include "MpsOprf.h"
#include "HomomorphicPRF.h"
#include "RandomOracle.h"

namespace mpsoprf {

using namespace osuCrypto;

// 双中心MPSI协议类
class BicentricMpsi {
public:
    struct ReceiverMetrics {
        std::vector<block> intersection;
        double activePhaseSeconds = 0.0;
    };

    struct SenderMetrics {
        double clientPhaseSeconds = 0.0;
    };

    // 是否启用安全扩展（随机预言机）
    // AC-5.1: 使用随机预言机RO替换直接输入
    static bool enableSecurityExtension;
    
    // 接收方执行，返回交集
    // senderChannels: 与所有发送方的通信通道
    // Y: 接收方的集合
    static macoro::task<ReceiverMetrics> runAsReceiver(
        coproto::Socket& leaderChannel,
        const std::vector<coproto::Socket*>& senderChannels,
        const std::vector<block>& Y,
        PRNG& prng
    );
    
    // 主导发送方执行
    // receiverChannel: 与接收方的通信通道
    // senderChannels: 与其他发送方的通信通道
    // X_n: 主导发送方的集合
    static macoro::task<void> runAsLeader(
        coproto::Socket& receiverChannel,
        const std::vector<coproto::Socket*>& senderChannels,
        const std::vector<block>& X_n,
        PRNG& prng
    );
    
    // 普通发送方执行
    // receiverChannel: 与接收方的通信通道
    // leaderChannel: 与主导发送方的通信通道
    // X_i: 发送方的集合
    static macoro::task<SenderMetrics> runAsSender(
        coproto::Socket& receiverChannel,
        coproto::Socket& leaderChannel,
        const std::vector<block>& X_i,
        PRNG& prng
    );
    
private:
    // 预处理输入（如果启用安全扩展）
    static std::vector<block> preprocessInputs(const std::vector<block>& inputs);
};

} // namespace mpsoprf
