#pragma once
// vOLE封装类
// 封装libOTe的Silent vOLE实现
// 
// 根据实验文档第3.1节:
// vOLE关系: C = A' + Δ·B
// - 发送方获得 (A', C)
// - 接收方获得 (Δ, B)

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/Socket.h>
#include <vector>

namespace mpsoprf {

using namespace osuCrypto;

// vOLE输出结构
struct VoleOutput {
    std::vector<block> values;  // 发送方: a 或 c; 接收方: b 或 d
};

// vOLE封装类
class VoleWrapper {
public:
    // 执行vOLE协议 - 发送方
    // 输出: (a, c) 其中 d = a * b + c
    // a: 随机向量
    // c: 相关向量
    static macoro::task<std::pair<std::vector<block>, std::vector<block>>> 
    runAsSender(
        coproto::Socket& channel,
        size_t numOTs,
        PRNG& prng
    );
    
    // 执行vOLE协议 - 接收方
    // 输入: b (选择向量)
    // 输出: d 其中 d = a * b + c
    static macoro::task<std::vector<block>>
    runAsReceiver(
        coproto::Socket& channel,
        const std::vector<block>& b,
        PRNG& prng
    );
    
    // 简化版本：发送方获得 (a, c)，接收方获得 (b, d)
    // 满足 d[i] = a[i] * b[i] + c[i] (在GF(2^128)上)
    static macoro::task<void> runVole(
        coproto::Socket& channel,
        bool isSender,
        size_t numOTs,
        std::vector<block>& out1,  // 发送方: a, 接收方: b
        std::vector<block>& out2,  // 发送方: c, 接收方: d
        PRNG& prng
    );
    
    // ========================================
    // MPS-OPRF协议专用接口
    // 根据文档: C = A' + Δ·B
    // ========================================
    
    // vOLE发送方（主导发送方Sn）
    // 输出: A' 和 C
    static macoro::task<void> runVoleSender(
        coproto::Socket& channel,
        size_t numOTs,
        std::vector<block>& A_prime,  // 输出: A'
        std::vector<block>& C,        // 输出: C
        PRNG& prng
    );
    
    // vOLE接收方（接收方R）
    // 输出: Δ 和 B
    static macoro::task<void> runVoleReceiver(
        coproto::Socket& channel,
        size_t numOTs,
        block& delta,                 // 输出: Δ
        std::vector<block>& B,        // 输出: B
        PRNG& prng
    );
};

} // namespace mpsoprf
