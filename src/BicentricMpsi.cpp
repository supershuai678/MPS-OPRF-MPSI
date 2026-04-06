// Bicentric (star) topology MPSI protocol
//
// Unified GF(2^128) mode:
//   T_i encodes PRF outputs as blocks
//   Accumulation uses XOR (GF128 addition)
//   Comparison uses block equality
//
// When ENABLE_HOMOMORPHIC_HASH is defined:
//   EC overhead is injected inside evalPRF (see MpsOprf.cpp)
//   Protocol logic remains identical — GF128 guarantees correctness

#include "BicentricMpsi.h"
#include "RandomOracle.h"
#include <volePSI/Paxos.h>
#include <thread>
#include <macoro/sync_wait.h>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace mpsoprf {

bool BicentricMpsi::enableSecurityExtension = true;

std::vector<block> BicentricMpsi::preprocessInputs(const std::vector<block>& inputs) {
    if (enableSecurityExtension)
        return SecureRandomOracle::preprocessInputs(inputs);
    return inputs;
}

// ============================================================================
// GF(2^128) mode
// ============================================================================

// Receiver: collect T_i from all senders, verify intersection
macoro::task<BicentricMpsi::ReceiverMetrics> BicentricMpsi::runAsReceiver(
    coproto::Socket& leaderChannel,
    const std::vector<coproto::Socket*>& senderChannels,
    const std::vector<block>& Y,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    size_t numSenders = senderChannels.size() + 1; // +1 for leader
    size_t m = Y.size();

    std::vector<block> Y_processed = preprocessInputs(Y);

    // Step 1: MPS-OPRF
    auto t0 = Clock::now();
    auto receiverOutput = co_await MpsOprf::runAsReceiver(leaderChannel, Y_processed, prng);
    auto t1 = Clock::now();

    // Step 2: Receive T_i from all senders
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    u64 tSize = paxos.size();

    std::vector<std::vector<block>> T(numSenders);
    for (size_t i = 0; i < numSenders; ++i)
        T[i].resize(tSize);

    co_await leaderChannel.recv(T[numSenders - 1]);
    for (size_t i = 0; i < senderChannels.size(); ++i) {
        co_await senderChannels[i]->recv(T[i]);
    }
    auto t2 = Clock::now();

    // Step 3: Compute intersection (batch operations)
    std::vector<block> intersection;

    // Batch decode K → Receiver PRF: F_R(x) = Decode(K,x) XOR delta*H(x) XOR w
    std::vector<block> decoded_K(m);
    paxos.decode<block>(Y_processed, decoded_K, receiverOutput.K, 1);

    std::vector<block> F_R(m);
    for (size_t j = 0; j < m; ++j) {
        block hx = GF128PRF::hashToField(Y_processed[j]);
        F_R[j] = decoded_K[j] ^ receiverOutput.delta.gf128Mul(hx) ^ receiverOutput.w;
    }

    // XOR all T vectors then batch decode once (OKVS linearity)
    std::vector<block> T_merged(tSize, oc::ZeroBlock);
    for (size_t i = 0; i < numSenders; ++i)
        for (size_t k = 0; k < tSize; ++k)
            T_merged[k] ^= T[i][k];

    std::vector<block> accumulated(m);
    paxos.decode<block>(Y_processed, accumulated, T_merged, 1);

#ifdef ENABLE_HOMOMORPHIC_HASH
    for (size_t j = 0; j < m; ++j) {
        Point p_recv = HomomorphicPRF::eval(Scalar(F_R[j]), Y_processed[j]);
        Point p_accum = HomomorphicPRF::eval(Scalar(accumulated[j]), Y_processed[j]);
        if (HomomorphicPRF::equalPoints(p_recv, p_accum))
            intersection.push_back(Y[j]);
    }
#else
    for (size_t j = 0; j < m; ++j) {
        if (F_R[j] == accumulated[j])
            intersection.push_back(Y[j]);
    }
#endif
    auto t3 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[BC-R] MPS-OPRF: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Recv T: " << ms(t1, t2)
              << " ms | Compute: " << ms(t2, t3)
              << " ms | Total: " << ms(t0, t3) << " ms" << std::endl;

    ReceiverMetrics metrics;
    metrics.intersection = std::move(intersection);
    metrics.activePhaseSeconds =
        std::chrono::duration<double>(t1 - t0).count() +
        std::chrono::duration<double>(t3 - t2).count();

    co_return metrics;
}

// Leader: MPS-OPRF + encode T_n
macoro::task<void> BicentricMpsi::runAsLeader(
    coproto::Socket& receiverChannel,
    const std::vector<coproto::Socket*>& senderChannels,
    const std::vector<block>& X_n,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    size_t numSenders = senderChannels.size();
    size_t m = X_n.size();

    std::vector<block> X_n_processed = preprocessInputs(X_n);

    // Step 1: MPS-OPRF
    auto t0 = Clock::now();
    auto leaderOutput = co_await MpsOprf::runAsLeader(
        receiverChannel, senderChannels, X_n_processed, numSenders, prng);
    auto t1 = Clock::now();

    // Step 2: T_n = Encode(RO(x_j^n), F_n(RO(x_j^n)))
    // Leader uses share index 0
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);

    // Batch decode C_shares[0] then XOR w_shares[0]
    std::vector<block> values(m);
    paxos.decode<block>(X_n_processed, values, leaderOutput.C_shares[0], 1);
    for (size_t j = 0; j < m; ++j) {
        values[j] ^= leaderOutput.w_shares[0];
#ifdef ENABLE_HOMOMORPHIC_HASH
        HomomorphicPRF::eval(Scalar(values[j]), X_n_processed[j]);
#endif
    }

    std::vector<block> T_n(paxos.size());
    paxos.solve<block>(X_n_processed, values, T_n, &prng, 1);
    auto t2 = Clock::now();

    co_await receiverChannel.send(T_n);
    auto t3 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[BC-L] MPS-OPRF: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Compute T: " << ms(t1, t2)
              << " ms | Send T: " << ms(t2, t3)
              << " ms | Total: " << ms(t0, t3) << " ms" << std::endl;
}

// Sender: MPS-OPRF + encode T_i
macoro::task<BicentricMpsi::SenderMetrics> BicentricMpsi::runAsSender(
    coproto::Socket& receiverChannel,
    coproto::Socket& leaderChannel,
    const std::vector<block>& X_i,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    size_t m = X_i.size();

    std::vector<block> X_i_processed = preprocessInputs(X_i);

    // Step 1: MPS-OPRF
    auto t0 = Clock::now();
    auto senderOutput = co_await MpsOprf::runAsSender(leaderChannel, prng);
    auto t1 = Clock::now();

    // Step 2: T_i = Encode(RO(x_j^i), F_i(RO(x_j^i)))
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);

    // Batch decode C_i then XOR w_i
    std::vector<block> values(m);
    paxos.decode<block>(X_i_processed, values, senderOutput.C_i, 1);
    for (size_t j = 0; j < m; ++j) {
        values[j] ^= senderOutput.w_i;
#ifdef ENABLE_HOMOMORPHIC_HASH
        HomomorphicPRF::eval(Scalar(values[j]), X_i_processed[j]);
#endif
    }

    std::vector<block> T_i(paxos.size());
    paxos.solve<block>(X_i_processed, values, T_i, &prng, 1);
    auto t2 = Clock::now();

    co_await receiverChannel.send(T_i);
    auto t3 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[BC-S] MPS-OPRF: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Compute T: " << ms(t1, t2)
              << " ms | Send T: " << ms(t2, t3)
              << " ms | Total: " << ms(t0, t3) << " ms" << std::endl;

    SenderMetrics metrics;
    metrics.clientPhaseSeconds = std::chrono::duration<double>(t3 - t1).count();
    co_return metrics;
}

} // namespace mpsoprf
