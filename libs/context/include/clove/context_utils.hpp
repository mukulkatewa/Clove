#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <string>

namespace clove {

/// Generate a random hex string of the given length.
inline std::string generate_hex_id(size_t len = 12) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len);
    std::uniform_int_distribution<int> dist(0, 15);
    for (size_t i = 0; i < len; ++i) out += hex[dist(rng)];
    return out;
}

inline std::string generate_artifact_id() { return "art_" + generate_hex_id(); }
inline std::string generate_chain_id()    { return "chain_" + generate_hex_id(); }
inline std::string generate_mem_id()      { return "mem_" + generate_hex_id(); }

/// Current timestamp in milliseconds since epoch.
inline uint64_t now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace clove
