#ifdef ENABLE_HOMOMORPHIC_HASH
// 同态伪随机函数实现
// 基于离散对数问题（DLP）的乘法群实现
// 
// 重要：哈希函数区分
// ==================
// - H: 普通哈希函数（如SHA-256），H: X -> F，输出为有限域元素/标量
// - H_G: 哈希到群函数，H_G: X -> G，输出为群元素（本文件中的hashToGroup函数）
// - H_h(k, x): 同态伪随机函数，H_h(k, x) = H_G(x)^k，输出为群元素
//
// 使用libsodium的Ristretto255实现，数学上等价于DLP乘法群：
// - Ristretto255是素数阶椭圆曲线群
// - 点加法对应乘法群的乘法
// - 标量乘法对应乘法群的幂运算
//
// H_h(k, x) = k * H_G(x) 等价于 H_G(x)^k

#include "HomomorphicPRF.h"
#include <cryptoTools/Crypto/RandomOracle.h>

namespace mpsoprf {

// ============================================================================
// 普通哈希函数 H: X -> F（输出标量）
// 用于OKVS编码值，如 P = Encode(x, H(x))
// 这是独立于 H_G 的普通哈希函数
// ============================================================================
Scalar HomomorphicPRF::hashToScalar(const block& x) {
    Scalar result;
    
    // 使用SHA-512哈希，然后reduce到标量
    unsigned char hash[crypto_hash_sha512_BYTES];
    crypto_hash_sha512(hash, (const unsigned char*)&x, sizeof(block));
    
    // 将哈希值reduce到Ristretto255标量域
    crypto_core_ristretto255_scalar_reduce(result.data, hash);
    
    return result;
}

// ============================================================================
// 哈希到群函数 H_G: X -> G（输出群元素）
// 用于同态PRF的基础
// ============================================================================

// 哈希到群: H_G: {0,1}* -> G
// 注意：这是 H_G（哈希到群），不是普通哈希 H
Point HomomorphicPRF::hashToGroup(const block& x) {
    Point result;
    
    // 使用SHA-512哈希，然后映射到Ristretto255点
    unsigned char hash[crypto_hash_sha512_BYTES];
    crypto_hash_sha512(hash, (const unsigned char*)&x, sizeof(block));
    
    // 从哈希值派生Ristretto255点
    crypto_core_ristretto255_from_hash(result.data, hash);
    
    return result;
}

// 同态PRF: H_h(k, x) = k * H_G(x)
// 在乘法群表示下等价于 H_G(x)^k
// 注意：这是 H_h（同态PRF），不是普通哈希 H
Point HomomorphicPRF::eval(const Scalar& k, const block& x) {
    Point hx = hashToGroup(x);  // H_G(x)
    Point result;
    
    // 标量乘法: result = k * H_G(x)
    // 在乘法群表示下等价于 H_G(x)^k
    crypto_scalarmult_ristretto255(result.data, k.data, hx.data);
    
    return result;
}

// 验证同态性: H_h(k1+k2, x) = H_h(k1, x) * H_h(k2, x)
bool HomomorphicPRF::verifyHomomorphic(const Scalar& k1, const Scalar& k2, const block& x) {
    // 计算 k1 + k2
    Scalar k_sum = addKeys(k1, k2);
    
    // 左边: H_h(k1+k2, x)
    Point left = eval(k_sum, x);
    
    // 右边: H_h(k1, x) * H_h(k2, x)
    Point p1 = eval(k1, x);
    Point p2 = eval(k2, x);
    Point right = mulPoints(p1, p2);
    
    return equalPoints(left, right);
}

// 生成随机密钥
Scalar HomomorphicPRF::randomKey(PRNG& prng) {
    Scalar result;
    // 使用libsodium生成随机标量
    crypto_core_ristretto255_scalar_random(result.data);
    return result;
}

// 密钥加法（标量加法）
// 在乘法群表示下对应指数加法: k1 + k2
Scalar HomomorphicPRF::addKeys(const Scalar& k1, const Scalar& k2) {
    Scalar result;
    crypto_core_ristretto255_scalar_add(result.data, k1.data, k2.data);
    return result;
}

// 密钥减法（标量减法）
// k1 - k2
Scalar HomomorphicPRF::subKeys(const Scalar& k1, const Scalar& k2) {
    Scalar result;
    crypto_core_ristretto255_scalar_sub(result.data, k1.data, k2.data);
    return result;
}

// 密钥乘法（标量乘法）
// k1 * k2
Scalar HomomorphicPRF::mulKeys(const Scalar& k1, const Scalar& k2) {
    Scalar result;
    crypto_core_ristretto255_scalar_mul(result.data, k1.data, k2.data);
    return result;
}

// 点乘法（群运算）
// 在乘法群表示下对应群乘法: p1 * p2
Point HomomorphicPRF::mulPoints(const Point& p1, const Point& p2) {
    Point result;
    crypto_core_ristretto255_add(result.data, p1.data, p2.data);
    return result;
}

// 标量乘点: k * P (幂运算 P^k)
Point HomomorphicPRF::scalarMulPoint(const Scalar& k, const Point& p) {
    Point result;
    crypto_scalarmult_ristretto255(result.data, k.data, p.data);
    return result;
}

// 检查两个点是否相等
bool HomomorphicPRF::equalPoints(const Point& p1, const Point& p2) {
    return sodium_memcmp(p1.data, p2.data, crypto_core_ristretto255_BYTES) == 0;
}

// 将Point编码为Scalar（用于某些计算）
// 通过哈希Point的字节表示得到Scalar
Scalar HomomorphicPRF::pointToScalar(const Point& p) {
    Scalar result;
    // 使用SHA-512哈希Point，然后reduce到标量
    unsigned char hash[crypto_hash_sha512_BYTES];
    crypto_hash_sha512(hash, p.data, crypto_core_ristretto255_BYTES);
    crypto_core_ristretto255_scalar_reduce(result.data, hash);
    return result;
}

} // namespace mpsoprf

#endif // ENABLE_HOMOMORPHIC_HASH
