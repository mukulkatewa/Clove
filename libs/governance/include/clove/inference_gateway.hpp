#pragma once

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace clove {

struct InferenceGatewayConfig {
    bool                     enabled = false;
    std::vector<std::string> allowed_providers;
    std::vector<std::string> allowed_models;
    double                   max_cost_usd     = 0.0;
    double                   current_cost_usd = 0.0;
};

struct LlmDomainMapping {
    std::string domain;
    std::string provider;
};

class InferenceGateway {
public:
    void configure(const InferenceGatewayConfig& config);
    [[nodiscard]] InferenceGatewayConfig get_config() const;
    [[nodiscard]] bool is_enabled() const;

    [[nodiscard]] bool is_model_allowed(const std::string& model) const;
    [[nodiscard]] bool is_provider_allowed(const std::string& provider) const;
    [[nodiscard]] bool is_within_cost_limit(double estimated_cost = 0.0) const;
    void record_cost(double cost);

    [[nodiscard]] bool is_llm_domain(const std::string& domain) const;
    [[nodiscard]] std::string get_provider_for_domain(const std::string& domain) const;

    void update_config(const nlohmann::json& delta);
    [[nodiscard]] nlohmann::json to_json() const;

    static const std::vector<LlmDomainMapping>& known_domains();

private:
    mutable std::mutex      mutex_;
    InferenceGatewayConfig  config_;

    static bool model_pattern_matches(const std::string& model,
                                      const std::string& pattern);
};

} // namespace clove
