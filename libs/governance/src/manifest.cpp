#include <clove/manifest.hpp>

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>

namespace clove {

// ---------------------------------------------------------------------------
// SHA-256 using OpenSSL EVP (works with OpenSSL 3.x and 1.1.x)
// ---------------------------------------------------------------------------

std::string sha256_hex(const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  hash_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    const EVP_MD* md = EVP_sha256();
    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 digest computation failed");
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// Manifest parsing
// ---------------------------------------------------------------------------

Manifest parse_manifest(const std::string& json_contents) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_contents);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string("Manifest parse error: ") + e.what());
    }

    Manifest m;

    // Version & name
    if (j.contains("version") && j["version"].is_string()) {
        m.version = j["version"].get<std::string>();
    }
    if (j.contains("name") && j["name"].is_string()) {
        m.name = j["name"].get<std::string>();
    }

    // Agents
    if (j.contains("agents") && j["agents"].is_array()) {
        for (const auto& agent_json : j["agents"]) {
            ManifestAgent agent;

            if (agent_json.contains("name") && agent_json["name"].is_string()) {
                agent.name = agent_json["name"].get<std::string>();
            }
            if (agent_json.contains("command") && agent_json["command"].is_string()) {
                agent.command = agent_json["command"].get<std::string>();
            }
            if (agent_json.contains("level") && agent_json["level"].is_string()) {
                agent.level = agent_json["level"].get<std::string>();
            }
            if (agent_json.contains("can_think") && agent_json["can_think"].is_boolean()) {
                agent.can_think = agent_json["can_think"].get<bool>();
            }
            if (agent_json.contains("memory_mb") && agent_json["memory_mb"].is_number_unsigned()) {
                agent.memory_mb = agent_json["memory_mb"].get<uint64_t>();
            }
            if (agent_json.contains("cpu_percent") && agent_json["cpu_percent"].is_number_unsigned()) {
                agent.cpu_percent = agent_json["cpu_percent"].get<uint64_t>();
            }

            m.agents.push_back(std::move(agent));
        }
    }

    // Policy
    if (j.contains("policy") && j["policy"].is_object()) {
        const auto& p = j["policy"];

        if (p.contains("llm_allowed_providers") && p["llm_allowed_providers"].is_array()) {
            for (const auto& v : p["llm_allowed_providers"]) {
                if (v.is_string()) {
                    m.policy.llm_allowed_providers.push_back(v.get<std::string>());
                }
            }
        }
        if (p.contains("llm_max_cost_usd") && p["llm_max_cost_usd"].is_number()) {
            m.policy.llm_max_cost_usd = p["llm_max_cost_usd"].get<double>();
        }
        if (p.contains("network_default") && p["network_default"].is_string()) {
            m.policy.network_default = p["network_default"].get<std::string>();
        }
        if (p.contains("allowed_domains") && p["allowed_domains"].is_array()) {
            for (const auto& v : p["allowed_domains"]) {
                if (v.is_string()) {
                    m.policy.allowed_domains.push_back(v.get<std::string>());
                }
            }
        }
    }

    // Compute digest for integrity verification
    m.sha256_digest = sha256_hex(json_contents);

    return m;
}

} // namespace clove
