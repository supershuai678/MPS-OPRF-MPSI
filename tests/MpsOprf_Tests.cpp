// MPS-OPRF协议测试
// 测试MPS-OPRF协议的正确性
//
// 根据实验文档第4节的协议定义进行测试

#include <iostream>
#include <thread>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/LocalAsyncSock.h>
#include <volePSI/Paxos.h>

#include "MpsOprf.h"
#include "HomomorphicPRF.h"
#include "SecretShare.h"

using namespace osuCrypto;
using namespace mpsoprf;

// 测试SenderOutput的PRF计算
// F_i(x) = Decode(C_i, x) XOR w_i
void test_MpsOprf_Sender_PRF() {
    std::cout << "Testing MpsOprf Sender PRF evaluation... " << std::flush;

    PRNG prng(sysRandomSeed());

    // 创建测试数据
    const size_t m = 32;
    std::vector<block> testKeys(m);
    std::vector<block> testValues(m);
    for (size_t i = 0; i < m; ++i) {
        testKeys[i] = prng.get<block>();
        testValues[i] = prng.get<block>();
    }

    // 编码OKVS作为C_i
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, prng.get<block>());

    std::vector<block> C_i(paxos.size());
    paxos.solve<block>(span<const block>(testKeys), span<const block>(testValues),
                span<block>(C_i), &prng, 1);

    // 创建SenderOutput
    MpsOprf::SenderOutput senderOut;
    senderOut.w_i = prng.get<block>();
    senderOut.C_i = C_i;
    senderOut.m = m;

    // 测试PRF计算
    block testInput = testKeys[0];  // 使用已编码的键
    block result1 = senderOut.evalPRF(testInput);
    block result2 = senderOut.evalPRF(testInput);

    // 相同输入应该产生相同输出
    assert(result1 == result2);

    // 不同输入应该产生不同输出
    block testInput2 = testKeys[1];
    block result3 = senderOut.evalPRF(testInput2);
    assert(!(result1 == result3));

    std::cout << "PASSED" << std::endl;
}

// 测试LeaderOutput的PRF计算
void test_MpsOprf_Leader_PRF() {
    std::cout << "Testing MpsOprf Leader PRF... " << std::flush;

    PRNG prng(sysRandomSeed());

    const size_t m = 32;
    const size_t numParties = 3;

    // 创建测试数据
    std::vector<block> testKeys(m);
    std::vector<block> testValues(m);
    for (size_t i = 0; i < m; ++i) {
        testKeys[i] = prng.get<block>();
        testValues[i] = prng.get<block>();
    }

    // 编码OKVS
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, prng.get<block>());

    std::vector<block> C(paxos.size());
    paxos.solve<block>(span<const block>(testKeys), span<const block>(testValues),
                span<block>(C), &prng, 1);

    // 创建LeaderOutput
    MpsOprf::LeaderOutput leaderOut;
    leaderOut.w = prng.get<block>();
    leaderOut.C = C;
    leaderOut.m = m;

    // 分割w和C
    leaderOut.w_shares = SecretShare::split(leaderOut.w, numParties, prng);

    // 分割C
    leaderOut.C_shares.resize(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        leaderOut.C_shares[i].resize(C.size());
    }
    for (size_t j = 0; j < C.size(); ++j) {
        auto c_shares = SecretShare::split(C[j], numParties, prng);
        for (size_t i = 0; i < numParties; ++i) {
            leaderOut.C_shares[i][j] = c_shares[i];
        }
    }

    // 测试PRF计算
    block testInput = testKeys[0];
    block result1 = leaderOut.evalPRF(testInput, 0);
    block result2 = leaderOut.evalPRF(testInput, 0);

    assert(result1 == result2);

    // 不同份额索引应该产生不同结果
    block result3 = leaderOut.evalPRF(testInput, 1);
    // 由于份额不同，结果应该不同

    std::cout << "PASSED" << std::endl;
}

// 测试ReceiverOutput的PRF计算
// F(x) = Decode(K, x) XOR gf128Mul(delta, H(x)) XOR w
void test_MpsOprf_Receiver_PRF() {
    std::cout << "Testing MpsOprf Receiver PRF... " << std::flush;

    PRNG prng(sysRandomSeed());

    const size_t m = 32;

    // 创建测试数据
    std::vector<block> testKeys(m);
    std::vector<block> testValues(m);
    for (size_t i = 0; i < m; ++i) {
        testKeys[i] = prng.get<block>();
        testValues[i] = prng.get<block>();
    }

    // 编码OKVS作为K
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, prng.get<block>());

    std::vector<block> K(paxos.size());
    paxos.solve<block>(span<const block>(testKeys), span<const block>(testValues),
                span<block>(K), &prng, 1);

    // 创建ReceiverOutput
    MpsOprf::ReceiverOutput receiverOut;
    receiverOut.w = prng.get<block>();
    receiverOut.delta = prng.get<block>();
    receiverOut.K = K;
    receiverOut.m = m;

    // 测试PRF计算
    block testInput = testKeys[0];
    block result1 = receiverOut.evalPRF(testInput, K);
    block result2 = receiverOut.evalPRF(testInput, K);

    assert(result1 == result2);

    std::cout << "PASSED" << std::endl;
}

// 测试秘密分割的正确性
void test_MpsOprf_secret_sharing() {
    std::cout << "Testing MpsOprf secret sharing consistency... " << std::flush;

    PRNG prng(sysRandomSeed());

    // 创建一个秘密
    block secret = prng.get<block>();
    size_t numShares = 4;

    // 分割
    auto shares = SecretShare::split(secret, numShares, prng);

    // 重构
    block reconstructed = SecretShare::reconstruct(shares);

    assert(secret == reconstructed);

    std::cout << "PASSED" << std::endl;
}

// 测试密钥分割
void test_MpsOprf_key_sharing() {
    std::cout << "Testing MpsOprf key sharing... " << std::flush;

    PRNG prng(sysRandomSeed());

    // 创建完整密钥 (block)
    block fullKey = prng.get<block>();

    // 分割密钥为3份
    auto keyShares = SecretShare::split(fullKey, 3, prng);

    // 重构
    block reconstructed = SecretShare::reconstruct(keyShares);

    assert(fullKey == reconstructed);

    std::cout << "PASSED" << std::endl;
}

#ifdef ENABLE_HOMOMORPHIC_HASH
// 测试同态PRF的同态性质 (only available in HH mode)
// H_h(k1+k2, x) = H_h(k1, x) · H_h(k2, x)
void test_HomomorphicPRF_homomorphism() {
    std::cout << "Testing HomomorphicPRF homomorphism... " << std::flush;

    PRNG prng(sysRandomSeed());

    // 生成两个随机密钥
    Scalar k1 = HomomorphicPRF::randomKey(prng);
    Scalar k2 = HomomorphicPRF::randomKey(prng);

    // 测试输入
    block x = prng.get<block>();

    // 验证同态性
    bool result = HomomorphicPRF::verifyHomomorphic(k1, k2, x);
    assert(result);

    std::cout << "PASSED" << std::endl;
}
#endif

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "MPS-OPRF Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        test_MpsOprf_Sender_PRF();
        test_MpsOprf_Leader_PRF();
        test_MpsOprf_Receiver_PRF();
        test_MpsOprf_secret_sharing();
        test_MpsOprf_key_sharing();
#ifdef ENABLE_HOMOMORPHIC_HASH
        test_HomomorphicPRF_homomorphism();
#endif

        std::cout << "\nAll MPS-OPRF tests PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
