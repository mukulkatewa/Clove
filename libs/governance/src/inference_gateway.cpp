#include <clove/inference_gateway.hpp>

#include <algorithm>

namespace clove {

// ---------------------------------------------------------------------------
// Known LLM provider domains
// ---------------------------------------------------------------------------

static const std::vector<LlmDomainMapping> kKnownDomains = {
    {"api.openai.com",                        "openai"},
    {"api.anthropic.com",                     "anthropic"},
    {"generativelanguage.googleapis.com",     "gemini"},
    {"api.groq.com",                          "groq"},
    {"api.mistral.ai",                        "mistral"},
    {"api.cohere.ai",                         "cohere"},
    {"api.together.xyz",                      "together"},
};

const std::vector<LlmDomainMapping>& InferenceGateway::known_domains() {
    return kKnownDomains;
}

// ---------------------------------------------------------------------------
// Wildcard model matching
// ---------------------------------------------------------------------------

bool InferenceGateway::model_pattern_matches(const std::string& model,
                                             const std::string& pattern) {
    if (pattern == model) return true;
    if (pattern == "*") return true;

    // Trailing wildcard: "gemini-*" matches "gemini-2.0-flash"
    if (!pattern.empty() && pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return model.compare(0, prefix.size(), prefix) == 0;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void InferenceGateway::configure(const InferenceGatewayConfig& config) {
    std::lock_guard lock(mutex_);
    config_ = config;
}

InferenceGatewayConfig InferenceGateway::get_config() const {
    std::lock_guard lock(mutex_);
    return config_;
}

bool InferenceGateway::is_enabled() const {
    std::lock_guard lock(mutex_);
    return config_.enabled;
}

// ---------------------------------------------------------------------------
// Access checks
// ---------------------------------------------------------------------------

bool InferenceGateway::is_model_allowed(const std::string& model) const {
    std::lock_guard lock(mutex_);

    if (!config_.enabled) return true;
    if (config_.allowed_models.empty()) return true;

    for (const auto& pattern : config_.allowed_models) {
        if (model_pattern_matches(model, pattern)) return true;
    }

    return false;
}

bool InferenceGateway::is_provider_allowed(const std::string& provider) const {
    std::lock_guard lock(mutex_);

    if (!config_.enabled) return true;
    if (config_.allowed_providers.empty()) return true;

    for (const auto& allowed : config_.allowed_providers) {
        if (allowed == provider || allowed == "*") return true;
    }

    return false;
}

bool InferenceGateway::is_within_cost_limit(double estimated_cost) const {
    std::lock_guard lock(mutex_);

    if (!config_.enabled) return true;
    if (config_.max_cost_usd <= 0.0) return true; // no limit

    return (config_.current_cost_usd + estimated_cost) <= config_.max_cost_usd;
}

void InferenceGateway::record_cost(double cost) {
    std::lock_guard lock(mutex_);
    config_.current_cost_usd += cost;
}

// ---------------------------------------------------------------------------
// Domain lookup
// ---------------------------------------------------------------------------

bool InferenceGateway::is_llm_domain(const std::string& domain) const {
    for (const auto& mapping : kKnownDomains) {
        if (mapping.domain == domain) return true;
    }
    return false;
}

std::string InferenceGateway::get_provider_for_domain(
    const std::string& domain) const {
    for (const auto& mapping : kKnownDomains) {
        if (mapping.domain == domain) return mapping.provider;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Dynamic config update
// ---------------------------------------------------------------------------

void InferenceGateway::update_config(const nlohmann::json& delta) {
    std::lock_guard lock(mutex_);

    if (delta.contains("enabled") && delta["enabled"].is_boolean()) {
        config_.enabled = delta["enabled"].get<bool>();
    }
    if (delta.contains("allowed_providers") && delta["allowed_providers"].is_array()) {
        config_.allowed_providers.clear();
        for (const auto& v : delta["allowed_providers"]) {
            if (v.is_string()) config_.allowed_providers.push_back(v.get<std::string>());
        }
    }
    if (delta.contains("allowed_models") && delta["allowed_models"].is_array()) {
        config_.allowed_models.clear();
        for (const auto& v : delta["allowed_models"]) {
            if (v.is_string()) config_.allowed_models.push_back(v.get<std::string>());
        }
    }
    if (delta.contains("max_cost_usd") && delta["max_cost_usd"].is_number()) {
        config_.max_cost_usd = delta["max_cost_usd"].get<double>();
    }
}

nlohmann::json InferenceGateway::to_json() const {
    std::lock_guard lock(mutex_);
    return nlohmann::json{
        {"enabled",           config_.enabled},
        {"allowed_providers", config_.allowed_providers},
        {"allowed_models",    config_.allowed_models},
        {"max_cost_usd",      config_.max_cost_usd},
        {"current_cost_usd",  config_.current_cost_usd},
    };
}

} // namespace clove
