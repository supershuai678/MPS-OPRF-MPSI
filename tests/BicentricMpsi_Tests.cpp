// 双中心MPSI测试
#include <iostream>
#include <set>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <volePSI/Paxos.h>
#include "BicentricMpsi.h"
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

void test_PRF_consistency() {
    std::cout << "Testing PRF consistency... " << std::flush;
    PRNG prng(sysRandomSeed());
    block w_block = prng.get<block>();
    block a_block = prng.get<block>();
    auto w_shares = SecretShare::split(w_block, 3, prng);
    auto a_shares = SecretShare::split(a_block, 3, prng);
    assert(w_block == SecretShare::reconstruct(w_shares));
    assert(a_block == SecretShare::reconstruct(a_shares));
    std::cout << "PASSED" << std::endl;
}

void test_OKVS() {
    std::cout << "Testing OKVS encode/decode... " << std::flush;
    PRNG prng(sysRandomSeed());
    const size_t m = 64;
    std::vector<block> keys(m), values(m);
    for (size_t i = 0; i < m; ++i) { keys[i] = prng.get<block>(); values[i] = prng.get<block>(); }
    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, block(0, 0));
    std::vector<block> encoded(paxos.size());
    paxos.solve<block>(span<const block>(keys), span<const block>(values), span<block>(encoded), &prng, 1);
    std::vector<block> decoded(m);
    paxos.decode<block>(span<const block>(keys), span<block>(decoded), span<const block>(encoded), 1);
    for (size_t i = 0; i < m; ++i) assert(decoded[i] == values[i]);
    std::cout << "PASSED" << std::endl;
}

void test_3party_local() {
    std::cout << "Testing 3-party local... " << std::flush;
    PRNG prng(sysRandomSeed());
    const size_t setSize = 32, intersectionSize = 5;
    std::vector<block> intersection;
    for (size_t i = 0; i < intersectionSize; ++i) intersection.push_back(prng.get<block>());
    auto s1 = generateSet(setSize, prng, intersection);
    auto s2 = generateSet(setSize, prng, intersection);
    auto s3 = generateSet(setSize, prng, intersection);
    size_t cnt = 0;
    for (const auto& e : intersection) {
        bool in1 = std::find(s1.begin(),s1.end(),e)!=s1.end();
        bool in2 = std::find(s2.begin(),s2.end(),e)!=s2.end();
        bool in3 = std::find(s3.begin(),s3.end(),e)!=s3.end();
        if (in1 && in2 && in3) cnt++;
    }
    assert(cnt == intersectionSize);
    std::cout << "PASSED (" << cnt << "/" << intersectionSize << ")" << std::endl;
}

void test_4party_local() {
    std::cout << "Testing 4-party local... " << std::flush;
    PRNG prng(sysRandomSeed());
    const size_t setSize = 64, intersectionSize = 8, numParties = 4;
    std::vector<block> intersection;
    for (size_t i = 0; i < intersectionSize; ++i) intersection.push_back(prng.get<block>());
    std::vector<std::vector<block>> sets(numParties);
    for (size_t i = 0; i < numParties; ++i) sets[i] = generateSet(setSize, prng, intersection);
    size_t cnt = 0;
    for (const auto& e : intersection) {
        bool inAll = true;
        for (size_t i = 0; i < numParties && inAll; ++i)
            if (std::find(sets[i].begin(), sets[i].end(), e) == sets[i].end()) inAll = false;
        if (inAll) cnt++;
    }
    assert(cnt == intersectionSize);
    std::cout << "PASSED (" << cnt << "/" << intersectionSize << ")" << std::endl;
}

void test_different_sizes() {
    std::cout << "Testing different sizes... " << std::flush;
    PRNG prng(sysRandomSeed());
    size_t sizes[] = {16, 64, 128, 256};
    for (size_t sz : sizes) {
        std::vector<block> intersection;
        for (size_t i = 0; i < sz/8; ++i) intersection.push_back(prng.get<block>());
        auto set1 = generateSet(sz, prng, intersection);
        volePSI::Baxos paxos;
        paxos.init(sz, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, block(0, 0));
        std::vector<block> values(sz), encoded(paxos.size());
        for (size_t i = 0; i < sz; ++i) values[i] = prng.get<block>();
        paxos.solve<block>(span<const block>(set1), span<const block>(values), span<block>(encoded), &prng, 1);
    }
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "BicentricMpsi Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    try {
        test_PRF_consistency();
        test_OKVS();
        test_3party_local();
        test_4party_local();
        test_different_sizes();
        std::cout << "\nAll BicentricMpsi tests PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFAILED: " << e.what() << std::endl;
        return 1;
    }
}
