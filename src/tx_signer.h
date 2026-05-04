#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// ── Keccak-256 (Ethereum flavour, pre-NIST) ──────────────────────────────────
std::array<uint8_t, 32> keccak256(const uint8_t* data, size_t len);

// ── EIP-1559 transaction signer ──────────────────────────────────────────────
class TxSigner {
public:
    // private_key_hex: 0x-prefixed 32-byte hex string.
    explicit TxSigner(const std::string& private_key_hex);

    // Returns the 0x-prefixed Ethereum address derived from the private key.
    const std::string& getAddress() const { return address_; }

    // Builds, RLP-encodes, and signs an EIP-1559 transaction.
    // Returns a 0x-prefixed hex-encoded signed transaction ready for broadcast.
    std::string signTransaction(uint64_t                    nonce,
                                uint64_t                    max_priority_fee_per_gas,
                                uint64_t                    max_fee_per_gas,
                                uint64_t                    gas_limit,
                                const std::string&          to,
                                const std::vector<uint8_t>& data,
                                uint64_t                    chain_id = 1);

private:
    std::array<uint8_t, 32> private_key_;
    std::string             address_;

    void deriveAddress();
};
