// Ring topology MPSI protocol
//
// Unified GF(2^128) mode:
//   Ring accumulation: b_j = F_i(x) XOR Decode(T_prev, x)
//   (XOR replaces group multiplication)
//
// When ENABLE_HOMOMORPHIC_HASH is defined:
//   EC overhead is injected inside evalPRF (see MpsOprf.cpp)
//   Protocol logic remains identical — GF128 guarantees correctness

#include "RingMpsi.h"
#include "RandomOracle.h"
#include <volePSI/Paxos.h>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace mpsoprf {

bool RingMpsi::enableSecurityExtension = true;

std::vector<block> RingMpsi::preprocessInputs(const std::vector<block>& inputs) {
    if (enableSecurityExtension)
        return SecureRandomOracle::preprocessInputs(inputs);
    return inputs;
}

// ============================================================================
// GF(2^128) mode
// ============================================================================

// Receiver: receive T_1 from S_1, compute intersection
macoro::task<RingMpsi::ReceiverMetrics> RingMpsi::runAsReceiver(
    coproto::Socket& prevChannel,
    coproto::Socket& leaderChannel,
    const std::vector<block>& Y,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    size_t m = Y.size();
    std::vector<block> Y_processed = preprocessInputs(Y);

    // Step 1: MPS-OPRF
    auto t0 = Clock::now();
    auto receiverOutput = co_await MpsOprf::runAsReceiver(leaderChannel, Y_processed, prng);
    auto t1 = Clock::now();

    // Step 2: Receive T_1 from S_1
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    std::vector<block> T_1(paxos.size());
    co_await prevChannel.recv(T_1);
    auto t2 = Clock::now();

    // Step 3: Compute intersection (batch operations)
    std::vector<block> intersection;

    // Batch decode K → F_R values
    std::vector<block> decoded_K(m);
    paxos.decode<block>(Y_processed, decoded_K, receiverOutput.K, 1);

    std::vector<block> F_R(m);
    for (size_t j = 0; j < m; ++j) {
        block hx = GF128PRF::hashToField(Y_processed[j]);
        F_R[j] = decoded_K[j] ^ receiverOutput.delta.gf128Mul(hx) ^ receiverOutput.w;
    }

    // Batch decode T_1
    std::vector<block> decoded_T1(m);
    paxos.decode<block>(Y_processed, decoded_T1, T_1, 1);

#ifdef ENABLE_HOMOMORPHIC_HASH
    for (size_t j = 0; j < m; ++j) {
        Point p_recv = HomomorphicPRF::eval(Scalar(F_R[j]), Y_processed[j]);
        Point p_decoded = HomomorphicPRF::eval(Scalar(decoded_T1[j]), Y_processed[j]);
        if (HomomorphicPRF::equalPoints(p_recv, p_decoded))
            intersection.push_back(Y[j]);
    }
#else
    for (size_t j = 0; j < m; ++j) {
        if (F_R[j] == decoded_T1[j])
            intersection.push_back(Y[j]);
    }
#endif
    auto t3 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[Ring-R] MPS-OPRF: " << std::fixed << std::setprecision(1) << ms(t0, t1)
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

// Leader S_n: MPS-OPRF + encode T_n, send to S_{n-1}
macoro::task<void> RingMpsi::runAsLeader(
    coproto::Socket& nextChannel,
    coproto::Socket& receiverChannel,
    const std::vector<coproto::Socket*>& senderChannels,
    const std::vector<block>& X_n,
    size_t numSenders,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
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

    // Send T_n to S_{n-1}
    co_await nextChannel.send(T_n);
    auto t3 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[Ring-L] MPS-OPRF: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Compute T: " << ms(t1, t2)
              << " ms | Send T: " << ms(t2, t3)
              << " ms | Total: " << ms(t0, t3) << " ms" << std::endl;
}

// First sender S_1: receive T_prev from S_2, compute T_1, send to R
// BUG-8 fix: prevChannel receives T_prev from S_2 (not from leaderChannel)
macoro::task<RingMpsi::SenderMetrics> RingMpsi::runAsFirstSender(
    coproto::Socket& leaderChannel,
    coproto::Socket& prevChannel,
    coproto::Socket& nextChannel,
    const std::vector<block>& X_1,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    size_t m = X_1.size();
    std::vector<block> X_1_processed = preprocessInputs(X_1);

    // Step 1: MPS-OPRF
    auto t0 = Clock::now();
    auto senderOutput = co_await MpsOprf::runAsSender(leaderChannel, prng);
    auto t1 = Clock::now();

    // Step 2: Receive T_prev from S_2 (via prevChannel)
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    std::vector<block> T_prev(paxos.size());
    co_await prevChannel.recv(T_prev);
    auto t2 = Clock::now();

    // Step 3: Batch decode C_i and T_prev, compute b_j = F_1(x_j) XOR Decode(T_prev, x_j)
    std::vector<block> decoded_Ci(m), decoded_Tprev(m);
    paxos.decode<block>(X_1_processed, decoded_Ci, senderOutput.C_i, 1);
    paxos.decode<block>(X_1_processed, decoded_Tprev, T_prev, 1);

    std::vector<block> values(m);
    for (size_t j = 0; j < m; ++j) {
        block f1_xj = decoded_Ci[j] ^ senderOutput.w_i;
#ifdef ENABLE_HOMOMORPHIC_HASH
        HomomorphicPRF::eval(Scalar(f1_xj), X_1_processed[j]);
#endif
        values[j] = f1_xj ^ decoded_Tprev[j];
    }

    // T_1 = Encode(RO(x_j^1), b_j)
    std::vector<block> T_1(paxos.size());
    paxos.solve<block>(X_1_processed, values, T_1, &prng, 1);
    auto t3 = Clock::now();

    // Send T_1 to R
    co_await nextChannel.send(T_1);
    auto t4 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[Ring-S1] MPS-OPRF: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Recv T: " << ms(t1, t2)
              << " ms | Compute: " << ms(t2, t3)
              << " ms | Send T: " << ms(t3, t4)
              << " ms | Total: " << ms(t0, t4) << " ms" << std::endl;

    SenderMetrics metrics;
    metrics.clientPhaseSeconds = std::chrono::duration<double>(t4 - t1).count();
    co_return metrics;
}

// Middle sender S_i (i in [2, n-1])
macoro::task<RingMpsi::SenderMetrics> RingMpsi::runAsSender(
    coproto::Socket& prevChannel,
    coproto::Socket& nextChannel,
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

    // Step 2: Receive T_{i+1} from prevChannel
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    std::vector<block> T_prev(paxos.size());
    co_await prevChannel.recv(T_prev);
    auto t2 = Clock::now();

    // Step 3: Batch decode C_i and T_prev, compute b_j = F_i(x_j) XOR Decode(T_prev, x_j)
    std::vector<block> decoded_Ci(m), decoded_Tprev(m);
    paxos.decode<block>(X_i_processed, decoded_Ci, senderOutput.C_i, 1);
    paxos.decode<block>(X_i_processed, decoded_Tprev, T_prev, 1);

    std::vector<block> values(m);
    for (size_t j = 0; j < m; ++j) {
        block fi_xj = decoded_Ci[j] ^ senderOutput.w_i;
#ifdef ENABLE_HOMOMORPHIC_HASH
        HomomorphicPRF::eval(Scalar(fi_xj), X_i_processed[j]);
#endif
        values[j] = fi_xj ^ decoded_Tprev[j];
    }

    // T_i = Encode(RO(x_j^i), b_j)
    std::vector<block> T_i(paxos.size());
    paxos.solve<block>(X_i_processed, values, T_i, &prng, 1);
    auto t3 = Clock::now();

    // Send T_i to S_{i-1}
    co_await nextChannel.send(T_i);
    auto t4 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[Ring-Si] MPS-OPRF: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Recv T: " << ms(t1, t2)
              << " ms | Compute: " << ms(t2, t3)
              << " ms | Send T: " << ms(t3, t4)
              << " ms | Total: " << ms(t0, t4) << " ms" << std::endl;

    SenderMetrics metrics;
    metrics.clientPhaseSeconds = std::chrono::duration<double>(t4 - t1).count();
    co_return metrics;
}

} // namespace mpsoprf
