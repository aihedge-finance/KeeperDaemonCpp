#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

struct TxReceipt {
    bool        success  = false;
    uint64_t    gas_used = 0;
    std::string tx_hash;
};

// Boost.Beast HTTP/HTTPS JSON-RPC client.
// All methods return std::optional; nullopt means RPC/network error.
// Each call opens a fresh connection — appropriate for a low-frequency keeper.
// Automatically uses plain TCP for http:// (e.g. local Anvil) and TLS for https://.
class EthClient {
public:
    // Parses rpc_url into host/port/path at construction time.
    explicit EthClient(const std::string& rpc_url);

    // eth_estimateGas — nullopt on revert or network error.
    std::optional<uint64_t> estimateGas(const std::string& from,
                                         const std::string& to,
                                         const std::string& data);

    // eth_getBlockByNumber("latest") → baseFeePerGas in wei.
    std::optional<uint64_t> getBaseFeePerGas();

    // eth_getTransactionCount — returns pending nonce for address.
    std::optional<uint64_t> getNonce(const std::string& address);

    // eth_sendRawTransaction — returns 0x-prefixed tx hash.
    std::optional<std::string> sendRawTransaction(const std::string& hex_tx);

    // eth_getTransactionReceipt — nullopt if not mined yet.
    std::optional<TxReceipt> getTransactionReceipt(const std::string& tx_hash);

private:
    bool        is_https_{false};
    std::string host_;
    std::string port_;
    std::string path_;
    int         request_id_{0};

    // Performs a single synchronous HTTP or HTTPS POST, returns parsed JSON body.
    // Throws std::runtime_error on network/HTTP error.
    nlohmann::json rpcCall(const std::string& method,
                            const nlohmann::json& params);
};
