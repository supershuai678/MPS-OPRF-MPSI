// MPS-OPRF protocol implementation
//
// Unified GF(2^128) mode:
//   PRF: F(x) = Decode(OKVS, x) XOR w
//   All operations use block (128-bit) with XOR addition and gf128Mul
//
// When ENABLE_HOMOMORPHIC_HASH is defined:
//   Each evalPRF additionally performs an EC scalar multiplication
//   (crypto_scalarmult_ristretto255) to simulate HH computation overhead.
//   The EC result is discarded — protocol correctness uses GF128 only.
//
// vOLE relation (libOTe SilentVole GF128):
//   libOTe Sender: holds (delta, B)
//   libOTe Receiver: holds (A, C) where C[i] = A[i] * delta XOR B[i]
//
//   Protocol mapping:
//   Leader = libOTe Receiver, gets (A', C)
//   R = libOTe Sender, gets (delta, B)

#include "MpsOprf.h"
#include "VoleWrapper.h"
#include <volePSI/Paxos.h>
#include <thread>
#include <macoro/sync_wait.h>
#include <iostream>
#include <iomanip>
#include <chrono>

namespace mpsoprf {

// ============================================================================
// GF(2^128) mode — PRF evaluation
// ============================================================================

// Receiver PRF: F_R(x) = Decode(K, x) XOR gf128Mul(delta, H(x)) XOR w
block MpsOprf::ReceiverOutput::evalPRF(const block& x, const std::vector<block>& K_okvs) const {
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);

    block decoded;
    paxos.decode<block>({&x, 1}, {&decoded, 1}, K_okvs, 1);

    // H(x) -> GF(2^128) field element
    block hx = GF128PRF::hashToField(x);

    // delta * H(x) in GF(2^128)
    block delta_hx = delta.gf128Mul(hx);

    // F_R(x) = Decode(K, x) XOR delta*H(x) XOR w
    // In GF(2^128): subtraction = addition = XOR
    block result = decoded ^ delta_hx ^ w;

#ifdef ENABLE_HOMOMORPHIC_HASH
    // EC overhead: scalar multiplication using PRF result and input x
    HomomorphicPRF::eval(Scalar(result), x);
#endif

    return result;
}

#ifdef ENABLE_HOMOMORPHIC_HASH
// Receiver PRF with homomorphic hash output: returns Point = H_G(x)^{F_R(x)}
Point MpsOprf::ReceiverOutput::evalPRF_HH(const block& x, const std::vector<block>& K_okvs) const {
    block result = evalPRF(x, K_okvs);
    return HomomorphicPRF::eval(Scalar(result), x);
}
#endif

// Sender PRF: F_i(x) = Decode(C_i, x) XOR w_i
block MpsOprf::SenderOutput::evalPRF(const block& x) const {
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);

    block decoded;
    paxos.decode<block>({&x, 1}, {&decoded, 1}, C_i, 1);

    block result = decoded ^ w_i;

#ifdef ENABLE_HOMOMORPHIC_HASH
    // EC overhead: scalar multiplication using PRF result and input x
    HomomorphicPRF::eval(Scalar(result), x);
#endif

    return result;
}

#ifdef ENABLE_HOMOMORPHIC_HASH
// Sender PRF with homomorphic hash output: returns Point = H_G(x)^{F_i(x)}
Point MpsOprf::SenderOutput::evalPRF_HH(const block& x) const {
    block result = evalPRF(x);
    return HomomorphicPRF::eval(Scalar(result), x);
}
#endif

// Leader PRF: F_leader(x) = Decode(C_shares[idx], x) XOR w_shares[idx]
block MpsOprf::LeaderOutput::evalPRF(const block& x, size_t myIndex) const {
    if (myIndex >= C_shares.size() || myIndex >= w_shares.size())
        return oc::ZeroBlock;

    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);

    block decoded;
    paxos.decode<block>({&x, 1}, {&decoded, 1}, C_shares[myIndex], 1);

    block result = decoded ^ w_shares[myIndex];

#ifdef ENABLE_HOMOMORPHIC_HASH
    // EC overhead: scalar multiplication using PRF result and input x
    HomomorphicPRF::eval(Scalar(result), x);
#endif

    return result;
}

#ifdef ENABLE_HOMOMORPHIC_HASH
// Leader PRF with homomorphic hash output: returns Point = H_G(x)^{F_leader(x)}
Point MpsOprf::LeaderOutput::evalPRF_HH(const block& x, size_t myIndex) const {
    block result = evalPRF(x, myIndex);
    return HomomorphicPRF::eval(Scalar(result), x);
}
#endif

// ============================================================================
// GF(2^128) mode — Protocol execution
// ============================================================================

// Receiver execution
macoro::task<MpsOprf::ReceiverOutput> MpsOprf::runAsReceiver(
    coproto::Socket& leaderChannel,
    const std::vector<block>& Y,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    ReceiverOutput output;
    size_t m = Y.size();
    output.m = m;

    // Step 1: Initialize paxos to get the required vOLE length
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    u64 paxosSize = paxos.size();

    // Step 1b: Receive paxosSize from leader for synchronization
    u64 leaderPaxosSize;
    co_await leaderChannel.recv(leaderPaxosSize);

    // Step 2: Execute vOLE with leader
    auto t0 = Clock::now();
    std::vector<block> B(paxosSize);
    co_await VoleWrapper::runVoleReceiver(leaderChannel, paxosSize, output.delta, B, prng);
    auto t1 = Clock::now();

    // Step 3: Receive A from leader (A = A' ⊕ P)
    std::vector<block> A(paxosSize);
    co_await leaderChannel.recv(A);
    auto t2 = Clock::now();

    // Step 4: K[i] = B[i] XOR gf128Mul(A[i], delta)
    output.K.resize(paxosSize);
    for (size_t i = 0; i < paxosSize; ++i) {
        output.K[i] = B[i] ^ A[i].gf128Mul(output.delta);
    }
    auto t3 = Clock::now();

    // Step 5: Coin toss -> w
    block w_block = co_await CoinToss::execute(leaderChannel, prng, false);
    output.w = w_block;
    auto t4 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[OPRF-R] vOLE: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Recv A: " << ms(t1, t2)
              << " ms | Compute K: " << ms(t2, t3)
              << " ms | CoinToss: " << ms(t3, t4)
              << " ms | Total: " << ms(t0, t4) << " ms" << std::endl;

    co_return output;
}

// Leader execution
macoro::task<MpsOprf::LeaderOutput> MpsOprf::runAsLeader(
    coproto::Socket& receiverChannel,
    const std::vector<coproto::Socket*>& senderChannels,
    const std::vector<block>& X_n,
    size_t numSenders,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    LeaderOutput output;
    size_t m = X_n.size();
    output.m = m;

    // Step 1: Initialize paxos to determine vOLE length
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    u64 paxosSize = paxos.size();

    // Step 1b: Send paxosSize to receiver for synchronization
    co_await receiverChannel.send(paxosSize);

    // Step 2: Execute vOLE with receiver
    auto t0 = Clock::now();
    std::vector<block> A_prime(paxosSize);
    output.C.resize(paxosSize);
    co_await VoleWrapper::runVoleSender(receiverChannel, paxosSize, A_prime, output.C, prng);
    auto t1 = Clock::now();

    // Step 3: OKVS encode — P = Encode(X_n, H(X_n))
    std::vector<block> h_values(m);
    for (size_t j = 0; j < m; ++j) {
        h_values[j] = GF128PRF::hashToField(X_n[j]);
    }

    std::vector<block> P(paxosSize);
    paxos.solve<block>(X_n, h_values, P, &prng, 1);

    // Step 4: A = A' XOR P (GF(2^128) addition = XOR)
    std::vector<block> A(paxosSize);
    for (size_t i = 0; i < paxosSize; ++i) {
        A[i] = A_prime[i] ^ P[i];
    }

    // Step 5: Send A to receiver
    co_await receiverChannel.send(A);
    auto t2 = Clock::now();

    // Step 6: Coin toss -> w
    block w_block = co_await CoinToss::execute(receiverChannel, prng, true);
    output.w = w_block;
    auto t3 = Clock::now();

    // Step 7: Generate per-sender seeds and expand shares via PRG
    // Instead of sending full C_i vectors over the network, send only a seed (16 bytes).
    // Each sender expands the seed locally using PRG to recover w_i and C_i.
    // Leader keeps the constrained share: C_shares[0] = C XOR all sender C_shares.
    size_t totalParties = numSenders + 1; // including leader

    std::vector<block> senderSeeds(numSenders);
    prng.get(senderSeeds.data(), senderSeeds.size());

    output.w_shares.resize(totalParties);
    output.C_shares.resize(totalParties);
    for (size_t i = 0; i < totalParties; ++i)
        output.C_shares[i].resize(paxosSize);

    // Expand sender shares from seeds
    for (size_t i = 0; i < numSenders; ++i) {
        PRNG seedPrng(senderSeeds[i]);
        output.w_shares[i + 1] = seedPrng.get<block>();
        seedPrng.get(output.C_shares[i + 1].data(), output.C_shares[i + 1].size());
    }

    // Leader's w_share = w XOR all sender w_shares
    block wXor = oc::ZeroBlock;
    for (size_t i = 0; i < numSenders; ++i)
        wXor = wXor ^ output.w_shares[i + 1];
    output.w_shares[0] = w_block ^ wXor;

    // Leader's C_share = C XOR all sender C_shares
    for (size_t j = 0; j < paxosSize; ++j) {
        block xorSum = oc::ZeroBlock;
        for (size_t i = 0; i < numSenders; ++i)
            xorSum = xorSum ^ output.C_shares[i + 1][j];
        output.C_shares[0][j] = output.C[j] ^ xorSum;
    }
    auto t4 = Clock::now();

    // Step 8: Distribute seeds (not full shares) to senders
    for (size_t i = 0; i < numSenders; ++i) {
        co_await senderChannels[i]->send(senderSeeds[i]);
        co_await senderChannels[i]->send(m);
    }
    auto t5 = Clock::now();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[OPRF-L] vOLE: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | Encode+SendA: " << ms(t1, t2)
              << " ms | CoinToss: " << ms(t2, t3)
              << " ms | SecretShare: " << ms(t3, t4)
              << " ms | Distribute: " << ms(t4, t5)
              << " ms | Total: " << ms(t0, t5) << " ms" << std::endl;

    co_return output;
}

// Sender execution
macoro::task<MpsOprf::SenderOutput> MpsOprf::runAsSender(
    coproto::Socket& leaderChannel,
    PRNG& prng
) {
    using Clock = std::chrono::high_resolution_clock;
    SenderOutput output;

    // Receive seed and m from leader, then expand shares locally via PRG
    auto t0 = Clock::now();
    block seed;
    co_await leaderChannel.recv(seed);
    co_await leaderChannel.recv(output.m);
    auto t1 = Clock::now();

    // Expand w_i and C_i from seed (same PRG sequence as leader)
    PRNG seedPrng(seed);
    output.w_i = seedPrng.get<block>();
    volePSI::Baxos paxosTmp;
    paxosTmp.init(output.m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    output.C_i.resize(paxosTmp.size());
    seedPrng.get(output.C_i.data(), output.C_i.size());
    auto t2 = Clock::now();
    output.activeSeconds = std::chrono::duration<double>(t2 - t1).count();

    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    std::cerr << "[OPRF-S] Recv seed: " << std::fixed << std::setprecision(1) << ms(t0, t1)
              << " ms | PRG expand: " << ms(t1, t2)
              << " ms | Total: " << ms(t0, t2) << " ms" << std::endl;

    co_return output;
}

} // namespace mpsoprf
