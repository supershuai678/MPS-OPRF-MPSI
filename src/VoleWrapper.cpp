// vOLE wrapper — libOTe SilentVole GF128
//
// libOTe SilentVole GF128 relation:
//   Sender (libOTe): input delta, output B[]
//   Receiver (libOTe): output mA[], mC[] where mA[i] = mB[i] + mC[i] * delta
//
// Protocol mapping:
//   Leader (S_n) = libOTe Receiver, gets (A', C)
//   R (Receiver) = libOTe Sender, gets (delta, B)
//   K[i] = B[i] XOR A[i].gf128Mul(delta) = C[i] (when A == A')

#include "VoleWrapper.h"
#include <libOTe/Vole/Silent/SilentVoleSender.h>
#include <libOTe/Vole/Silent/SilentVoleReceiver.h>

namespace mpsoprf {

using VoleSender = oc::SilentVoleSender<block, block, oc::CoeffCtxGF128>;
using VoleReceiver = oc::SilentVoleReceiver<block, block, oc::CoeffCtxGF128>;

// vOLE Sender (Leader S_n) — uses libOTe Receiver role
// Output: A' and C, length = voleLength (= paxos.size())
macoro::task<void> VoleWrapper::runVoleSender(
    coproto::Socket& channel,
    size_t numOTs,
    std::vector<block>& A_prime,
    std::vector<block>& C,
    PRNG& prng
) {
    VoleReceiver receiver;

    A_prime.resize(numOTs);
    C.resize(numOTs);

    co_await receiver.silentReceiveInplace(numOTs, prng, channel);

    // libOTe relation: mA[i] = mB[i] + mC[i] * delta
    // mC = random input  → maps to protocol's A'
    // mA = correlated output → maps to protocol's C
    for (size_t i = 0; i < numOTs && i < receiver.mC.size(); ++i)
        A_prime[i] = receiver.mC[i];
    for (size_t i = 0; i < numOTs && i < receiver.mA.size(); ++i)
        C[i] = receiver.mA[i];

    co_return;
}

// vOLE Receiver (R) — uses libOTe Sender role
// Output: delta and B, length = voleLength (= paxos.size())
macoro::task<void> VoleWrapper::runVoleReceiver(
    coproto::Socket& channel,
    size_t numOTs,
    block& delta,
    std::vector<block>& B,
    PRNG& prng
) {
    VoleSender sender;

    B.resize(numOTs);

    co_await sender.silentSendInplace(delta, numOTs, prng, channel);

    for (size_t i = 0; i < numOTs && i < sender.mB.size(); ++i)
        B[i] = sender.mB[i];

    co_return;
}

// Legacy interfaces (kept for backward compatibility)

macoro::task<std::pair<std::vector<block>, std::vector<block>>>
VoleWrapper::runAsSender(
    coproto::Socket& channel,
    size_t numOTs,
    PRNG& prng
) {
    VoleSender sender;

    std::vector<block> a(numOTs);
    std::vector<block> c(numOTs);

    block delta;
    co_await sender.silentSendInplace(delta, numOTs, prng, channel);

    for (size_t i = 0; i < numOTs && i < sender.mB.size(); ++i)
        c[i] = sender.mB[i];
    a[0] = delta;

    co_return std::make_pair(std::move(a), std::move(c));
}

macoro::task<std::vector<block>>
VoleWrapper::runAsReceiver(
    coproto::Socket& channel,
    const std::vector<block>& b,
    PRNG& prng
) {
    VoleReceiver receiver;
    size_t numOTs = b.size();

    co_await receiver.silentReceiveInplace(numOTs, prng, channel);

    std::vector<block> d(numOTs);
    for (size_t i = 0; i < numOTs && i < receiver.mA.size(); ++i)
        d[i] = receiver.mA[i];

    co_return d;
}

macoro::task<void> VoleWrapper::runVole(
    coproto::Socket& channel,
    bool isSender,
    size_t numOTs,
    std::vector<block>& out1,
    std::vector<block>& out2,
    PRNG& prng
) {
    if (isSender) {
        VoleSender sender;
        out1.resize(numOTs);
        out2.resize(numOTs);
        block delta;
        co_await sender.silentSendInplace(delta, numOTs, prng, channel);
        for (size_t i = 0; i < numOTs && i < sender.mB.size(); ++i)
            out2[i] = sender.mB[i];
        out1[0] = delta;
    } else {
        VoleReceiver receiver;
        co_await receiver.silentReceiveInplace(numOTs, prng, channel);
        out1.resize(numOTs);
        out2.resize(numOTs);
        // libOTe: mC = random input (→ out1), mA = correlated output (→ out2)
        for (size_t i = 0; i < numOTs && i < receiver.mC.size(); ++i)
            out1[i] = receiver.mC[i];
        for (size_t i = 0; i < numOTs && i < receiver.mA.size(); ++i)
            out2[i] = receiver.mA[i];
    }
    co_return;
}

} // namespace mpsoprf
