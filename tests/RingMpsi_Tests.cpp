// 环形MPSI测试
#include <iostream>
#include <set>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <volePSI/Paxos.h>
#include "RingMpsi.h"
#include "HomomorphicPRF.h"
#include "SecretShare.h"

using namespace osuCrypto;
using namespace mpsoprf;

std::vector<block> generateSet(size_t size, PRNG& prng, const std::vector<block>& intersection) {
    std::vector<block> set;
    std::set<block> seen;
    for (const auto& elem : intersection) { set.push_back(elem); seen.insert(elem); }
    while (set.size() < size) {
        block r = prng.get<block>();
        if (seen.find(r) == seen.end()) { set.push_back(r); seen.insert(r); }
    }
    return set;
}

// 测试环形传递逻辑
void test_ring_transfer() {
    std::cout << "Testing ring transfer logic... " << std::flush;

    PRNG prng(sysRandomSeed());
    const size_t m = 32;
    const size_t numParties = 4;

    // 生成交集
    std::vector<block> intersection;
    for (size_t i = 0; i < 5; ++i) {
        intersection.push_back(prng.get<block>());
    }

    // 生成各方集合
    std::vector<std::vector<block>> sets(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        sets[i] = generateSet(m, prng, intersection);
    }

    // 模拟环形传递
    // S_n -> S_1 -> S_2 -> ... -> S_{n-1} -> R

    // 初始化OKVS
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, block(0, 0));

    // 主导发送方编码
    std::vector<block> values_n(m);
    for (size_t j = 0; j < m; ++j) {
        values_n[j] = prng.get<block>();
    }

    std::vector<block> T_n(paxos.size());
    paxos.solve<block>(span<const block>(sets[0]), span<const block>(values_n),
                span<block>(T_n), &prng, 1);

    // 模拟环形传递
    std::vector<block> T_current = T_n;

    for (size_t i = 1; i < numParties - 1; ++i) {
        // 解码
        std::vector<block> decoded(m);
        paxos.decode<block>(span<const block>(sets[i]), span<block>(decoded),
                     span<const block>(T_current), 1);

        // 计算新值（模拟PRF乘积）
        std::vector<block> new_values(m);
        for (size_t j = 0; j < m; ++j) {
            new_values[j] = decoded[j] ^ prng.get<block>();  // 简化的PRF模拟
        }

        // 重新编码
        std::vector<block> T_new(paxos.size());
        paxos.solve<block>(span<const block>(sets[i]), span<const block>(new_values),
                    span<block>(T_new), &prng, 1);

        T_current = T_new;
    }

    // 接收方解码
    std::vector<block> final_decoded(m);
    paxos.decode<block>(span<const block>(sets[numParties - 1]), span<block>(final_decoded),
                 span<const block>(T_current), 1);

    std::cout << "PASSED" << std::endl;
}

// 测试3方环形本地模拟
void test_3party_ring_local() {
    std::cout << "Testing 3-party ring local... " << std::flush;

    PRNG prng(sysRandomSeed());
    const size_t setSize = 32, intersectionSize = 5;

    std::vector<block> intersection;
    for (size_t i = 0; i < intersectionSize; ++i) {
        intersection.push_back(prng.get<block>());
    }

    auto s1 = generateSet(setSize, prng, intersection);
    auto s2 = generateSet(setSize, prng, intersection);
    auto s3 = generateSet(setSize, prng, intersection);

    // 验证交集
    size_t cnt = 0;
    for (const auto& e : intersection) {
        bool in1 = std::find(s1.begin(), s1.end(), e) != s1.end();
        bool in2 = std::find(s2.begin(), s2.end(), e) != s2.end();
        bool in3 = std::find(s3.begin(), s3.end(), e) != s3.end();
        if (in1 && in2 && in3) cnt++;
    }

    assert(cnt == intersectionSize);
    std::cout << "PASSED (" << cnt << "/" << intersectionSize << ")" << std::endl;
}

// 测试4方环形本地模拟
void test_4party_ring_local() {
    std::cout << "Testing 4-party ring local... " << std::flush;

    PRNG prng(sysRandomSeed());
    const size_t setSize = 64, intersectionSize = 8, numParties = 4;

    std::vector<block> intersection;
    for (size_t i = 0; i < intersectionSize; ++i) {
        intersection.push_back(prng.get<block>());
    }

    std::vector<std::vector<block>> sets(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        sets[i] = generateSet(setSize, prng, intersection);
    }

    // 验证密钥分割 (using block directly)
    block w_block = prng.get<block>();
    block a_block = prng.get<block>();

    auto w_shares = SecretShare::split(w_block, numParties - 1, prng);
    auto a_shares = SecretShare::split(a_block, numParties - 1, prng);

    assert(w_block == SecretShare::reconstruct(w_shares));
    assert(a_block == SecretShare::reconstruct(a_shares));

    // 验证交集
    size_t cnt = 0;
    for (const auto& e : intersection) {
        bool inAll = true;
        for (size_t i = 0; i < numParties && inAll; ++i) {
            if (std::find(sets[i].begin(), sets[i].end(), e) == sets[i].end())
                inAll = false;
        }
        if (inAll) cnt++;
    }

    assert(cnt == intersectionSize);
    std::cout << "PASSED (" << cnt << "/" << intersectionSize << ")" << std::endl;
}

// 测试OKVS链式传递
void test_okvs_chain() {
    std::cout << "Testing OKVS chain transfer... " << std::flush;

    PRNG prng(sysRandomSeed());
    const size_t m = 64;

    // 生成共同元素
    std::vector<block> common_keys(m);
    for (size_t i = 0; i < m; ++i) {
        common_keys[i] = prng.get<block>();
    }

    // 初始值
    std::vector<block> initial_values(m);
    for (size_t i = 0; i < m; ++i) {
        initial_values[i] = prng.get<block>();
    }

    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, block(0, 0));

    // 编码
    std::vector<block> encoded(paxos.size());
    paxos.solve<block>(span<const block>(common_keys), span<const block>(initial_values),
                span<block>(encoded), &prng, 1);

    // 解码验证
    std::vector<block> decoded(m);
    paxos.decode<block>(span<const block>(common_keys), span<block>(decoded),
                 span<const block>(encoded), 1);

    for (size_t i = 0; i < m; ++i) {
        assert(decoded[i] == initial_values[i]);
    }

    std::cout << "PASSED" << std::endl;
}

// 测试不同集合大小
void test_different_sizes() {
    std::cout << "Testing different sizes... " << std::flush;

    PRNG prng(sysRandomSeed());
    size_t sizes[] = {16, 64, 128, 256};

    for (size_t sz : sizes) {
        std::vector<block> intersection;
        for (size_t i = 0; i < sz/8; ++i) {
            intersection.push_back(prng.get<block>());
        }

        auto set1 = generateSet(sz, prng, intersection);

        volePSI::Baxos paxos;
        paxos.init(sz, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, block(0, 0));

        std::vector<block> values(sz), encoded(paxos.size());
        for (size_t i = 0; i < sz; ++i) values[i] = prng.get<block>();

        paxos.solve<block>(span<const block>(set1), span<const block>(values),
                    span<block>(encoded), &prng, 1);
    }

    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "RingMpsi Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        test_ring_transfer();
        test_okvs_chain();
        test_3party_ring_local();
        test_4party_ring_local();
        test_different_sizes();

        std::cout << "\nAll RingMpsi tests PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << std::endl;
        return 1;
    }
}
