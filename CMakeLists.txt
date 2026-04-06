cmake_minimum_required(VERSION 3.15)
project(mps-oprf-mpsi)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-maes -mpclmul -msse4.1 -mavx -mavx2)

# Dependency paths
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/thirdparty_bzs" CACHE PATH "BZS-MPSI source directory")
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

# ============================================================
# Test targets
# ============================================================

# HH-only targets (require HomomorphicPRF / libsodium)
if(ENABLE_HOMOMORPHIC_HASH)
    add_executable(mps_oprf_tests tests/main_tests.cpp)
    target_link_libraries(mps_oprf_tests PRIVATE mps_oprf_mpsi)

    add_executable(perf_tests_large tests/Performance_Tests_Large.cpp)
    target_link_libraries(perf_tests_large PRIVATE mps_oprf_mpsi)

    add_executable(perf_tests tests/Performance_Tests.cpp)
    target_link_libraries(perf_tests PRIVATE mps_oprf_mpsi)

    add_executable(perf_tests_protocols tests/Performance_Tests_Protocols.cpp)
    target_link_libraries(perf_tests_protocols PRIVATE mps_oprf_mpsi)

    add_executable(mpsoprf_tests tests/MpsOprf_Tests.cpp)
    target_link_libraries(mpsoprf_tests PRIVATE mps_oprf_mpsi)

    add_executable(mps_oprf_frontend frontend/main.cpp)
    target_link_libraries(mps_oprf_frontend PRIVATE mps_oprf_mpsi)
endif()

# Mode-independent test targets
add_executable(cointoss_tests tests/CoinToss_Tests.cpp)
target_link_libraries(cointoss_tests PRIVATE mps_oprf_mpsi)

add_executable(vole_tests tests/VoleWrapper_Tests.cpp)
target_link_libraries(vole_tests PRIVATE mps_oprf_mpsi)

add_executable(bicentric_tests tests/BicentricMpsi_Tests.cpp)
target_link_libraries(bicentric_tests PRIVATE mps_oprf_mpsi)

add_executable(ring_tests tests/RingMpsi_Tests.cpp)
target_link_libraries(ring_tests PRIVATE mps_oprf_mpsi)

add_executable(security_tests tests/Security_Tests.cpp)
target_link_libraries(security_tests PRIVATE mps_oprf_mpsi)

add_executable(simple_prf_tests tests/SimplePRF_Tests.cpp)
target_link_libraries(simple_prf_tests PRIVATE mps_oprf_mpsi)

# End-to-end diagnostic (real vOLE, local sockets, full protocol)
add_executable(e2e_diagnostic tests/E2E_Diagnostic.cpp)
target_link_libraries(e2e_diagnostic PRIVATE mps_oprf_mpsi)

# ============================================================
# Frontend executables
# ============================================================

# Multi-process TCP performance benchmark
add_executable(perf_network frontend/perf_network.cpp)
target_link_libraries(perf_network PRIVATE mps_oprf_mpsi)

# Debug intersection test (single process, no network)
add_executable(debug_intersection frontend/debug_intersection.cpp)
target_link_libraries(debug_intersection PRIVATE mps_oprf_mpsi)

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
