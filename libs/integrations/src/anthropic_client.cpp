#include <clove/anthropic_client.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace clove {

// ── libcurl write callback ──────────────────────────────────────
static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

void AnthropicClient::configure(const std::string& api_key) {
    api_key_ = api_key;
    spdlog::info("Anthropic client configured (native API)");
}

// ── Model name resolution ───────────────────────────────────────
std::string AnthropicClient::resolve_model(const std::string& model) {
    // Map friendly names to API model IDs
    if (model == "claude-opus-4" || model == "anthropic/claude-opus-4")
        return "claude-opus-4-20250514";
    if (model == "claude-sonnet-4" || model == "anthropic/claude-sonnet-4")
        return "claude-sonnet-4-20250514";
    if (model == "claude-haiku-4" || model == "anthropic/claude-haiku-4" ||
        model == "claude-haiku-4-5" || model == "anthropic/claude-haiku-4-5")
        return "claude-haiku-4-5-20251001";
    // If it already looks like a full model ID, use as-is
    if (model.find("claude-") == 0 && model.find("-202") != std::string::npos)
        return model;
    // Default
    return "claude-sonnet-4-20250514";
}

// ── Cost estimation ─────────────────────────────────────────────
double AnthropicClient::estimate_cost(const std::string& model, int input_tokens, int output_tokens) {
    // Prices per million tokens (as of 2026)
    double input_price = 3.0;   // default Sonnet
    double output_price = 15.0;

    if (model.find("opus") != std::string::npos) {
        input_price = 15.0; output_price = 75.0;
    } else if (model.find("haiku") != std::string::npos) {
        input_price = 0.80; output_price = 4.0;
    } else if (model.find("sonnet") != std::string::npos) {
        input_price = 3.0; output_price = 15.0;
    }

    return (input_tokens * input_price / 1000000.0) + (output_tokens * output_price / 1000000.0);
}

// ── HTTP POST ───────────────────────────────────────────────────
AnthropicClient::HttpResponse AnthropicClient::http_post(const std::string& path, const json& body) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.body = "Failed to init curl";
        return resp;
    }

    std::string url = "https://api.anthropic.com" + path;
    std::string body_str = body.dump();
    std::string response_body;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("x-api-key: " + api_key_).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "content-type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        resp.success = (http_code >= 200 && http_code < 300);
        resp.status_code = static_cast<int>(http_code);
        resp.body = response_body;
    } else {
        resp.body = std::string("curl error: ") + curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

// ── Chat (full messages) ────────────────────────────────────────
AnthropicResponse AnthropicClient::chat(const std::string& model,
                                         const json& messages,
                                         int max_tokens,
                                         const json& tools,
                                         const std::string& system_prompt) {
    AnthropicResponse result;
    std::string resolved = resolve_model(model);

    json body;
    body["model"] = resolved;
    body["max_tokens"] = max_tokens;
    body["messages"] = messages;

    if (!system_prompt.empty()) {
        body["system"] = system_prompt;
    }

    if (!tools.empty() && tools.is_array() && tools.size() > 0) {
        body["tools"] = tools;
    }

    spdlog::debug("Anthropic API call: model={} messages={} max_tokens={}", resolved, messages.size(), max_tokens);

    auto http_resp = http_post("/v1/messages", body);

    if (!http_resp.success) {
        // Try to parse error
        try {
            auto err = json::parse(http_resp.body);
            result.error = err.value("error", json::object()).value("message", http_resp.body);
        } catch (...) {
            result.error = http_resp.body;
        }
        spdlog::warn("Anthropic API error ({}): {}", http_resp.status_code, result.error);
        return result;
    }

    // Parse response
    try {
        auto resp = json::parse(http_resp.body);

        result.success = true;
        result.model_used = resp.value("model", resolved);
        result.stop_reason = resp.value("stop_reason", "");

        // Extract content — Anthropic returns array of content blocks
        if (resp.contains("content") && resp["content"].is_array()) {
            for (auto& block : resp["content"]) {
                if (block.value("type", "") == "text") {
                    if (!result.content.empty()) result.content += "\n";
                    result.content += block.value("text", "");
                }
                // Tool use blocks
                if (block.value("type", "") == "tool_use") {
                    if (!result.content.empty()) result.content += "\n";
                    result.content += "[tool_use: " + block.value("name", "?") + "]";
                }
            }
        }

        // Usage
        if (resp.contains("usage")) {
            result.input_tokens = resp["usage"].value("input_tokens", 0);
            result.output_tokens = resp["usage"].value("output_tokens", 0);
            result.cost_usd = estimate_cost(resolved, result.input_tokens, result.output_tokens);
        }

        spdlog::info("Anthropic response: model={} in={} out={} cost=${:.6f}",
            result.model_used, result.input_tokens, result.output_tokens, result.cost_usd);

    } catch (const std::exception& e) {
        result.error = std::string("Failed to parse response: ") + e.what();
        spdlog::warn("Anthropic parse error: {}", result.error);
    }

    return result;
}

// ── Simple ask ──────────────────────────────────────────────────
AnthropicResponse AnthropicClient::ask(const std::string& model,
                                        const std::string& prompt,
                                        int max_tokens) {
    json messages = json::array();
    messages.push_back({{"role", "user"}, {"content", prompt}});
    return chat(model, messages, max_tokens);
}

} // namespace clove
