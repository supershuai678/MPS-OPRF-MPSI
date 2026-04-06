#pragma once
// Ring topology MPSI protocol
// Party order: S_n -> S_{n-1} -> ... -> S_2 -> S_1 -> R

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/Socket.h>
#include <vector>

#include "MpsOprf.h"
#include "HomomorphicPRF.h"
#include "RandomOracle.h"

namespace mpsoprf {

using namespace osuCrypto;

class RingMpsi {
public:
    struct ReceiverMetrics {
        std::vector<block> intersection;
        double activePhaseSeconds = 0.0;
    };

    struct SenderMetrics {
        double clientPhaseSeconds = 0.0;
    };

    static bool enableSecurityExtension;

    // Receiver: receives T_1 from S_1, computes intersection
    static macoro::task<ReceiverMetrics> runAsReceiver(
        coproto::Socket& prevChannel,      // from S_1
        coproto::Socket& leaderChannel,    // from S_n (for MPS-OPRF)
        const std::vector<block>& Y,
        PRNG& prng
    );

    // Leader S_n: encodes T_n, sends to S_{n-1}
    static macoro::task<void> runAsLeader(
        coproto::Socket& nextChannel,      // to S_{n-1}
        coproto::Socket& receiverChannel,  // to R (for MPS-OPRF)
        const std::vector<coproto::Socket*>& senderChannels,
        const std::vector<block>& X_n,
        size_t numSenders,
        PRNG& prng
    );

    // Sender S_i (middle node, i in [2, n-1])
    static macoro::task<SenderMetrics> runAsSender(
        coproto::Socket& prevChannel,      // from S_{i+1}
        coproto::Socket& nextChannel,      // to S_{i-1}
        coproto::Socket& leaderChannel,    // from leader (MPS-OPRF share)
        const std::vector<block>& X_i,
        PRNG& prng
    );

    // First sender S_1: receives T_2, sends T_1 to R
    // BUG-8 fix: added prevChannel to receive T_prev from S_2
    static macoro::task<SenderMetrics> runAsFirstSender(
        coproto::Socket& leaderChannel,    // from leader (MPS-OPRF share)
        coproto::Socket& prevChannel,      // from S_2 (receive T_prev) — BUG-8 fix
        coproto::Socket& nextChannel,      // to R (send T_1)
        const std::vector<block>& X_1,
        PRNG& prng
    );

private:
    static std::vector<block> preprocessInputs(const std::vector<block>& inputs);
};

} // namespace mpsoprf
