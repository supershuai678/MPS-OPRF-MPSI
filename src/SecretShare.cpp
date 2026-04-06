// XOR secret sharing: secret = share_1 XOR share_2 XOR ... XOR share_n

#include "SecretShare.h"

namespace mpsoprf {

std::vector<block> SecretShare::split(
    const block& secret,
    size_t numShares,
    PRNG& prng
) {
    if (numShares == 0)
        throw std::invalid_argument("SecretShare: numShares must be > 0");

    std::vector<block> shares(numShares);

    if (numShares == 1) {
        shares[0] = secret;
        return shares;
    }

    block xorSum = oc::ZeroBlock;
    for (size_t i = 0; i < numShares - 1; ++i) {
        shares[i] = prng.get<block>();
        xorSum = xorSum ^ shares[i];
    }
    shares[numShares - 1] = secret ^ xorSum;

    return shares;
}

block SecretShare::reconstruct(const std::vector<block>& shares) {
    if (shares.empty())
        throw std::invalid_argument("SecretShare: shares cannot be empty");

    block result = oc::ZeroBlock;
    for (const auto& share : shares)
        result = result ^ share;
    return result;
}

} // namespace mpsoprf
