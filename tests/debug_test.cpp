#include <iostream>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Crypto/RandomOracle.h>

using namespace osuCrypto;

int main() {
    block x1 = toBlock(1);
    
    // 测试1: 直接使用RandomOracle
    block h1, h2;
    {
        RandomOracle ro(sizeof(block));
        ro.Update(x1);
        ro.Final(h1);
    }
    {
        RandomOracle ro(sizeof(block));
        ro.Update(x1);
        ro.Final(h2);
    }
    
    std::cout << "h1: " << h1 << std::endl;
    std::cout << "h2: " << h2 << std::endl;
    std::cout << "Equal: " << (h1 == h2 ? "YES" : "NO") << std::endl;
    
    return 0;
}
