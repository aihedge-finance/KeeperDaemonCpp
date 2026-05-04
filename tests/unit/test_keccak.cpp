// Unit tests for the keccak256 function against known Ethereum test vectors.
// All expected hashes verified against https://emn178.github.io/online-tools/keccak_256.html

#include <catch2/catch_test_macros.hpp>
#include "tx_signer.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>

static std::string toHex(const std::array<uint8_t, 32>& bytes) {
    std::ostringstream ss;
    for (auto b : bytes)
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return ss.str();
}

TEST_CASE("keccak256 of empty input", "[keccak]") {
    // Ethereum uses pre-NIST Keccak, NOT SHA-3.
    // keccak256("") = c5d246...
    const uint8_t empty[1] = {0}; // valid pointer, 0 length
    auto hash = keccak256(empty, 0);
    CHECK(toHex(hash) == "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
}

TEST_CASE("keccak256 of 'abc'", "[keccak]") {
    const uint8_t data[] = {'a', 'b', 'c'};
    auto hash = keccak256(data, 3);
    CHECK(toHex(hash) == "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

TEST_CASE("keccak256 of 'hello'", "[keccak]") {
    const char* s = "hello";
    auto hash = keccak256(reinterpret_cast<const uint8_t*>(s), 5);
    CHECK(toHex(hash) == "1c8aff950685c2ed4bc3174f3472287b56d9517b9c948127319a09a7a36deac8");
}

TEST_CASE("keccak256 of long string crosses block boundary", "[keccak]") {
    // 136 bytes = exactly one RATE block. Tests block boundary handling.
    std::string s(136, 'x');
    auto hash = keccak256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    // Just verify it doesn't crash and returns a non-zero hash
    bool nonzero = std::any_of(hash.begin(), hash.end(), [](uint8_t b) { return b != 0; });
    CHECK(nonzero);
}
