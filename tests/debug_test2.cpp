#include <iostream>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include "HomomorphicPRF.h"

using namespace osuCrypto;
using namespace mpsoprf;

int main() {
    block x1 = toBlock(1);
    
    std::cout << "x1: " << x1 << std::endl;
    
    // 测试HomomorphicPRF::hashToGroup
    auto h1 = HomomorphicPRF::hashToGroup(x1);
    auto h2 = HomomorphicPRF::hashToGroup(x1);
    
    std::cout << "h1: " << h1 << std::endl;
    std::cout << "h2: " << h2 << std::endl;
    std::cout << "Equal (==): " << (h1 == h2 ? "YES" : "NO") << std::endl;
    std::cout << "Equal (equalPoints): " << (HomomorphicPRF::equalPoints(h1, h2) ? "YES" : "NO") << std::endl;
    
    return 0;
}
