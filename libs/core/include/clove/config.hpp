#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace clove {

struct KernelConfig {
    // -----------------------------------------------------------------------
    // Core
    // -----------------------------------------------------------------------
    std::string socket_path       = "/tmp/clove.sock";
    bool        enable_sandboxing = true;

    // -----------------------------------------------------------------------
    // LLM
    // -----------------------------------------------------------------------
    std::string llm_model         = "gemini-2.0-flash";
    std::string gemini_api_key;
    size_t      llm_worker_count  = 8;
    size_t      async_worker_count = 8;

    // -----------------------------------------------------------------------
    // Inference Gateway
    // -----------------------------------------------------------------------
    bool                     llm_proxy_enabled = false;
    std::vector<std::string> llm_allowed_providers;
    std::vector<std::string> llm_allowed_models;
    double                   llm_max_cost_usd  = 0.0;

    // -----------------------------------------------------------------------
    // OpenRouter integration
    // -----------------------------------------------------------------------
    bool        openrouter_enabled  = false;
    std::string openrouter_api_key;
    std::string openrouter_base_url = "https://openrouter.ai/api/v1";

    // -----------------------------------------------------------------------
    // Egress proxy
    // -----------------------------------------------------------------------
    bool     egress_proxy_enabled = false;
    uint16_t egress_proxy_port    = 9999;

    // -----------------------------------------------------------------------
    // Privacy / PII
    // -----------------------------------------------------------------------
    bool                     privacy_enabled  = false;
    std::string              privacy_mode     = "audit"; // audit | redact | block
    std::vector<std::string> privacy_patterns = {
        "ssn", "email", "phone", "credit_card", "ip_address"
    };

    // -----------------------------------------------------------------------
    // Policy
    // -----------------------------------------------------------------------
    std::string policy_file;
    std::string manifest_file;

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------
    std::string db_path             = "clove.db";
    bool        persistence_enabled = true;

    // -----------------------------------------------------------------------
    // API server
    // -----------------------------------------------------------------------
    bool        api_enabled = false;
    uint16_t    api_port    = 8080;
    std::string api_key;

    // -----------------------------------------------------------------------
    // Tunnel
    // -----------------------------------------------------------------------
    std::string relay_url;
    std::string machine_id;
    std::string machine_token;
    bool        tunnel_auto_connect = false;

    // -----------------------------------------------------------------------
    // Integrations
    // -----------------------------------------------------------------------
    bool        otel_enabled  = false;
    std::string otel_endpoint = "http://localhost:4317";
    bool        mcp_enabled   = false;
    bool        a2a_enabled   = false;
    uint16_t    a2a_port      = 8081;
};

} // namespace clove
