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

    // Strip system messages from messages array, use Anthropic's top-level system param
    json filtered_messages = json::array();
    std::string extracted_system = system_prompt;
    for (auto& msg : messages) {
        std::string role = msg.value("role", "");
        if (role == "system") {
            std::string sc = msg.value("content", "");
            if (!sc.empty()) {
                if (!extracted_system.empty()) extracted_system += "\n";
                extracted_system += sc;
            }
        } else if (role == "tool") {
            // Convert OpenAI tool result format to Anthropic tool_result format
            json converted;
            converted["role"] = "user";
            json tool_result_block;
            tool_result_block["type"] = "tool_result";
            tool_result_block["tool_use_id"] = msg.value("tool_call_id", "");
            tool_result_block["content"] = msg.value("content", "");
            converted["content"] = json::array({tool_result_block});
            filtered_messages.push_back(converted);
        } else {
            filtered_messages.push_back(msg);
        }
    }
    body["messages"] = filtered_messages;

    if (!extracted_system.empty()) {
        body["system"] = extracted_system;
    }

    // Convert OpenAI tools format to Anthropic format
    if (!tools.empty() && tools.is_array() && tools.size() > 0) {
        json anthropic_tools = json::array();
        for (auto& t : tools) {
            if (t.value("type", "") == "function" && t.contains("function")) {
                auto& fn = t["function"];
                json at;
                at["name"] = fn.value("name", "");
                at["description"] = fn.value("description", "");
                at["input_schema"] = fn.value("parameters", json::object());
                anthropic_tools.push_back(at);
            }
        }
        if (!anthropic_tools.empty()) {
            body["tools"] = anthropic_tools;
        }
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

        // Build OpenAI-compatible tool_calls and text content
        json openai_tool_calls = json::array();
        std::string text_content;

        if (resp.contains("content") && resp["content"].is_array()) {
            for (auto& block : resp["content"]) {
                std::string btype = block.value("type", "");
                if (btype == "text") {
                    if (!text_content.empty()) text_content += "\n";
                    text_content += block.value("text", "");
                } else if (btype == "tool_use") {
                    // Convert Anthropic tool_use → OpenAI tool_call
                    json tc;
                    tc["id"] = block.value("id", "");
                    tc["type"] = "function";
                    json fn;
                    fn["name"] = block.value("name", "");
                    // Anthropic returns input as object, OpenAI expects string
                    fn["arguments"] = block.value("input", json::object()).dump();
                    tc["function"] = fn;
                    openai_tool_calls.push_back(tc);
                }
            }
        }

        result.content = text_content;

        // Build raw_json in OpenAI-compatible format for RunEngine
        json msg_obj;
        msg_obj["role"] = "assistant";
        msg_obj["content"] = text_content;
        if (!openai_tool_calls.empty()) {
            msg_obj["tool_calls"] = openai_tool_calls;
        }
        std::string finish_reason = result.stop_reason == "tool_use" ? "tool_calls" : "stop";
        result.raw_json = json({
            {"choices", json::array({
                json({
                    {"message", msg_obj},
                    {"finish_reason", finish_reason}
                })
            })}
        }).dump();

        // Usage
        if (resp.contains("usage")) {
            result.input_tokens = resp["usage"].value("input_tokens", 0);
            result.output_tokens = resp["usage"].value("output_tokens", 0);
            result.cost_usd = estimate_cost(resolved, result.input_tokens, result.output_tokens);
        }

        spdlog::info("Anthropic response: model={} in={} out={} stop={} cost=${:.6f}",
            result.model_used, result.input_tokens, result.output_tokens, result.stop_reason, result.cost_usd);

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
