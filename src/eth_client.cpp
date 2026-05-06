#include "eth_client.h"

#include <nlohmann/json.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>   // defines beast::ssl_stream; pulls in asio ssl headers
#include <boost/beast/version.hpp>

#include <openssl/ssl.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
namespace ssl   = net::ssl;
using tcp       = net::ip::tcp;

// ── URL parser ───────────────────────────────────────────────────────────────

EthClient::EthClient(const std::string& rpc_url) {
    // Expected format: https://host[:port]/path  OR  http://host[:port]/path
    std::string url = rpc_url;
    is_https_ = (url.substr(0, 8) == "https://");
    std::string scheme = is_https_ ? "https://" : "http://";
    port_ = is_https_ ? "443" : "80";

    url = url.substr(scheme.size());
    auto slash = url.find('/');
    std::string hostport = (slash != std::string::npos) ? url.substr(0, slash) : url;
    path_ = (slash != std::string::npos) ? url.substr(slash) : "/";

    auto colon = hostport.find(':');
    if (colon != std::string::npos) {
        host_ = hostport.substr(0, colon);
        port_ = hostport.substr(colon + 1);
    } else {
        host_ = hostport;
    }
}

// ── Core HTTP/HTTPS POST ─────────────────────────────────────────────────────

nlohmann::json EthClient::rpcCall(const std::string& method,
                                   const nlohmann::json& params) {
    net::io_context ioc;
    tcp::resolver  resolver(ioc);
    auto           results = resolver.resolve(host_, port_);

    nlohmann::json body = {
        {"jsonrpc", "2.0"},
        {"method",  method},
        {"params",  params},
        {"id",      ++request_id_}
    };

    auto build_request = [&]() {
        http::request<http::string_body> req{http::verb::post, path_, 11};
        req.set(http::field::host,         host_);
        req.set(http::field::content_type, "application/json");
        req.set(http::field::user_agent,   "curve-keeper/1.0");
        req.body() = body.dump();
        req.prepare_payload();
        return req;
    };

    beast::flat_buffer buf;
    http::response<http::string_body> res;

    if (is_https_) {
        // ── HTTPS path (mainnet RPC providers) ───────────────────────────────
        ssl::context ctx(ssl::context::tlsv12_client);
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);

        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        // SNI — required by most CDN/cloud RPC providers
        if (!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str()))
            throw std::runtime_error("SSL SNI setup failed");

        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::write(stream, build_request());
        http::read(stream, buf, res);

        beast::error_code ec;
        stream.shutdown(ec);
        // ssl::error::stream_truncated is normal on clean TLS shutdown
        if (ec && ec != ssl::error::stream_truncated)
            std::cerr << "[WARN] TLS shutdown: " << ec.message() << "\n";
    } else {
        // ── HTTP path (local Anvil fork, no TLS) ─────────────────────────────
        beast::tcp_stream stream(ioc);
        stream.connect(results);

        http::write(stream, build_request());
        http::read(stream, buf, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    }

    return nlohmann::json::parse(res.body());
}

// ── Public methods ────────────────────────────────────────────────────────────

std::optional<uint64_t> EthClient::estimateGas(const std::string& from,
                                                 const std::string& to,
                                                 const std::string& data) {
    try {
        auto resp = rpcCall("eth_estimateGas",
                            nlohmann::json::array({
                                {{"from", from}, {"to", to}, {"data", data}}
                            }));
        if (resp.contains("error")) {
            std::cerr << "[WARN] eth_estimateGas error: "
                      << resp["error"].dump() << "\n";
            return std::nullopt;
        }
        std::string hex = resp["result"].get<std::string>();
        return static_cast<uint64_t>(std::stoull(hex.substr(2), nullptr, 16));
    } catch (const std::exception& e) {
        std::cerr << "[WARN] eth_estimateGas exception: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<uint64_t> EthClient::getBaseFeePerGas() {
    try {
        auto resp = rpcCall("eth_getBlockByNumber",
                            nlohmann::json::array({"latest", false}));
        if (resp.contains("error") || resp["result"].is_null()) return std::nullopt;
        std::string hex = resp["result"]["baseFeePerGas"].get<std::string>();
        return static_cast<uint64_t>(std::stoull(hex.substr(2), nullptr, 16));
    } catch (const std::exception& e) {
        std::cerr << "[WARN] getBaseFeePerGas exception: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<uint64_t> EthClient::getNonce(const std::string& address) {
    try {
        auto resp = rpcCall("eth_getTransactionCount",
                            nlohmann::json::array({address, "pending"}));
        if (resp.contains("error")) return std::nullopt;
        std::string hex = resp["result"].get<std::string>();
        return static_cast<uint64_t>(std::stoull(hex.substr(2), nullptr, 16));
    } catch (const std::exception& e) {
        std::cerr << "[WARN] getNonce exception: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::string> EthClient::sendRawTransaction(const std::string& hex_tx) {
    try {
        auto resp = rpcCall("eth_sendRawTransaction",
                            nlohmann::json::array({hex_tx}));
        if (resp.contains("error")) {
            std::cerr << "[ERROR] eth_sendRawTransaction: "
                      << resp["error"].dump() << "\n";
            return std::nullopt;
        }
        return resp["result"].get<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "[WARN] sendRawTransaction exception: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<TxReceipt> EthClient::getTransactionReceipt(const std::string& tx_hash) {
    try {
        auto resp = rpcCall("eth_getTransactionReceipt",
                            nlohmann::json::array({tx_hash}));
        if (resp.contains("error") || resp["result"].is_null())
            return std::nullopt;

        auto& r = resp["result"];
        TxReceipt receipt;
        receipt.tx_hash  = tx_hash;
        receipt.success  = (r["status"].get<std::string>() == "0x1");
        std::string gu   = r["gasUsed"].get<std::string>();
        receipt.gas_used = static_cast<uint64_t>(std::stoull(gu.substr(2), nullptr, 16));
        return receipt;
    } catch (const std::exception& e) {
        std::cerr << "[WARN] getTransactionReceipt exception: " << e.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::string> EthClient::ethCall(const std::string& to,
                                               const std::string& data) {
    try {
        auto resp = rpcCall("eth_call",
                            nlohmann::json::array({
                                {{"to", to}, {"data", data}},
                                "latest"
                            }));
        if (resp.contains("error") || resp["result"].is_null()) {
            std::cerr << "[WARN] eth_call error: " << resp.dump() << "\n";
            return std::nullopt;
        }
        return resp["result"].get<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "[WARN] ethCall exception: " << e.what() << "\n";
        return std::nullopt;
    }
}
