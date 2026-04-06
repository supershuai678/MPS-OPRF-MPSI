// CoinToss 单元测试
// 测试抛硬币协议的承诺方案和两方协议

#include <iostream>
#include <thread>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/Socket/LocalAsyncSock.h>

#include "CoinToss.h"

using namespace osuCrypto;
using namespace mpsoprf;

// 测试承诺方案
void test_CoinToss_commitment() {
    std::cout << "Testing CoinToss commitment... ";
    
    PRNG prng(sysRandomSeed());
    block value = prng.get<block>();
    block randomness = prng.get<block>();
    
    // 创建承诺
    block commitment = CoinToss::commit(value, randomness);
    
    // 验证正确的打开
    assert(CoinToss::verify(commitment, value, randomness));
    
    // 验证错误的值不能通过
    block wrongValue = prng.get<block>();
    assert(!CoinToss::verify(commitment, wrongValue, randomness));
    
    // 验证错误的随机数不能通过
    block wrongRandomness = prng.get<block>();
    assert(!CoinToss::verify(commitment, value, wrongRandomness));
    
    std::cout << "PASSED" << std::endl;
}

// 测试两方协议
void test_CoinToss_protocol() {
    std::cout << "Testing CoinToss two-party protocol... ";
    
    // 创建本地socket对
    auto sockets = coproto::LocalAsyncSocket::makePair();
    
    block result1, result2;
    std::exception_ptr eptr1, eptr2;
    
    // 启动两个线程执行协议
    std::thread initiator([&]() {
        try {
            PRNG prng(toBlock(1));
            result1 = macoro::sync_wait(CoinToss::execute(sockets[0], prng, true));
        } catch (...) {
            eptr1 = std::current_exception();
        }
    });
    
    std::thread responder([&]() {
        try {
            PRNG prng(toBlock(2));
            result2 = macoro::sync_wait(CoinToss::execute(sockets[1], prng, false));
        } catch (...) {
            eptr2 = std::current_exception();
        }
    });
    
    initiator.join();
    responder.join();
    
    // 检查异常
    if (eptr1) std::rethrow_exception(eptr1);
    if (eptr2) std::rethrow_exception(eptr2);
    
    // 验证双方获得相同结果
    assert(result1 == result2);
    
    std::cout << "PASSED" << std::endl;
}

// 测试随机性
void test_CoinToss_randomness() {
    std::cout << "Testing CoinToss randomness... ";
    
    const int numTrials = 10;
    std::vector<block> results;
    
    for (int i = 0; i < numTrials; ++i) {
        auto sockets = coproto::LocalAsyncSocket::makePair();
        
        block result1, result2;
        
        std::thread initiator([&]() {
            PRNG prng(toBlock(i * 2));
            result1 = macoro::sync_wait(CoinToss::execute(sockets[0], prng, true));
        });
        
        std::thread responder([&]() {
            PRNG prng(toBlock(i * 2 + 1));
            result2 = macoro::sync_wait(CoinToss::execute(sockets[1], prng, false));
        });
        
        initiator.join();
        responder.join();
        
        assert(result1 == result2);
        results.push_back(result1);
    }
    
    // 验证结果不全相同（随机性）
    bool allSame = true;
    for (int i = 1; i < numTrials; ++i) {
        if (results[i] != results[0]) {
            allSame = false;
            break;
        }
    }
    assert(!allSame);
    
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "CoinToss Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        test_CoinToss_commitment();
        test_CoinToss_protocol();
        test_CoinToss_randomness();
        
        std::cout << "\nAll CoinToss tests PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
