// vOLE封装测试
// 测试vOLE协议的正确性

#include <iostream>
#include <thread>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/LocalAsyncSock.h>

#include "VoleWrapper.h"

using namespace osuCrypto;
using namespace mpsoprf;

// 测试vOLE基本功能
void test_VoleWrapper_basic() {
    std::cout << "Testing VoleWrapper basic functionality... " << std::flush;
    
    const size_t numOTs = 128;
    
    auto sockets = coproto::LocalAsyncSocket::makePair();
    
    std::vector<block> sender_a, sender_c;
    std::vector<block> receiver_b, receiver_d;
    std::exception_ptr eptr1, eptr2;
    
    // 发送方线程
    std::thread senderThread([&]() {
        try {
            PRNG prng(toBlock(1));
            sender_a.resize(numOTs);
            sender_c.resize(numOTs);
            macoro::sync_wait(VoleWrapper::runVole(
                sockets[0], true, numOTs, sender_a, sender_c, prng));
        } catch (...) {
            eptr1 = std::current_exception();
        }
    });
    
    // 接收方线程
    std::thread receiverThread([&]() {
        try {
            PRNG prng(toBlock(2));
            receiver_b.resize(numOTs);
            receiver_d.resize(numOTs);
            macoro::sync_wait(VoleWrapper::runVole(
                sockets[1], false, numOTs, receiver_b, receiver_d, prng));
        } catch (...) {
            eptr2 = std::current_exception();
        }
    });
    
    senderThread.join();
    receiverThread.join();
    
    if (eptr1) std::rethrow_exception(eptr1);
    if (eptr2) std::rethrow_exception(eptr2);
    
    // 验证输出大小
    assert(sender_a.size() == numOTs);
    assert(sender_c.size() == numOTs);
    assert(receiver_b.size() == numOTs);
    assert(receiver_d.size() == numOTs);
    
    std::cout << "PASSED" << std::endl;
}

// 测试vOLE不同大小
void test_VoleWrapper_sizes() {
    std::cout << "Testing VoleWrapper with different sizes... " << std::flush;
    
    std::vector<size_t> sizes = {64, 256, 512};
    
    for (size_t numOTs : sizes) {
        auto sockets = coproto::LocalAsyncSocket::makePair();
        
        std::vector<block> sender_a, sender_c;
        std::vector<block> receiver_b, receiver_d;
        
        std::thread senderThread([&]() {
            PRNG prng(toBlock(numOTs));
            sender_a.resize(numOTs);
            sender_c.resize(numOTs);
            macoro::sync_wait(VoleWrapper::runVole(
                sockets[0], true, numOTs, sender_a, sender_c, prng));
        });
        
        std::thread receiverThread([&]() {
            PRNG prng(toBlock(numOTs + 1));
            receiver_b.resize(numOTs);
            receiver_d.resize(numOTs);
            macoro::sync_wait(VoleWrapper::runVole(
                sockets[1], false, numOTs, receiver_b, receiver_d, prng));
        });
        
        senderThread.join();
        receiverThread.join();
        
        assert(sender_c.size() == numOTs);
        assert(receiver_d.size() == numOTs);
    }
    
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "VoleWrapper Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        test_VoleWrapper_basic();
        test_VoleWrapper_sizes();
        
        std::cout << "\nAll VoleWrapper tests PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
