// End-to-end diagnostic test for MPS-OPRF + BicentricMpsi
//
// Tests the FULL protocol with REAL SilentVole over LocalAsyncSocket
// (no TCP needed). Prints detailed diagnostics at each step.
//
// Build: cmake --build build --target e2e_diagnostic
// Run:   ./build/e2e_diagnostic

#include <iostream>
#include <thread>
#include <vector>
#include <cstdio>
#include <cassert>

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/LocalAsyncSock.h>
#include <volePSI/Paxos.h>

#include "MpsOprf.h"
#include "BicentricMpsi.h"
#include "VoleWrapper.h"
#include "SecretShare.h"
#include "RandomOracle.h"

using namespace osuCrypto;
using namespace mpsoprf;

static void pb(const char* label, const block& b) {
    auto* v = (const uint64_t*)&b;
    printf("  %-35s = (%016lx, %016lx)\n", label, v[0], v[1]);
}

// ==========================================================================
// Test 1: Verify vOLE relation with real SilentVole
// ==========================================================================
void test_vole_relation() {
    printf("\n==== TEST 1: vOLE Relation ====\n");

    const size_t numOTs = 1024;
    auto sockets = coproto::LocalAsyncSocket::makePair();

    std::vector<block> A_prime, C, B;
    block delta;
    std::exception_ptr eptr1, eptr2;

    // Leader (libOTe Receiver) gets A', C
    std::thread leaderThread([&]() {
        try {
            PRNG prng(block(0, 100));
            A_prime.resize(numOTs);
            C.resize(numOTs);
            macoro::sync_wait(VoleWrapper::runVoleSender(
                sockets[0], numOTs, A_prime, C, prng));
        } catch (...) {
            eptr1 = std::current_exception();
        }
    });

    // Receiver (libOTe Sender) gets delta, B
    std::thread receiverThread([&]() {
        try {
            PRNG prng(block(0, 200));
            B.resize(numOTs);
            macoro::sync_wait(VoleWrapper::runVoleReceiver(
                sockets[1], numOTs, delta, B, prng));
        } catch (...) {
            eptr2 = std::current_exception();
        }
    });

    leaderThread.join();
    receiverThread.join();
    if (eptr1) std::rethrow_exception(eptr1);
    if (eptr2) std::rethrow_exception(eptr2);

    printf("  A'.size()=%zu, C.size()=%zu, B.size()=%zu\n",
           A_prime.size(), C.size(), B.size());
    pb("delta", delta);

    // Verify: C[i] == A'[i] * delta XOR B[i]
    size_t pass = 0, fail = 0;
    for (size_t i = 0; i < numOTs; i++) {
        block expected = A_prime[i].gf128Mul(delta) ^ B[i];
        if (memcmp(&expected, &C[i], 16) == 0) {
            pass++;
        } else {
            fail++;
            if (fail <= 3) {
                printf("  FAIL at i=%zu:\n", i);
                pb("    A'[i]", A_prime[i]);
                pb("    B[i]", B[i]);
                pb("    C[i] (actual)", C[i]);
                pb("    A'[i]*delta^B[i] (expected)", expected);
            }
        }
    }

    printf("  vOLE relation check: %zu/%zu PASS, %zu FAIL\n", pass, numOTs, fail);
    if (fail > 0) {
        printf("  *** vOLE RELATION IS BROKEN — this is the root cause! ***\n");

        // Additional check: try the REVERSE relation C[i] = delta * A'[i] XOR B[i]
        size_t pass2 = 0;
        for (size_t i = 0; i < numOTs; i++) {
            block expected2 = delta.gf128Mul(A_prime[i]) ^ B[i];
            if (memcmp(&expected2, &C[i], 16) == 0) pass2++;
        }
        printf("  Reverse relation (delta*A'[i]^B[i]): %zu/%zu PASS\n", pass2, numOTs);

        // Check: B[i] == C[i] XOR A'[i] * delta
        size_t pass3 = 0;
        for (size_t i = 0; i < numOTs; i++) {
            block expected3 = C[i] ^ A_prime[i].gf128Mul(delta);
            if (memcmp(&expected3, &B[i], 16) == 0) pass3++;
        }
        printf("  B[i]==C[i]^A'[i]*delta: %zu/%zu PASS\n", pass3, numOTs);

        // Check: are any values all-zero?
        size_t zeroA = 0, zeroB = 0, zeroC = 0;
        for (size_t i = 0; i < numOTs; i++) {
            if (A_prime[i] == oc::ZeroBlock) zeroA++;
            if (B[i] == oc::ZeroBlock) zeroB++;
            if (C[i] == oc::ZeroBlock) zeroC++;
        }
        printf("  Zero blocks: A'=%zu, B=%zu, C=%zu (out of %zu)\n",
               zeroA, zeroB, zeroC, numOTs);
    }

    printf("  Result: %s\n\n", fail == 0 ? "PASS" : "FAIL");
}

// ==========================================================================
// Test 2: OKVS encode/decode linearity
// ==========================================================================
void test_okvs_linearity() {
    printf("==== TEST 2: OKVS Linearity ====\n");

    PRNG prng(block(0, 42));
    const size_t m = 64;

    volePSI::Baxos paxos;
    paxos.init(m, 1 << 14, 3, 40, volePSI::PaxosParam::GF128, oc::ZeroBlock);
    u64 paxosSize = paxos.size();
    printf("  m=%zu, paxosSize=%llu\n", m, (unsigned long long)paxosSize);

    // Generate keys and values
    std::vector<block> keys(m), vals(m);
    for (size_t i = 0; i < m; i++) {
        keys[i] = prng.get<block>();
        vals[i] = prng.get<block>();
    }

    // Encode
    std::vector<block> P(paxosSize);
    paxos.solve<block>(keys, vals, P, &prng, 1);

    // Test 2a: Basic roundtrip
    size_t roundtrip_pass = 0;
    for (size_t i = 0; i < m; i++) {
        block decoded;
        paxos.decode<block>({&keys[i], 1}, {&decoded, 1}, P, 1);
        if (decoded == vals[i]) roundtrip_pass++;
    }
    printf("  Roundtrip: %zu/%zu PASS\n", roundtrip_pass, m);

    // Test 2b: Scalar multiplication linearity
    // Decode(alpha * P, x) should == alpha * Decode(P, x)
    block alpha = prng.get<block>();
    std::vector<block> alphaP(paxosSize);
    for (size_t i = 0; i < paxosSize; i++)
        alphaP[i] = alpha.gf128Mul(P[i]);

    size_t linear_pass = 0;
    for (size_t i = 0; i < m; i++) {
        block decoded_alphaP;
        paxos.decode<block>({&keys[i], 1}, {&decoded_alphaP, 1}, alphaP, 1);
        block expected = alpha.gf128Mul(vals[i]);
        if (decoded_alphaP == expected) linear_pass++;
    }
    printf("  Scalar mult linearity: %zu/%zu PASS\n", linear_pass, m);

    // Test 2c: XOR linearity
    // Decode(P1 XOR P2, x) == Decode(P1, x) XOR Decode(P2, x)
    std::vector<block> vals2(m);
    for (size_t i = 0; i < m; i++) vals2[i] = prng.get<block>();
    std::vector<block> P2(paxosSize);
    paxos.solve<block>(keys, vals2, P2, &prng, 1);

    std::vector<block> P1xP2(paxosSize);
    for (size_t i = 0; i < paxosSize; i++)
        P1xP2[i] = P[i] ^ P2[i];

    size_t xor_pass = 0;
    for (size_t i = 0; i < m; i++) {
        block decoded_xor;
        paxos.decode<block>({&keys[i], 1}, {&decoded_xor, 1}, P1xP2, 1);
        block expected = vals[i] ^ vals2[i];
        if (decoded_xor == expected) xor_pass++;
    }
    printf("  XOR linearity: %zu/%zu PASS\n", xor_pass, m);

    bool ok = (roundtrip_pass == m && linear_pass == m && xor_pass == m);
    printf("  Result: %s\n\n", ok ? "PASS" : "FAIL");
}

// ==========================================================================
// Test 3: Full MPS-OPRF + Bicentric end-to-end
// ==========================================================================
void test_bicentric_e2e() {
    printf("==== TEST 3: Bicentric MPSI End-to-End ====\n");

    const size_t m = 128;
    const size_t n = 3; // 3 parties: Receiver + Leader + 1 Sender
    printf("  m=%zu, parties=%zu\n", m, n);

    // Generate test sets: first m/2 common
    std::vector<block> setR(m), setL(m), setS(m);
    for (size_t i = 0; i < m / 2; i++)
        setR[i] = setL[i] = setS[i] = block(0, i);
    PRNG prngR(block(0, 1)), prngL(block(0, 2)), prngS(block(0, 3));
    for (size_t i = m / 2; i < m; i++) {
        setR[i] = prngR.get<block>();
        setL[i] = prngL.get<block>();
        setS[i] = prngS.get<block>();
    }

    // Create local socket pairs
    // Receiver-Leader, Receiver-Sender, Leader-Sender
    auto rl_socks = coproto::LocalAsyncSocket::makePair(); // [0]=Leader, [1]=Receiver
    auto rs_socks = coproto::LocalAsyncSocket::makePair(); // [0]=Sender, [1]=Receiver
    auto ls_socks = coproto::LocalAsyncSocket::makePair(); // [0]=Sender, [1]=Leader

    std::vector<block> result;
    std::exception_ptr eptr_r, eptr_l, eptr_s;

    // Receiver thread
    std::thread receiverThread([&]() {
        try {
            PRNG prng(block(0, 10));
            std::vector<coproto::Socket*> senderPtrs = {&rs_socks[1]};
            auto receiverMetrics = macoro::sync_wait(BicentricMpsi::runAsReceiver(
                rl_socks[1], senderPtrs, setR, prng));
            result = std::move(receiverMetrics.intersection);
        } catch (...) {
            eptr_r = std::current_exception();
        }
    });

    // Leader thread
    std::thread leaderThread([&]() {
        try {
            PRNG prng(block(0, 20));
            std::vector<coproto::Socket*> senderPtrs = {&ls_socks[1]};
            macoro::sync_wait(BicentricMpsi::runAsLeader(
                rl_socks[0], senderPtrs, setL, prng));
        } catch (...) {
            eptr_l = std::current_exception();
        }
    });

    // Sender thread
    std::thread senderThread([&]() {
        try {
            PRNG prng(block(0, 30));
            macoro::sync_wait(BicentricMpsi::runAsSender(
                rs_socks[0], ls_socks[0], setS, prng));
        } catch (...) {
            eptr_s = std::current_exception();
        }
    });

    receiverThread.join();
    leaderThread.join();
    senderThread.join();

    if (eptr_r) {
        printf("  Receiver exception: ");
        try { std::rethrow_exception(eptr_r); }
        catch (const std::exception& e) { printf("%s\n", e.what()); }
    }
    if (eptr_l) {
        printf("  Leader exception: ");
        try { std::rethrow_exception(eptr_l); }
        catch (const std::exception& e) { printf("%s\n", e.what()); }
    }
    if (eptr_s) {
        printf("  Sender exception: ");
        try { std::rethrow_exception(eptr_s); }
        catch (const std::exception& e) { printf("%s\n", e.what()); }
    }

    printf("  Intersection size: %zu (expected %zu)\n", result.size(), m / 2);

    if (result.size() == m / 2) {
        printf("  Result: PASS\n");
    } else {
        printf("  Result: FAIL\n");
        if (result.size() == 0) {
            printf("\n  *** Intersection is 0 — protocol is completely broken ***\n");
            printf("  If Test 1 (vOLE) PASSED, the issue is in protocol integration.\n");
            printf("  If Test 1 (vOLE) FAILED, the vOLE relation is the root cause.\n");
        }
    }
    printf("\n");
}

// ==========================================================================
// Test 4: Isolated MPS-OPRF PRF consistency
// ==========================================================================
void test_oprf_consistency() {
    printf("==== TEST 4: MPS-OPRF PRF Consistency ====\n");

    const size_t m = 64;
    auto sockets = coproto::LocalAsyncSocket::makePair(); // [0]=Leader, [1]=Receiver
    auto ls_socks = coproto::LocalAsyncSocket::makePair(); // [0]=Sender, [1]=Leader

    // Common elements
    std::vector<block> commonSet(m);
    for (size_t i = 0; i < m; i++)
        commonSet[i] = block(0, i);

    auto processedSet = SecureRandomOracle::preprocessInputs(commonSet);

    MpsOprf::ReceiverOutput recvOut;
    MpsOprf::LeaderOutput leaderOut;
    MpsOprf::SenderOutput senderOut;
    std::exception_ptr e1, e2, e3;

    // Leader (no actual senders in this simplified test — just 1 sender)
    std::vector<coproto::Socket*> senderPtrs = {&ls_socks[1]};

    std::thread receiverThread([&]() {
        try {
            PRNG prng(block(0, 10));
            recvOut = macoro::sync_wait(MpsOprf::runAsReceiver(
                sockets[1], processedSet, prng));
        } catch (...) { e1 = std::current_exception(); }
    });

    std::thread leaderThread([&]() {
        try {
            PRNG prng(block(0, 20));
            leaderOut = macoro::sync_wait(MpsOprf::runAsLeader(
                sockets[0], senderPtrs, processedSet, 1, prng));
        } catch (...) { e2 = std::current_exception(); }
    });

    std::thread senderThread([&]() {
        try {
            PRNG prng(block(0, 30));
            senderOut = macoro::sync_wait(MpsOprf::runAsSender(
                ls_socks[0], prng));
        } catch (...) { e3 = std::current_exception(); }
    });

    receiverThread.join();
    leaderThread.join();
    senderThread.join();

    if (e1) { try { std::rethrow_exception(e1); } catch (const std::exception& e) { printf("  Receiver: %s\n", e.what()); return; } }
    if (e2) { try { std::rethrow_exception(e2); } catch (const std::exception& e) { printf("  Leader: %s\n", e.what()); return; } }
    if (e3) { try { std::rethrow_exception(e3); } catch (const std::exception& e) { printf("  Sender: %s\n", e.what()); return; } }

    printf("  recvOut.K.size()=%zu, recvOut.m=%zu\n", recvOut.K.size(), recvOut.m);
    printf("  senderOut.C_i.size()=%zu, senderOut.m=%zu\n", senderOut.C_i.size(), senderOut.m);
    printf("  leaderOut.C_shares[0].size()=%zu, leaderOut.m=%zu\n",
           leaderOut.C_shares.empty() ? 0 : leaderOut.C_shares[0].size(), leaderOut.m);

    // Check PRF consistency: F_R(x) should == F_leader(x) XOR F_sender(x)
    // for elements in ALL parties' sets (which they all have the same set here)
    size_t prf_match = 0;
    for (size_t j = 0; j < std::min(m, (size_t)10); j++) {
        block x = processedSet[j];

        block f_r = recvOut.evalPRF(x, recvOut.K);
        block f_leader = leaderOut.evalPRF(x, 0); // share index 0
        block f_sender = senderOut.evalPRF(x);

        block f_combined = f_leader ^ f_sender;

        bool match = (f_r == f_combined);
        if (match) prf_match++;

        if (j < 3 || !match) {
            printf("  [%zu] F_R==F_L^F_S: %s\n", j, match ? "YES" : "NO");
            if (!match) {
                pb("    F_R(x)", f_r);
                pb("    F_L(x)", f_leader);
                pb("    F_S(x)", f_sender);
                pb("    F_L^F_S", f_combined);
            }
        }
    }

    size_t checkCount = std::min(m, (size_t)10);
    printf("  PRF consistency: %zu/%zu PASS\n", prf_match, checkCount);
    printf("  Result: %s\n\n", prf_match == checkCount ? "PASS" : "FAIL");
}

// ==========================================================================
int main() {
    printf("========================================\n");
    printf("MPS-OPRF/MPSI End-to-End Diagnostic\n");
    printf("========================================\n");

    try {
        test_vole_relation();
        test_okvs_linearity();
        test_oprf_consistency();
        test_bicentric_e2e();
    } catch (const std::exception& e) {
        printf("\n*** FATAL: %s ***\n", e.what());
        return 1;
    }

    printf("========================================\n");
    printf("Diagnostic Complete\n");
    printf("========================================\n");
    return 0;
}
