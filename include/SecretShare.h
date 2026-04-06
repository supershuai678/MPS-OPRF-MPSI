#pragma once
// XOR secret sharing: secret = share_1 XOR share_2 XOR ... XOR share_n

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <vector>

namespace mpsoprf {

using namespace osuCrypto;

class SecretShare {
public:
    // Split: secret = XOR of all shares
    static std::vector<block> split(
        const block& secret,
        size_t numShares,
        PRNG& prng
    );

    // Reconstruct: XOR all shares
    static block reconstruct(const std::vector<block>& shares);
};

} // namespace mpsoprf
