// MPS-OPRF-MPSI Multi-process TCP Performance Benchmark
//
// Usage: ./perf_network -nu <n> -t <t> -id <id> -nn <log2_m> -proto <bc|ring> [-net <lan|wan>] [-ip <addr>] [-port <base>]
//
// Role assignment (BZS-MPSI compatible):
//   ID = n-1:     Receiver (R)
//   ID = n-2:     Leader (S_n)
//   ID = 0 ~ n-3: Sender (S_1 ~ S_{n-2})
//
// Port assignment:
//   Receiver-Leader:    port_base
//   Receiver-Sender[i]: port_base + 100 + i
//   Leader-Sender[i]:   port_base + 500 + i
//   Ring Sender[i]-Sender[i+1]: port_base + 800 + i
//
// Example (4-party, m=4096, bicentric):
//   Terminal 0: ./perf_network -nu 4 -id 0 -nn 12 -proto bc
//   Terminal 1: ./perf_network -nu 4 -id 1 -nn 12 -proto bc
//   Terminal 2: ./perf_network -nu 4 -id 2 -nn 12 -proto bc
//   Terminal 3: ./perf_network -nu 4 -id 3 -nn 12 -proto bc

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iomanip>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/AsioSocket.h>
#include "MpsOprf.h"
#include "BicentricMpsi.h"
#include "RingMpsi.h"

using namespace osuCrypto;
using namespace mpsoprf;

struct SocketCounters {
    size_t bytesSent = 0;
    size_t bytesReceived = 0;
};

static constexpr u64 kReadyTag = 0x52454144595f5f31ull; // "READY__1"
static constexpr u64 kStartTag = 0x53544152545f5f31ull; // "START__1"

// Create a TCP connection pair: higher ID listens, lower ID connects
static coproto::Socket connectTcp(const std::string& ip, size_t port, size_t myId, size_t otherId) {
    std::string addr = ip + ":" + std::to_string(port);
    if (myId > otherId) {
        // server
        return coproto::asioConnect(addr, true);
    }

    // client — retry until the listening side is ready
    constexpr size_t kMaxAttempts = 40;
    constexpr auto kRetryDelay = std::chrono::milliseconds(100);
    for (size_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        try {
            return coproto::asioConnect(addr, false);
        } catch (const std::exception&) {
            if (attempt + 1 == kMaxAttempts)
                throw;
            std::this_thread::sleep_for(kRetryDelay);
        }
    }

    throw std::runtime_error("failed to connect to " + addr);
}

static void sendControlTag(coproto::Socket& sock, u64 tag) {
    macoro::sync_wait(sock.send(tag));
}

static void waitForControlTag(coproto::Socket& sock, u64 expectedTag, const char* phase) {
    u64 actualTag = 0;
    macoro::sync_wait(sock.recv(actualTag));
    if (actualTag != expectedTag)
        throw std::runtime_error(std::string("unexpected ") + phase + " tag before timing start");
}

static void sendReadySignal(coproto::Socket& sock) {
    sendControlTag(sock, kReadyTag);
}

static void waitForReadySignal(coproto::Socket& sock) {
    waitForControlTag(sock, kReadyTag, "ready");
}

static void sendStartSignal(coproto::Socket& sock) {
    sendControlTag(sock, kStartTag);
}

static void waitForStartSignal(coproto::Socket& sock) {
    waitForControlTag(sock, kStartTag, "start");
}

static SocketCounters snapshotCounters(coproto::Socket& sock) {
    return {sock.bytesSent(), sock.bytesReceived()};
}

int main(int argc, char* argv[]) {
    CLP cmd(argc, argv);

    size_t n = cmd.getOr("nu", 4);
    size_t t = cmd.getOr("t", 1);
    size_t id = cmd.getOr("id", 0);
    size_t nn = cmd.getOr("nn", 12);
    std::string proto = cmd.getOr<std::string>("proto", "bc");
    std::string net = cmd.getOr<std::string>("net", "lan");
    std::string ip = cmd.getOr<std::string>("ip", "127.0.0.1");
    size_t portBase = cmd.getOr("port", 20000);

    size_t m = 1ull << nn;
    size_t receiverId = n - 1;
    size_t leaderId = n - 2;

    // Generate test set
    PRNG prng(oc::sysRandomSeed());
    std::vector<block> mySet(m);
    // Common intersection: first m/2 elements are shared
    for (size_t i = 0; i < m / 2; i++)
        mySet[i] = block(0, i);
    // Private elements: remaining random
    for (size_t i = m / 2; i < m; i++)
        mySet[i] = prng.get<block>();

    std::chrono::high_resolution_clock::time_point startTime;

    try {
        if (proto == "bc") {
            // ============================================================
            // Bicentric (star) topology
            // ============================================================

            if (id == receiverId) {
                // Receiver
                auto leaderSock = connectTcp(ip, portBase, id, leaderId);
                std::vector<coproto::Socket> senderSocks;
                std::vector<coproto::Socket*> senderPtrs;
                for (size_t i = 0; i < n - 2; i++) { // senders 0..n-3
                    senderSocks.push_back(connectTcp(ip, portBase + 100 + i, id, i));
                }
                for (auto& s : senderSocks) senderPtrs.push_back(&s);

                sendReadySignal(leaderSock);
                waitForStartSignal(leaderSock);

                auto leaderBase = snapshotCounters(leaderSock);
                std::vector<SocketCounters> senderBases;
                senderBases.reserve(senderSocks.size());
                for (auto& s : senderSocks)
                    senderBases.push_back(snapshotCounters(s));

                startTime = std::chrono::high_resolution_clock::now();
                auto result = macoro::sync_wait(BicentricMpsi::runAsReceiver(
                    leaderSock, senderPtrs, mySet, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();

                size_t bytesSent = leaderSock.bytesSent() - leaderBase.bytesSent;
                size_t bytesRecv = leaderSock.bytesReceived() - leaderBase.bytesReceived;
                for (size_t i = 0; i < senderSocks.size(); ++i) {
                    bytesSent += senderSocks[i].bytesSent() - senderBases[i].bytesSent;
                    bytesRecv += senderSocks[i].bytesReceived() - senderBases[i].bytesReceived;
                }
                double commKB = (bytesSent + bytesRecv) / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Receiver"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | ReceiverActive: " << std::fixed << std::setprecision(3) << result.activePhaseSeconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << " | Intersection: " << result.intersection.size()
                          << std::endl;

                // Intersection verification
                std::cout << "  Expected intersection: ~" << m / 2 << " elements" << std::endl;
                if (result.intersection.size() == m / 2)
                    std::cout << "  Verification: PASS" << std::endl;
                else
                    std::cout << "  Verification: MISMATCH (got " << result.intersection.size()
                              << ", expected " << m / 2 << ")" << std::endl;

                // Print first few intersection elements
                size_t printCount = std::min(result.intersection.size(), (size_t)5);
                for (size_t i = 0; i < printCount; i++) {
                    auto* v = (const uint64_t*)&result.intersection[i];
                    std::cout << "    [" << i << "] = (" << v[0] << ", " << v[1] << ")" << std::endl;
                }

            } else if (id == leaderId) {
                // Leader
                auto receiverSock = connectTcp(ip, portBase, id, receiverId);
                std::vector<coproto::Socket> senderSocks;
                std::vector<coproto::Socket*> senderPtrs;
                for (size_t i = 0; i < n - 2; i++) {
                    senderSocks.push_back(connectTcp(ip, portBase + 500 + i, id, i));
                }
                for (auto& s : senderSocks) senderPtrs.push_back(&s);

                waitForReadySignal(receiverSock);
                for (auto& s : senderSocks)
                    waitForReadySignal(s);

                sendStartSignal(receiverSock);
                for (auto& s : senderSocks)
                    sendStartSignal(s);

                auto receiverBase = snapshotCounters(receiverSock);
                std::vector<SocketCounters> senderBases;
                senderBases.reserve(senderSocks.size());
                for (auto& s : senderSocks)
                    senderBases.push_back(snapshotCounters(s));

                startTime = std::chrono::high_resolution_clock::now();
                macoro::sync_wait(BicentricMpsi::runAsLeader(
                    receiverSock, senderPtrs, mySet, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();

                size_t bytesSent = receiverSock.bytesSent() - receiverBase.bytesSent;
                size_t bytesRecv = receiverSock.bytesReceived() - receiverBase.bytesReceived;
                for (size_t i = 0; i < senderSocks.size(); ++i) {
                    bytesSent += senderSocks[i].bytesSent() - senderBases[i].bytesSent;
                    bytesRecv += senderSocks[i].bytesReceived() - senderBases[i].bytesReceived;
                }
                double commKB = (bytesSent + bytesRecv) / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Leader (ID=" << id << ")"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;

            } else {
                // Sender
                auto receiverSock = connectTcp(ip, portBase + 100 + id, id, receiverId);
                auto leaderSock = connectTcp(ip, portBase + 500 + id, id, leaderId);

                sendReadySignal(leaderSock);
                waitForStartSignal(leaderSock);

                auto receiverBase = snapshotCounters(receiverSock);
                auto leaderBase = snapshotCounters(leaderSock);

                startTime = std::chrono::high_resolution_clock::now();
                auto senderMetrics = macoro::sync_wait(BicentricMpsi::runAsSender(
                    receiverSock, leaderSock, mySet, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();
                double clientActiveSeconds = senderMetrics.clientPhaseSeconds;

                size_t bytesSent = (receiverSock.bytesSent() - receiverBase.bytesSent)
                                 + (leaderSock.bytesSent() - leaderBase.bytesSent);
                size_t bytesRecv = (receiverSock.bytesReceived() - receiverBase.bytesReceived)
                                 + (leaderSock.bytesReceived() - leaderBase.bytesReceived);
                double commKB = (bytesSent + bytesRecv) / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Sender (ID=" << id << ")"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | ClientActive: " << std::fixed << std::setprecision(3) << clientActiveSeconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;
            }

        } else if (proto == "ring") {
            // ============================================================
            // Ring topology
            // S_n -> S_{n-1} -> ... -> S_2 -> S_1 -> R
            // ============================================================

            if (id == receiverId) {
                // Receiver: MPS-OPRF with leader, receive T_1 from S_1
                auto leaderSock = connectTcp(ip, portBase, id, leaderId);
                // prevChannel from S_1 (sender ID 0)
                auto s1Sock = connectTcp(ip, portBase + 100, id, 0);

                sendReadySignal(leaderSock);
                waitForStartSignal(leaderSock);

                auto leaderBase = snapshotCounters(leaderSock);
                auto s1Base = snapshotCounters(s1Sock);

                startTime = std::chrono::high_resolution_clock::now();
                auto result = macoro::sync_wait(RingMpsi::runAsReceiver(
                    s1Sock, leaderSock, mySet, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();

                size_t totalBytes = (leaderSock.bytesSent() - leaderBase.bytesSent)
                                  + (leaderSock.bytesReceived() - leaderBase.bytesReceived)
                                  + (s1Sock.bytesSent() - s1Base.bytesSent)
                                  + (s1Sock.bytesReceived() - s1Base.bytesReceived);
                double commKB = totalBytes / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Receiver"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | ReceiverActive: " << std::fixed << std::setprecision(3) << result.activePhaseSeconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << " | Intersection: " << result.intersection.size()
                          << std::endl;

                // Intersection verification
                std::cout << "  Expected intersection: ~" << m / 2 << " elements" << std::endl;
                if (result.intersection.size() == m / 2)
                    std::cout << "  Verification: PASS" << std::endl;
                else
                    std::cout << "  Verification: MISMATCH (got " << result.intersection.size()
                              << ", expected " << m / 2 << ")" << std::endl;

                size_t printCount = std::min(result.intersection.size(), (size_t)5);
                for (size_t i = 0; i < printCount; i++) {
                    auto* v = (const uint64_t*)&result.intersection[i];
                    std::cout << "    [" << i << "] = (" << v[0] << ", " << v[1] << ")" << std::endl;
                }

            } else if (id == leaderId) {
                // Leader S_n: MPS-OPRF + send T_n to S_{n-1}
                auto receiverSock = connectTcp(ip, portBase, id, receiverId);

                // Sender channels for MPS-OPRF share distribution
                std::vector<coproto::Socket> senderSocks;
                std::vector<coproto::Socket*> senderPtrs;
                for (size_t i = 0; i < n - 2; i++) {
                    senderSocks.push_back(connectTcp(ip, portBase + 500 + i, id, i));
                }
                for (auto& s : senderSocks) senderPtrs.push_back(&s);

                // nextChannel to S_{n-1} (sender index n-3)
                size_t nextSenderId = n - 3; // last sender in ring
                auto nextSock = connectTcp(ip, portBase + 800 + nextSenderId, id, nextSenderId);

                waitForReadySignal(receiverSock);
                for (auto& s : senderSocks)
                    waitForReadySignal(s);

                sendStartSignal(receiverSock);
                for (auto& s : senderSocks)
                    sendStartSignal(s);

                auto receiverBase = snapshotCounters(receiverSock);
                std::vector<SocketCounters> senderBases;
                senderBases.reserve(senderSocks.size());
                for (auto& s : senderSocks)
                    senderBases.push_back(snapshotCounters(s));
                auto nextBase = snapshotCounters(nextSock);

                startTime = std::chrono::high_resolution_clock::now();
                macoro::sync_wait(RingMpsi::runAsLeader(
                    nextSock, receiverSock, senderPtrs, mySet, n - 2, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();

                size_t totalBytes = (receiverSock.bytesSent() - receiverBase.bytesSent)
                                  + (receiverSock.bytesReceived() - receiverBase.bytesReceived)
                                  + (nextSock.bytesSent() - nextBase.bytesSent)
                                  + (nextSock.bytesReceived() - nextBase.bytesReceived);
                for (size_t i = 0; i < senderSocks.size(); ++i) {
                    totalBytes += (senderSocks[i].bytesSent() - senderBases[i].bytesSent)
                                + (senderSocks[i].bytesReceived() - senderBases[i].bytesReceived);
                }
                double commKB = totalBytes / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Leader (ID=" << id << ")"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;

            } else if (id == 0) {
                // First sender S_1: receives T_prev from S_2, sends T_1 to R
                auto leaderSock = connectTcp(ip, portBase + 500 + id, id, leaderId);
                // prevChannel from S_2 (sender ID 1)
                coproto::Socket prevSock;
                if (n > 3) {
                    prevSock = connectTcp(ip, portBase + 800 + id, id, 1);
                } else {
                    // If n==3, S_1's prev is leader S_n directly
                    prevSock = connectTcp(ip, portBase + 800 + id, id, leaderId);
                }
                // nextChannel to R
                auto nextSock = connectTcp(ip, portBase + 100, id, receiverId);

                sendReadySignal(leaderSock);
                waitForStartSignal(leaderSock);

                auto leaderBase = snapshotCounters(leaderSock);
                auto prevBase = snapshotCounters(prevSock);
                auto nextBase = snapshotCounters(nextSock);

                startTime = std::chrono::high_resolution_clock::now();
                auto senderMetrics = macoro::sync_wait(RingMpsi::runAsFirstSender(
                    leaderSock, prevSock, nextSock, mySet, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();
                double clientActiveSeconds = senderMetrics.clientPhaseSeconds;

                size_t totalBytes = (leaderSock.bytesSent() - leaderBase.bytesSent)
                                  + (leaderSock.bytesReceived() - leaderBase.bytesReceived)
                                  + (prevSock.bytesSent() - prevBase.bytesSent)
                                  + (prevSock.bytesReceived() - prevBase.bytesReceived)
                                  + (nextSock.bytesSent() - nextBase.bytesSent)
                                  + (nextSock.bytesReceived() - nextBase.bytesReceived);
                double commKB = totalBytes / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: S_1 (ID=" << id << ")"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | ClientActive: " << std::fixed << std::setprecision(3) << clientActiveSeconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;

            } else {
                // Middle sender S_i (id in [1, n-3])
                auto leaderSock = connectTcp(ip, portBase + 500 + id, id, leaderId);
                // prevChannel: from S_{i+1} or from leader
                coproto::Socket prevSock;
                size_t prevId = (id + 1 < n - 2) ? id + 1 : leaderId;
                prevSock = connectTcp(ip, portBase + 800 + id, id, prevId);
                // nextChannel: to S_{i-1}
                auto nextSock = connectTcp(ip, portBase + 800 + (id - 1), id, id - 1);

                sendReadySignal(leaderSock);
                waitForStartSignal(leaderSock);

                auto leaderBase = snapshotCounters(leaderSock);
                auto prevBase = snapshotCounters(prevSock);
                auto nextBase = snapshotCounters(nextSock);

                startTime = std::chrono::high_resolution_clock::now();
                auto senderMetrics = macoro::sync_wait(RingMpsi::runAsSender(
                    prevSock, nextSock, leaderSock, mySet, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();
                double clientActiveSeconds = senderMetrics.clientPhaseSeconds;

                size_t totalBytes = (leaderSock.bytesSent() - leaderBase.bytesSent)
                                  + (leaderSock.bytesReceived() - leaderBase.bytesReceived)
                                  + (prevSock.bytesSent() - prevBase.bytesSent)
                                  + (prevSock.bytesReceived() - prevBase.bytesReceived)
                                  + (nextSock.bytesSent() - nextBase.bytesSent)
                                  + (nextSock.bytesReceived() - nextBase.bytesReceived);
                double commKB = totalBytes / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: S_" << (id + 1) << " (ID=" << id << ")"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | ClientActive: " << std::fixed << std::setprecision(3) << clientActiveSeconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;
            }
        } else if (proto == "oprf") {
            // ============================================================
            // Standalone MPS-OPRF (no intersection, OPRF only)
            // Connections: Receiver-Leader + Leader-Sender[i]
            // ============================================================

            if (id == receiverId) {
                // Receiver: only talks to Leader
                auto leaderSock = connectTcp(ip, portBase, id, leaderId);

                sendReadySignal(leaderSock);
                waitForStartSignal(leaderSock);
                auto leaderBase = snapshotCounters(leaderSock);
                startTime = std::chrono::high_resolution_clock::now();
                auto result = macoro::sync_wait(MpsOprf::runAsReceiver(
                    leaderSock, mySet, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();

                size_t totalBytes = (leaderSock.bytesSent() - leaderBase.bytesSent)
                                  + (leaderSock.bytesReceived() - leaderBase.bytesReceived);
                double commKB = totalBytes / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Receiver"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;

            } else if (id == leaderId) {
                // Leader: talks to Receiver + all Senders
                auto receiverSock = connectTcp(ip, portBase, id, receiverId);

                std::vector<coproto::Socket> senderSocks;
                std::vector<coproto::Socket*> senderPtrs;
                for (size_t i = 0; i < n - 2; i++) {
                    senderSocks.push_back(connectTcp(ip, portBase + 500 + i, id, i));
                }
                for (auto& s : senderSocks) senderPtrs.push_back(&s);

                waitForReadySignal(receiverSock);
                for (auto& s : senderSocks)
                    waitForReadySignal(s);

                sendStartSignal(receiverSock);
                for (auto& s : senderSocks)
                    sendStartSignal(s);

                auto receiverBase = snapshotCounters(receiverSock);
                std::vector<SocketCounters> senderBases;
                senderBases.reserve(senderSocks.size());
                for (auto& s : senderSocks)
                    senderBases.push_back(snapshotCounters(s));

                startTime = std::chrono::high_resolution_clock::now();
                auto result = macoro::sync_wait(MpsOprf::runAsLeader(
                    receiverSock, senderPtrs, mySet, n - 2, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();

                size_t totalBytes = (receiverSock.bytesSent() - receiverBase.bytesSent)
                                  + (receiverSock.bytesReceived() - receiverBase.bytesReceived);
                for (size_t i = 0; i < senderSocks.size(); ++i) {
                    totalBytes += (senderSocks[i].bytesSent() - senderBases[i].bytesSent)
                                + (senderSocks[i].bytesReceived() - senderBases[i].bytesReceived);
                }
                double commKB = totalBytes / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Leader (ID=" << id << ")"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;

            } else {
                // Sender: only talks to Leader
                auto leaderSock = connectTcp(ip, portBase + 500 + id, id, leaderId);

                sendReadySignal(leaderSock);
                waitForStartSignal(leaderSock);
                auto leaderBase = snapshotCounters(leaderSock);
                startTime = std::chrono::high_resolution_clock::now();
                auto result = macoro::sync_wait(MpsOprf::runAsSender(
                    leaderSock, prng));

                auto endTime = std::chrono::high_resolution_clock::now();
                double seconds = std::chrono::duration<double>(endTime - startTime).count();
                double clientActiveSeconds = result.activeSeconds;

                size_t totalBytes = (leaderSock.bytesSent() - leaderBase.bytesSent)
                                  + (leaderSock.bytesReceived() - leaderBase.bytesReceived);
                double commKB = totalBytes / 1024.0;

                std::cout << "Setting: " << net
                          << " | Protocol: " << proto
                          << " | m: 2^" << nn
                          << " | (n,t): (" << n << "," << t << ")"
                          << " | Role: Sender (ID=" << id << ")"
                          << " | Time: " << std::fixed << std::setprecision(3) << seconds << "s"
                          << " | ClientActive: " << std::fixed << std::setprecision(3) << clientActiveSeconds << "s"
                          << " | Comm: " << std::fixed << std::setprecision(2) << commKB << " KB"
                          << std::endl;
            }

        } else {
            std::cerr << "Unknown protocol: " << proto << ". Use 'bc', 'ring', or 'oprf'." << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error (ID=" << id << "): " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
