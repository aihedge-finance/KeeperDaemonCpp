#include "tx_signer.h"

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// ════════════════════════════════════════════════════════════════════════════
// Keccak-256 (Ethereum pre-NIST flavour)
// ════════════════════════════════════════════════════════════════════════════

static const uint64_t KECCAK_RC[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808AULL,
    0x8000000080008000ULL, 0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008AULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

static const int KECCAK_RHO[24] = {
     1,  3,  6, 10, 15, 21, 28, 36, 45, 55,  2, 14,
    27, 41, 56,  8, 25, 43, 62, 18, 39, 61, 20, 44
};

static const int KECCAK_PI[24] = {
    10,  7, 11, 17, 18, 3, 5, 16,  8, 21, 24, 4,
    15, 23, 19, 13, 12, 2, 20, 14, 22,  9,  6,  1
};

static inline uint64_t rotl64(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

static void keccakf(uint64_t st[25]) {
    for (int round = 0; round < 24; ++round) {
        // Theta
        uint64_t C[5], D[5];
        for (int i = 0; i < 5; ++i)
            C[i] = st[i] ^ st[i+5] ^ st[i+10] ^ st[i+15] ^ st[i+20];
        for (int i = 0; i < 5; ++i)
            D[i] = C[(i+4)%5] ^ rotl64(C[(i+1)%5], 1);
        for (int i = 0; i < 25; ++i)
            st[i] ^= D[i%5];
        // Rho + Pi
        uint64_t last = st[1];
        for (int i = 0; i < 24; ++i) {
            int j = KECCAK_PI[i];
            uint64_t tmp = st[j];
            st[j] = rotl64(last, KECCAK_RHO[i]);
            last = tmp;
        }
        // Chi
        for (int i = 0; i < 25; i += 5) {
            uint64_t t[5];
            for (int j = 0; j < 5; ++j) t[j] = st[i+j];
            for (int j = 0; j < 5; ++j)
                st[i+j] = t[j] ^ (~t[(j+1)%5] & t[(j+2)%5]);
        }
        // Iota
        st[0] ^= KECCAK_RC[round];
    }
}

std::array<uint8_t, 32> keccak256(const uint8_t* data, size_t len) {
    constexpr size_t RATE = 136; // 1088 bits
    uint64_t st[25] = {};

    size_t offset = 0;
    // Absorb full blocks
    while (offset + RATE <= len) {
        for (size_t i = 0; i < RATE / 8; ++i) {
            uint64_t word = 0;
            for (int b = 0; b < 8; ++b)
                word |= static_cast<uint64_t>(data[offset + i*8 + b]) << (b * 8);
            st[i] ^= word;
        }
        keccakf(st);
        offset += RATE;
    }
    // Pad the final block
    uint8_t block[RATE] = {};
    std::memcpy(block, data + offset, len - offset);
    block[len - offset]  = 0x01;       // Keccak padding (not SHA-3 0x06)
    block[RATE - 1]     |= 0x80;
    for (size_t i = 0; i < RATE / 8; ++i) {
        uint64_t word = 0;
        for (int b = 0; b < 8; ++b)
            word |= static_cast<uint64_t>(block[i*8 + b]) << (b * 8);
        st[i] ^= word;
    }
    keccakf(st);

    // Squeeze 32 bytes
    std::array<uint8_t, 32> hash;
    for (size_t i = 0; i < 4; ++i)
        for (int b = 0; b < 8; ++b)
            hash[i*8 + b] = static_cast<uint8_t>((st[i] >> (b * 8)) & 0xFF);
    return hash;
}

// ════════════════════════════════════════════════════════════════════════════
// Hex utilities
// ════════════════════════════════════════════════════════════════════════════

static std::string bytesToHex(const uint8_t* data, size_t len) {
    std::ostringstream ss;
    for (size_t i = 0; i < len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    return ss.str();
}

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::string h = (hex.substr(0, 2) == "0x") ? hex.substr(2) : hex;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < h.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
    return bytes;
}

// ════════════════════════════════════════════════════════════════════════════
// RLP encoding
// ════════════════════════════════════════════════════════════════════════════

static std::vector<uint8_t> rlpLength(size_t len, uint8_t base) {
    if (len <= 55) {
        return {static_cast<uint8_t>(base + len)};
    }
    // Encode len as big-endian, minimal bytes
    std::vector<uint8_t> lenBytes;
    size_t tmp = len;
    while (tmp) { lenBytes.push_back(tmp & 0xFF); tmp >>= 8; }
    std::reverse(lenBytes.begin(), lenBytes.end());
    std::vector<uint8_t> out;
    out.push_back(static_cast<uint8_t>(base + 55 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
    return out;
}

// RLP-encode a byte string
static std::vector<uint8_t> rlpBytes(const std::vector<uint8_t>& data) {
    if (data.size() == 1 && data[0] < 0x80) {
        // Construct explicitly to avoid GCC 13 false-positive -Warray-bounds
        // triggered by the inlined copy-constructor memmove path.
        return std::vector<uint8_t>(1, data[0]);
    }
    auto prefix = rlpLength(data.size(), 0x80);
    prefix.insert(prefix.end(), data.begin(), data.end());
    return prefix;
}

// RLP-encode an unsigned integer (big-endian, no leading zeros)
static std::vector<uint8_t> rlpUint(uint64_t value) {
    if (value == 0) return {0x80}; // RLP empty string = 0
    // Build big-endian bytes, strip leading zeros
    uint8_t buf[8];
    size_t len = 0;
    for (int i = 7; i >= 0; --i) {
        uint8_t b = (value >> (i * 8)) & 0xFF;
        if (len == 0 && b == 0) continue;
        buf[len++] = b;
    }
    std::vector<uint8_t> bytes(buf, buf + len);
    return rlpBytes(bytes);
}

// RLP-encode a 32-byte big-endian value (r or s from signature)
static std::vector<uint8_t> rlpUint256Bytes(const uint8_t* data32) {
    // Strip leading zeros
    size_t start = 0;
    while (start < 31 && data32[start] == 0) ++start;
    std::vector<uint8_t> bytes(data32 + start, data32 + 32);
    if (bytes.empty()) return {0x80};
    return rlpBytes(bytes);
}

// RLP-encode a list of already-encoded items
static std::vector<uint8_t> rlpList(const std::vector<std::vector<uint8_t>>& items) {
    std::vector<uint8_t> payload;
    for (auto& item : items)
        payload.insert(payload.end(), item.begin(), item.end());
    auto prefix = rlpLength(payload.size(), 0xC0);
    prefix.insert(prefix.end(), payload.begin(), payload.end());
    return prefix;
}

// ════════════════════════════════════════════════════════════════════════════
// TxSigner
// ════════════════════════════════════════════════════════════════════════════

TxSigner::TxSigner(const std::string& private_key_hex) {
    auto bytes = hexToBytes(private_key_hex);
    if (bytes.size() != 32)
        throw std::runtime_error("Private key must be 32 bytes");
    std::copy(bytes.begin(), bytes.end(), private_key_.begin());
    deriveAddress();
}

void TxSigner::deriveAddress() {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, private_key_.data()))
        throw std::runtime_error("Invalid private key");

    uint8_t pubkey_bytes[65];
    size_t  pubkey_len = 65;
    secp256k1_ec_pubkey_serialize(ctx, pubkey_bytes, &pubkey_len,
                                  &pubkey, SECP256K1_EC_UNCOMPRESSED);
    secp256k1_context_destroy(ctx);

    // Hash the 64-byte uncompressed key body (skip 0x04 prefix)
    auto hash = keccak256(pubkey_bytes + 1, 64);

    // Ethereum address = last 20 bytes of hash
    address_ = "0x" + bytesToHex(hash.data() + 12, 20);
}

std::string TxSigner::signTransaction(uint64_t                    nonce,
                                       uint64_t                    max_priority_fee,
                                       uint64_t                    max_fee,
                                       uint64_t                    gas_limit,
                                       const std::string&          to,
                                       const std::vector<uint8_t>& data,
                                       uint64_t                    chain_id) {
    // Encode the 20-byte "to" address as a fixed-length byte string
    auto to_bytes = hexToBytes(to);
    if (to_bytes.size() != 20)
        throw std::runtime_error("'to' address must be 20 bytes");

    // The "to" field is always RLP-encoded as a 20-byte string (no trimming)
    std::vector<uint8_t> to_rlp;
    {
        auto prefix = rlpLength(20, 0x80); // 0x94
        to_rlp.insert(to_rlp.end(), prefix.begin(), prefix.end());
        to_rlp.insert(to_rlp.end(), to_bytes.begin(), to_bytes.end());
    }

    // Build unsigned payload fields
    auto chain_id_enc    = rlpUint(chain_id);
    auto nonce_enc       = rlpUint(nonce);
    auto priority_enc    = rlpUint(max_priority_fee);
    auto max_fee_enc     = rlpUint(max_fee);
    auto gas_limit_enc   = rlpUint(gas_limit);
    auto value_enc       = rlpUint(0);            // 0 ETH
    auto data_enc        = rlpBytes(data);
    auto access_list_enc = rlpList({});            // empty access list

    auto unsigned_payload = rlpList({
        chain_id_enc, nonce_enc, priority_enc, max_fee_enc,
        gas_limit_enc, to_rlp, value_enc, data_enc, access_list_enc
    });

    // Signing hash: keccak256(0x02 || RLP(unsigned fields))
    std::vector<uint8_t> to_hash;
    to_hash.push_back(0x02);
    to_hash.insert(to_hash.end(), unsigned_payload.begin(), unsigned_payload.end());
    auto hash = keccak256(to_hash.data(), to_hash.size());

    // secp256k1 sign
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, hash.data(),
                                          private_key_.data(), nullptr, nullptr))
        throw std::runtime_error("ECDSA signing failed");

    uint8_t rs[64];
    int     recovery_id = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, rs, &recovery_id, &sig);
    secp256k1_context_destroy(ctx);

    // Build signed transaction RLP
    auto v_enc = rlpUint(static_cast<uint64_t>(recovery_id)); // 0 or 1
    auto r_enc = rlpUint256Bytes(rs);
    auto s_enc = rlpUint256Bytes(rs + 32);

    auto signed_payload = rlpList({
        chain_id_enc, nonce_enc, priority_enc, max_fee_enc,
        gas_limit_enc, to_rlp, value_enc, data_enc, access_list_enc,
        v_enc, r_enc, s_enc
    });

    std::vector<uint8_t> raw_tx;
    raw_tx.push_back(0x02); // EIP-2718 type byte
    raw_tx.insert(raw_tx.end(), signed_payload.begin(), signed_payload.end());

    return "0x" + bytesToHex(raw_tx.data(), raw_tx.size());
}
