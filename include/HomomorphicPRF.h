#pragma once

// ============================================================================
// GF(2^128) mode: XOR addition + GF128 polynomial multiplication
// PRF: F(k, x) = Decode(OKVS, x) XOR k
// Always available — both modes use GF128 for protocol correctness
// ============================================================================

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include <vector>

#ifdef ENABLE_HOMOMORPHIC_HASH
#include <sodium.h>
#endif

namespace mpsoprf {

using namespace osuCrypto;

using PRFKey = block;
using PRFOutput = block;

class GF128PRF {
public:
    // F(decoded, w) = decoded XOR w
    static block eval(const block& decoded_value, const block& w) {
        return decoded_value ^ w;
    }

    static block randomKey(PRNG& prng) {
        return prng.get<block>();
    }

    // XOR secret sharing: w = w_1 XOR w_2 XOR ... XOR w_n
    static std::vector<block> shareKey(const block& w, size_t n, PRNG& prng) {
        std::vector<block> shares(n);
        block xorSum = oc::ZeroBlock;
        for (size_t i = 0; i < n - 1; i++) {
            shares[i] = prng.get<block>();
            xorSum = xorSum ^ shares[i];
        }
        shares[n - 1] = xorSum ^ w;
        return shares;
    }

    static block combineShares(const std::vector<block>& shares) {
        block result = oc::ZeroBlock;
        for (const auto& s : shares) result = result ^ s;
        return result;
    }

    // F_i(decoded, w_i) = decoded XOR w_i
    static block evalShare(const block& decoded_value, const block& wi) {
        return decoded_value ^ wi;
    }

    // H: block -> GF(2^128) field element (for OKVS encoding values)
    static block hashToField(const block& x) {
        RandomOracle ro(sizeof(block));
        ro.Update(x);
        block result;
        ro.Final(result);
        return result;
    }
};

#ifdef ENABLE_HOMOMORPHIC_HASH

// ============================================================================
// Homomorphic Hash mode: Ristretto255 EC operations
// H_h(k, x) = H_G(x)^k, requires libsodium
// Used for EC overhead simulation in evalPRF
// ============================================================================

struct Point;

struct Scalar {
    unsigned char data[crypto_core_ristretto255_SCALARBYTES];

    Scalar() { memset(data, 0, sizeof(data)); }

    explicit Scalar(const block& b) {
        memset(data, 0, sizeof(data));
        memcpy(data, &b, std::min(sizeof(block), sizeof(data)));
    }

    void toBytes(unsigned char* out) const { memcpy(out, data, sizeof(data)); }
    void fromBytes(const unsigned char* in) { memcpy(data, in, sizeof(data)); }

    block toBlock() const {
        block b;
        memset(&b, 0, sizeof(b));
        memcpy(&b, data, std::min(sizeof(block), sizeof(data)));
        return b;
    }

    void fromBlock(const block& blk) {
        memset(data, 0, sizeof(data));
        memcpy(data, &blk, std::min(sizeof(block), sizeof(data)));
    }

    Scalar operator+(const Scalar& other) const {
        Scalar result;
        crypto_core_ristretto255_scalar_add(result.data, data, other.data);
        return result;
    }

    Scalar operator-(const Scalar& other) const {
        Scalar result;
        crypto_core_ristretto255_scalar_sub(result.data, data, other.data);
        return result;
    }

    Scalar operator*(const Scalar& other) const {
        Scalar result;
        crypto_core_ristretto255_scalar_mul(result.data, data, other.data);
        return result;
    }

    bool operator==(const Scalar& other) const {
        return sodium_memcmp(data, other.data, sizeof(data)) == 0;
    }

    bool operator!=(const Scalar& other) const { return !(*this == other); }
};

struct Point {
    unsigned char data[crypto_core_ristretto255_BYTES];

    Point() { memset(data, 0, sizeof(data)); }

    void toBytes(unsigned char* out) const { memcpy(out, data, sizeof(data)); }
    void fromBytes(const unsigned char* in) { memcpy(data, in, sizeof(data)); }

    block toBlock() const {
        block b;
        memset(&b, 0, sizeof(b));
        memcpy(&b, data, std::min(sizeof(block), (size_t)crypto_core_ristretto255_BYTES));
        return b;
    }

    static Point fromBlock(const block& b) {
        Point p;
        memcpy(p.data, &b, std::min(sizeof(block), (size_t)crypto_core_ristretto255_BYTES));
        return p;
    }

    Point pow(const Scalar& scalar) const {
        Point result;
        crypto_scalarmult_ristretto255(result.data, scalar.data, this->data);
        return result;
    }

    bool isIdentity() const {
        for (size_t i = 0; i < crypto_core_ristretto255_BYTES; ++i)
            if (data[i] != 0) return false;
        return true;
    }

    bool operator==(const Point& other) const {
        return sodium_memcmp(data, other.data, crypto_core_ristretto255_BYTES) == 0;
    }

    bool operator!=(const Point& other) const { return !(*this == other); }
};

constexpr size_t DLP_BYTES = crypto_core_ristretto255_BYTES;

class HomomorphicPRF {
public:
    static Scalar hashToScalar(const block& x);
    static Point hashToGroup(const block& x);
    static Point eval(const Scalar& k, const block& x);
    static bool verifyHomomorphic(const Scalar& k1, const Scalar& k2, const block& x);
    static Scalar randomKey(PRNG& prng);
    static Scalar addKeys(const Scalar& k1, const Scalar& k2);
    static Scalar subKeys(const Scalar& k1, const Scalar& k2);
    static Scalar mulKeys(const Scalar& k1, const Scalar& k2);
    static Point mulPoints(const Point& p1, const Point& p2);
    static Point scalarMulPoint(const Scalar& k, const Point& p);
    static bool equalPoints(const Point& p1, const Point& p2);
    static Scalar pointToScalar(const Point& p);
};

using DLPScalar = Scalar;
using DLPPoint = Point;

#endif // ENABLE_HOMOMORPHIC_HASH

} // namespace mpsoprf
