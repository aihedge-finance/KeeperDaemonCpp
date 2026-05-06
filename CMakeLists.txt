cmake_minimum_required(VERSION 3.25)
project(curve_keeper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Conan-managed packages ────────────────────────────────────────────────────
find_package(Boost REQUIRED COMPONENTS system json)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Catch2 REQUIRED)

# ── FetchContent: libsecp256k1 (not in ConanCenter reliably) ─────────────────
include(FetchContent)

FetchContent_Declare(
    secp256k1
    GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
    GIT_TAG        v0.5.1
)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(secp256k1)

# ── FetchContent: intx (256-bit integers for RLP) ────────────────────────────
FetchContent_Declare(
    intx
    GIT_REPOSITORY https://github.com/chfast/intx.git
    GIT_TAG        v0.11.0
)
FetchContent_MakeAvailable(intx)

# ── Shared library of testable sources (excludes main.cpp and keeper.cpp) ────
# This allows both curve_keeper and run_tests to link the same compiled objects.
set(CORE_SOURCES
    src/config.cpp
    src/eth_client.cpp
    src/tx_signer.cpp
    src/state_store.cpp
)

set(COMMON_LIBS
    Boost::system
    Boost::json
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    secp256k1
    intx::intx
)

# ── Main daemon executable ────────────────────────────────────────────────────
add_executable(curve_keeper
    src/main.cpp
    src/keeper.cpp
    ${CORE_SOURCES}
)
target_compile_options(curve_keeper PRIVATE -Wall -Wextra -O2)
target_include_directories(curve_keeper PRIVATE src)
target_link_libraries(curve_keeper PRIVATE ${COMMON_LIBS})

# ── Unit test executable ──────────────────────────────────────────────────────
add_executable(run_tests
    tests/unit/test_keccak.cpp
    tests/unit/test_tx_signer.cpp
    tests/unit/test_state_store.cpp
    tests/unit/test_config.cpp
    tests/unit/test_harvest_check.cpp
    ${CORE_SOURCES}
)
target_compile_options(run_tests PRIVATE -Wall -Wextra -O2)
target_include_directories(run_tests PRIVATE src)
target_link_libraries(run_tests PRIVATE
    Catch2::Catch2WithMain
    ${COMMON_LIBS}
)

include(CTest)
include(Catch)
catch_discover_tests(run_tests)

# Export compile_commands.json for clangd IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
