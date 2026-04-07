cmake_minimum_required(VERSION 3.15)
project(mps-oprf-mpsi)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-maes -mpclmul -msse4.1 -mavx -mavx2)

# Dependency paths
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/thirdparty" CACHE PATH "BZS-MPSI source directory")
set(INSTALL_PATH "${DEPS_DIR}/out/install/linux" CACHE PATH "Dependency install prefix")
set(BUILD_PATH "${DEPS_DIR}/out/build/linux" CACHE PATH "Dependency build directory")

# Homomorphic hash extension (off by default — GF128 mode)
option(ENABLE_HOMOMORPHIC_HASH "Enable Homomorphic Hash extension (requires libsodium)" OFF)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${INSTALL_PATH}/include
    ${DEPS_DIR}
    ${DEPS_DIR}/volePSI
)

link_directories(
    ${INSTALL_PATH}/lib
    ${BUILD_PATH}/volePSI
    ${BUILD_PATH}/tests
)

# Source files
set(SOURCES
    src/SimplePRF.cpp
    src/CoinToss.cpp
    src/SecretShare.cpp
    src/VoleWrapper.cpp
    src/MpsOprf.cpp
    src/BicentricMpsi.cpp
    src/RingMpsi.cpp
    src/RandomOracle.cpp
)

# Add HomomorphicPRF.cpp only in HH mode
if(ENABLE_HOMOMORPHIC_HASH)
    list(APPEND SOURCES src/HomomorphicPRF.cpp)
endif()

# Headers
set(HEADERS
    include/HomomorphicPRF.h
    include/SimplePRF.h
    include/CoinToss.h
    include/SecretShare.h
    include/VoleWrapper.h
    include/MpsOprf.h
    include/BicentricMpsi.h
    include/RingMpsi.h
    include/RandomOracle.h
)

# Static library
add_library(mps_oprf_mpsi STATIC ${SOURCES} ${HEADERS})
target_include_directories(mps_oprf_mpsi PUBLIC ${CMAKE_SOURCE_DIR}/include)

if(ENABLE_HOMOMORPHIC_HASH)
    target_compile_definitions(mps_oprf_mpsi PUBLIC ENABLE_HOMOMORPHIC_HASH)
endif()

# Always link sodium — precompiled libcryptoTools.a depends on it
target_link_libraries(mps_oprf_mpsi PUBLIC
    volePSI libOTe bitpolymul cryptoTools coproto macoro
    sodium pthread
)

# Mode-independent test targets

# ============================================================
# Frontend executables
# ============================================================

# Multi-process TCP performance benchmark
add_executable(perf_network frontend/perf_network.cpp)
target_link_libraries(perf_network PRIVATE mps_oprf_mpsi)


# ============================================================
# Install
# ============================================================

set(INSTALL_TARGETS mps_oprf_mpsi perf_network)
if(ENABLE_HOMOMORPHIC_HASH)
    list(APPEND INSTALL_TARGETS mps_oprf_tests mps_oprf_frontend)
endif()
install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES ${HEADERS} DESTINATION include/mps-oprf-mpsi)

message(STATUS "")
message(STATUS "MPS-OPRF-MPSI Configuration:")
message(STATUS "  Dependencies: ${DEPS_DIR}")
message(STATUS "  Install path: ${INSTALL_PATH}")
message(STATUS "  HH mode: ${ENABLE_HOMOMORPHIC_HASH}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
