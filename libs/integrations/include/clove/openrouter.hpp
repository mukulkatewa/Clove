#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace clove {

struct OpenRouterConfig {
    std::string api_key;
    std::string base_url = "https://openrouter.ai/api/v1";
    std::string default_model = "openai/gpt-4o";
    bool zero_data_retention = false;
    int timeout_ms = 25000;  // 25s — stays below Railway's 30s upstream timeout
    std::string app_name = "clove";
    std::string app_url = "https://cloveos.com";
};

struct OpenRouterUsage {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    double cost_usd = 0.0;
};

struct OpenRouterResponse {
    bool success = false;
    std::string content;
    std::string model_used;
    OpenRouterUsage usage;
    std::string error;
    std::string raw_json;  // Full response for debugging
};

struct OpenRouterModel {
    std::string id;             // "openai/gpt-4o"
    std::string name;           // "GPT-4o"
    double prompt_price;        // per 1M tokens
    double completion_price;    // per 1M tokens
    int context_length;
};

class OpenRouterClient {
public:
    OpenRouterClient() = default;

    void configure(const OpenRouterConfig& config);
    bool is_configured() const { return !config_.api_key.empty(); }
    const OpenRouterConfig& config() const { return config_; }

    /// Send a chat completion request.
    /// `messages` is a JSON array of {role, content} objects.
    /// If `messages` is empty, creates a single user message from `prompt`.
    OpenRouterResponse chat(const std::string& model,
                            const std::string& prompt,
                            const nlohmann::json& options = {});

    /// Send a chat completion with full message array.
    OpenRouterResponse chat_messages(const std::string& model,
                                     const nlohmann::json& messages,
                                     const nlohmann::json& options = {});

    /// List available models (cached for 5 minutes).
    std::vector<OpenRouterModel> list_models();

    /// Check if a model exists.
    bool model_exists(const std::string& model_id);

    /// Get remaining credits.
    std::optional<double> get_credits();

private:
    OpenRouterConfig config_;

    // HTTP helper — returns {success, status_code, body}
    struct HttpResponse {
        bool success = false;
        int status_code = 0;
        std::string body;
    };
    HttpResponse http_post(const std::string& path, const nlohmann::json& body);
    HttpResponse http_get(const std::string& path);

    // Model cache
    std::vector<OpenRouterModel> model_cache_;
    uint64_t model_cache_time_ = 0;

    nlohmann::json build_headers() const;
    OpenRouterResponse parse_chat_response(const HttpResponse& http_resp);
};

} // namespace clove
