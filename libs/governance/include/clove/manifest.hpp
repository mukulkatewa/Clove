#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clove {

struct ManifestAgent {
    std::string name;
    std::string command;
    std::string level       = "standard";
    bool        can_think   = true;
    uint64_t    memory_mb   = 256;
    uint64_t    cpu_percent = 25;
};

struct ManifestPolicy {
    std::vector<std::string> llm_allowed_providers;
    double                   llm_max_cost_usd = 0.0;
    std::string              network_default   = "allow";
    std::vector<std::string> allowed_domains;
};

struct Manifest {
    std::string                version;
    std::string                name;
    std::vector<ManifestAgent> agents;
    ManifestPolicy             policy;
    std::string                sha256_digest;
};

/// Parse a JSON manifest string into a Manifest struct.
/// Throws std::runtime_error on invalid input.
[[nodiscard]] Manifest parse_manifest(const std::string& json_contents);

/// Compute SHA-256 hex digest of arbitrary data (using OpenSSL).
[[nodiscard]] std::string sha256_hex(const std::string& data);

} // namespace clove
