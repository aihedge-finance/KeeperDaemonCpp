// Unit tests for TxSigner: address derivation and EIP-1559 transaction signing.
//
// Private key 0x00...01 is a well-known test key. Its address is documented at:
//   https://learnmeabitcoin.com/tools/private-key/
//   Expected address: 0x7e5f4552091a69125d5dfcb7b8c2659029395bdf (lowercase)

#include <catch2/catch_test_macros.hpp>
#include "tx_signer.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

static const std::string PRIVKEY_ONE =
    "0x0000000000000000000000000000000000000000000000000000000000000001";

// ── Address derivation ────────────────────────────────────────────────────────

TEST_CASE("TxSigner derives correct address from private key 1", "[signer]") {
    TxSigner signer(PRIVKEY_ONE);
    std::string addr = signer.getAddress();
    std::transform(addr.begin(), addr.end(), addr.begin(), ::tolower);
    CHECK(addr == "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf");
}

TEST_CASE("TxSigner getAddress returns 0x-prefixed 42-char string", "[signer]") {
    TxSigner signer(PRIVKEY_ONE);
    CHECK(signer.getAddress().size() == 42);
    CHECK(signer.getAddress().substr(0, 2) == "0x");
}

TEST_CASE("TxSigner throws on invalid private key", "[signer]") {
    CHECK_THROWS(TxSigner("0xdeadbeef")); // too short
}

// ── EIP-1559 transaction structure ────────────────────────────────────────────

TEST_CASE("EIP-1559 signed tx starts with 0x02 type byte", "[signer]") {
    TxSigner signer(PRIVKEY_ONE);
    const std::vector<uint8_t> calldata = {0x51, 0xcf, 0xf8, 0xd9}; // report()
    auto raw = signer.signTransaction(
        /*nonce*/    0,
        /*priority*/ 2'000'000'000ULL,   // 2 gwei
        /*maxFee*/   100'000'000'000ULL,  // 100 gwei
        /*gasLimit*/ 200'000,
        /*to*/       "0x80cbe6dac50064d2d695be7f3a3f580776d69224",
        calldata,
        /*chainId*/  1
    );
    // "0x02..." — EIP-2718 type 2 prefix
    REQUIRE(raw.size() > 4);
    CHECK(raw.substr(0, 4) == "0x02");
}

TEST_CASE("EIP-1559 signed tx is valid hex", "[signer]") {
    TxSigner signer(PRIVKEY_ONE);
    const std::vector<uint8_t> calldata = {0x51, 0xcf, 0xf8, 0xd9};
    auto raw = signer.signTransaction(0, 2'000'000'000ULL, 100'000'000'000ULL,
                                      200'000, "0x80cbe6dac50064d2d695be7f3a3f580776d69224",
                                      calldata, 1);
    // Strip "0x" and verify all characters are hex digits
    std::string hex = raw.substr(2);
    bool valid = std::all_of(hex.begin(), hex.end(), [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c));
    });
    CHECK(valid);
}

TEST_CASE("EIP-1559 different nonces produce different signed txs", "[signer]") {
    TxSigner signer(PRIVKEY_ONE);
    const std::vector<uint8_t> calldata = {0x51, 0xcf, 0xf8, 0xd9};
    const std::string to = "0x80cbe6dac50064d2d695be7f3a3f580776d69224";
    auto tx0 = signer.signTransaction(0, 2e9, 100e9, 200'000, to, calldata, 1);
    auto tx1 = signer.signTransaction(1, 2e9, 100e9, 200'000, to, calldata, 1);
    CHECK(tx0 != tx1);
}
