#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace clove {

struct AnthropicResponse {
    bool success = false;
    std::string content;
    std::string model_used;
    int input_tokens = 0;
    int output_tokens = 0;
    double cost_usd = 0.0;
    std::string error;
    std::string stop_reason;
    std::string raw_json;  // OpenAI-compatible format with tool_calls for RunEngine compatibility
};

/// Native Anthropic API client — calls api.anthropic.com directly.
/// No OpenRouter middleman. Uses x-api-key auth + anthropic-version header.
class AnthropicClient {
public:
    AnthropicClient() = default;

    void configure(const std::string& api_key);
    bool is_configured() const { return !api_key_.empty(); }
    const std::string& api_key() const { return api_key_; }

    /// Send a messages request. model should be like "claude-sonnet-4-20250514".
    /// messages is a JSON array of {role, content} objects.
    AnthropicResponse chat(const std::string& model,
                           const nlohmann::json& messages,
                           int max_tokens = 4096,
                           const nlohmann::json& tools = nlohmann::json::array(),
                           const std::string& system_prompt = "");

    /// Simple single-prompt call.
    AnthropicResponse ask(const std::string& model,
                          const std::string& prompt,
                          int max_tokens = 4096);

    /// Map friendly model names to API model IDs.
    static std::string resolve_model(const std::string& model);

    /// Estimate cost from token counts.
    static double estimate_cost(const std::string& model, int input_tokens, int output_tokens);

private:
    std::string api_key_;

    struct HttpResponse {
        bool success = false;
        int status_code = 0;
        std::string body;
    };
    HttpResponse http_post(const std::string& path, const nlohmann::json& body);
};

} // namespace clove
