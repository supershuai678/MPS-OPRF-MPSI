// MPS-OPRF-MPSI Performance Test - BZS Paper Style
// 支持BZS论文参数: m={2^12, 2^16, 2^20}, n={20, 50, 80, 110, 140}

#include <iostream>
#include <chrono>
#include <iomanip>
#include <set>
#include <fstream>
#include <cstring>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include "HomomorphicPRF.h"
#include "SecretShare.h"
#include <volePSI/Paxos.h>

using namespace osuCrypto;
using namespace std::chrono;

std::vector<block> generateSet(size_t size, PRNG& prng, const std::vector<block>& intersection) {
    std::vector<block> set;
    std::set<block> seen;
    for (const auto& elem : intersection) { 
        set.push_back(elem); 
        seen.insert(elem); 
    }
    while (set.size() < size) {
        block r = prng.get<block>();
        if (seen.find(r) == seen.end()) { 
            set.push_back(r); 
            seen.insert(r); 
        }
    }
    return set;
}

struct BenchmarkResult {
    double total_ms;
    double prf_ms;
    double okvs_encode_ms;
    double okvs_decode_ms;
    size_t sender_comm_kb;
    size_t leader_comm_kb;
    size_t receiver_comm_kb;
    size_t total_comm_kb;
    size_t intersection_found;
};

BenchmarkResult benchmark_MPSI(size_t numParties, size_t setSize, size_t intersectionSize, PRNG& prng) {
    BenchmarkResult result = {};
    
    // 生成交集
    std::vector<block> intersection;
    for (size_t i = 0; i < intersectionSize; ++i) {
        intersection.push_back(prng.get<block>());
    }
    
    // 生成各方集合
    std::vector<std::vector<block>> sets(numParties);
    for (size_t i = 0; i < numParties; ++i) {
        sets[i] = generateSet(setSize, prng, intersection);
    }
    
    auto total_start = high_resolution_clock::now();
    
    // PRF计算阶段
    auto prf_start = high_resolution_clock::now();
    
    mpsoprf::Scalar w, a;
    crypto_core_ristretto255_scalar_random(w.data);
    crypto_core_ristretto255_scalar_random(a.data);
    
    block w_block, a_block;
    w.toBytes((unsigned char*)&w_block);
    a.toBytes((unsigned char*)&a_block);
    
    auto w_shares = mpsoprf::SecretShare::split(w_block, numParties - 1, prng);
    auto a_shares = mpsoprf::SecretShare::split(a_block, numParties - 1, prng);
    
    // 每个发送方计算PRF值
    std::vector<std::vector<mpsoprf::Point>> allPRFValues(numParties - 1);
    for (size_t i = 0; i < numParties - 1; ++i) {
        allPRFValues[i].resize(setSize);
        mpsoprf::Scalar wi, ai;
        wi.fromBytes((const unsigned char*)&w_shares[i]);
        ai.fromBytes((const unsigned char*)&a_shares[i]);
        
        for (size_t j = 0; j < setSize; ++j) {
            mpsoprf::Scalar combined;
            crypto_core_ristretto255_scalar_add(combined.data, wi.data, ai.data);
            allPRFValues[i][j] = mpsoprf::HomomorphicPRF::eval(combined, sets[i][j]);
        }
    }
    
    auto prf_end = high_resolution_clock::now();
    result.prf_ms = duration_cast<microseconds>(prf_end - prf_start).count() / 1000.0;
    
    // OKVS编码阶段
    auto encode_start = high_resolution_clock::now();
    
    volePSI::Baxos paxos;
    size_t weight = (setSize > 10000) ? 5 : 3;
    paxos.init(setSize, 40, weight, 8, volePSI::PaxosParam::Binary, prng.get<block>());
    
    std::vector<std::vector<block>> encodedOKVS(numParties - 1);
    for (size_t i = 0; i < numParties - 1; ++i) {
        std::vector<block> values(setSize);
        for (size_t j = 0; j < setSize; ++j) {
            allPRFValues[i][j].toBytes((unsigned char*)&values[j]);
        }
        encodedOKVS[i].resize(paxos.size());
        paxos.solve(span<const block>(sets[i]), span<const block>(values), span<block>(encodedOKVS[i]), &prng);
    }
    
    auto encode_end = high_resolution_clock::now();
    result.okvs_encode_ms = duration_cast<microseconds>(encode_end - encode_start).count() / 1000.0;
    
    // OKVS解码阶段（接收方）
    auto decode_start = high_resolution_clock::now();
    
    const auto& receiverSet = sets[numParties - 1];
    result.intersection_found = 0;
    
    for (size_t j = 0; j < setSize; ++j) {
        mpsoprf::Point fr_y = mpsoprf::HomomorphicPRF::eval(w, receiverSet[j]);
        mpsoprf::Point product = fr_y;
        
        for (size_t i = 0; i < numParties - 1; ++i) {
            block decoded;
            paxos.decode(span<const block>(&receiverSet[j], 1), span<block>(&decoded, 1), 
                        span<const block>(encodedOKVS[i]));
            mpsoprf::Point decoded_point;
            decoded_point.fromBytes((const unsigned char*)&decoded);
            product = mpsoprf::HomomorphicPRF::mulPoints(product, decoded_point);
        }
        
        bool isIdentity = true;
        for (int k = 0; k < crypto_core_ristretto255_BYTES && isIdentity; ++k) {
            if (product.data[k] != 0) isIdentity = false;
        }
        if (isIdentity) result.intersection_found++;
    }
    
    auto decode_end = high_resolution_clock::now();
    result.okvs_decode_ms = duration_cast<microseconds>(decode_end - decode_start).count() / 1000.0;
    
    auto total_end = high_resolution_clock::now();
    result.total_ms = duration_cast<milliseconds>(total_end - total_start).count();
    
    // 计算通信量
    size_t okvs_size = paxos.size() * sizeof(block);
    size_t key_share_size = 2 * crypto_core_ristretto255_SCALARBYTES;
    size_t vole_comm = 2 * setSize * sizeof(block);
    
    result.sender_comm_kb = (key_share_size + okvs_size) / 1024;
    result.leader_comm_kb = (vole_comm + (numParties - 1) * key_share_size + okvs_size) / 1024;
    result.receiver_comm_kb = (vole_comm + (numParties - 1) * okvs_size) / 1024;
    result.total_comm_kb = result.leader_comm_kb + (numParties - 2) * result.sender_comm_kb + result.receiver_comm_kb;
    
    return result;
}

void run_single_test(size_t n, size_t m) {
    PRNG prng(sysRandomSeed());
    size_t intersectionSize = m / 10;
    
    std::cerr << "Running test: n=" << n << ", m=" << m << std::endl;
    
    auto result = benchmark_MPSI(n, m, intersectionSize, prng);
    
    // 输出格式: n=X, m=Y: Zms, WKB
    std::cout << "n=" << std::setw(3) << n << ", m=" << std::setw(7) << m 
              << ": " << std::setw(8) << (int)result.total_ms << "ms, " 
              << std::setw(8) << result.total_comm_kb << "KB" << std::endl;
}

void run_bzs_benchmark(const std::string& outputFile) {
    std::ofstream csv(outputFile);
    csv << "protocol,n,m,total_ms,prf_ms,okvs_encode_ms,okvs_decode_ms,"
        << "sender_comm_kb,leader_comm_kb,receiver_comm_kb,total_comm_kb\n";
    
    PRNG prng(sysRandomSeed());
    
    // BZS论文参数
    std::vector<size_t> partyCounts = {20, 50, 80, 110, 140};
    std::vector<size_t> setSizes = {4096, 65536, 1048576};  // 2^12, 2^16, 2^20
    
    std::cout << "========================================" << std::endl;
    std::cout << "MPS-OPRF-MPSI BZS-Style Benchmark" << std::endl;
    std::cout << "Parameters: n={20,50,80,110,140}, m={2^12, 2^16, 2^20}" << std::endl;
    std::cout << "========================================" << std::endl;
    
    for (size_t m : setSizes) {
        std::cout << "\n--- Set Size m = " << m << " (2^" << (size_t)log2(m) << ") ---" << std::endl;
        
        for (size_t n : partyCounts) {
            size_t intersectionSize = m / 10;
            
            std::cout << "  n=" << std::setw(3) << n << ": " << std::flush;
            
            auto result = benchmark_MPSI(n, m, intersectionSize, prng);
            
            std::cout << std::setw(8) << (int)result.total_ms << "ms, " 
                      << std::setw(8) << result.total_comm_kb << "KB" << std::endl;
            
            csv << "MPS-OPRF-MPSI," << n << "," << m << ","
                << result.total_ms << "," << result.prf_ms << ","
                << result.okvs_encode_ms << "," << result.okvs_decode_ms << ","
                << result.sender_comm_kb << "," << result.leader_comm_kb << ","
                << result.receiver_comm_kb << "," << result.total_comm_kb << "\n";
            csv.flush();
        }
    }
    
    csv.close();
    std::cout << "\nResults saved to " << outputFile << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc >= 4 && std::string(argv[1]) == "--single") {
        // 单次测试模式: --single n m
        size_t n = std::stoull(argv[2]);
        size_t m = std::stoull(argv[3]);
        run_single_test(n, m);
    } else if (argc >= 2 && std::string(argv[1]) == "--bzs") {
        // BZS完整测试
        std::string outputFile = (argc >= 3) ? argv[2] : "mps_oprf_bzs_benchmark.csv";
        run_bzs_benchmark(outputFile);
    } else {
        std::cout << "Usage:" << std::endl;
        std::cout << "  " << argv[0] << " --single <n> <m>   # Run single test" << std::endl;
        std::cout << "  " << argv[0] << " --bzs [output.csv] # Run BZS-style benchmark" << std::endl;
        return 1;
    }
    
    return 0;
}
