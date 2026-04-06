#pragma once
// MPS-OPRF protocol (Multi-Point Shared OPRF)
//
// Unified GF(2^128) mode: F(x) = Decode(OKVS, x) XOR w
// ENABLE_HOMOMORPHIC_HASH adds EC overhead simulation in evalPRF (correctness unchanged)

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/Socket.h>
#include <functional>
#include <vector>

#include "HomomorphicPRF.h"
#include "SecretShare.h"
#include "CoinToss.h"

namespace mpsoprf {

using namespace osuCrypto;

class MpsOprf {
public:

    // === Unified GF(2^128) mode ===
    // Both GF128 and HH modes use GF128 for protocol correctness.
    // ENABLE_HOMOMORPHIC_HASH only adds EC overhead simulation in evalPRF.

    struct ReceiverOutput {
        block w;                       // shared random value (GF128)
        block delta;                   // vOLE delta (GF128)
        std::vector<block> K;         // K = B XOR gf128Mul(A, delta)
        size_t m;                      // original element count (BUG-5)

        // F_R(x) = Decode(K, x) XOR gf128Mul(delta, H(x)) XOR w
        block evalPRF(const block& x, const std::vector<block>& K_okvs) const;

#ifdef ENABLE_HOMOMORPHIC_HASH
        // H_h(F_R(x), x) = H_G(x)^{F_R(x)} — returns EC Point
        Point evalPRF_HH(const block& x, const std::vector<block>& K_okvs) const;
#endif
    };

    struct SenderOutput {
        block w_i;                     // key share (GF128)
        std::vector<block> C_i;       // C share (OKVS form)
        size_t m;                      // original element count (BUG-5)
        double activeSeconds = 0.0;    // local PRG expansion time after seed arrives

        // F_i(x) = Decode(C_i, x) XOR w_i
        block evalPRF(const block& x) const;

#ifdef ENABLE_HOMOMORPHIC_HASH
        // H_h(F_i(x), x) = H_G(x)^{F_i(x)} — returns EC Point
        Point evalPRF_HH(const block& x) const;
#endif
    };

    struct LeaderOutput {
        block w;                       // full key from coin toss
        std::vector<block> C;         // vOLE C values
        std::vector<block> w_shares;  // w XOR shares
        std::vector<std::vector<block>> C_shares; // C XOR shares
        size_t m;                      // original element count (BUG-5)

        // F_leader(x) = Decode(C_shares[myIndex], x) XOR w_shares[myIndex]
        block evalPRF(const block& x, size_t myIndex) const;

#ifdef ENABLE_HOMOMORPHIC_HASH
        // H_h(F_leader(x), x) = H_G(x)^{F_leader(x)} — returns EC Point
        Point evalPRF_HH(const block& x, size_t myIndex) const;
#endif
    };

    static macoro::task<ReceiverOutput> runAsReceiver(
        coproto::Socket& leaderChannel,
        const std::vector<block>& Y,
        PRNG& prng
    );

    static macoro::task<LeaderOutput> runAsLeader(
        coproto::Socket& receiverChannel,
        const std::vector<coproto::Socket*>& senderChannels,
        const std::vector<block>& X_n,
        size_t numSenders,
        PRNG& prng
    );

    static macoro::task<SenderOutput> runAsSender(
        coproto::Socket& leaderChannel,
        PRNG& prng
    );
};

} // namespace mpsoprf
