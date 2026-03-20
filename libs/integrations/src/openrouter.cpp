#include <clove/openrouter.hpp>
#include <curl/curl.h>
#include <chrono>
#include <sstream>

namespace clove {

// ── libcurl write callback ──────────────────────────────────────
static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// ── Configuration ───────────────────────────────────────────────

void OpenRouterClient::configure(const OpenRouterConfig& config) {
    config_ = config;
}

// ── HTTP helpers ────────────────────────────────────────────────

OpenRouterClient::HttpResponse OpenRouterClient::http_post(
    const std::string& path, const nlohmann::json& body) {

    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.success = false;
        resp.body = "Failed to init curl";
        return resp;
    }

    std::string url = config_.base_url + path;
    std::string body_str = body.dump();
    std::string response_body;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + config_.api_key).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("X-Title: " + config_.app_name).c_str());
    headers = curl_slist_append(headers, ("HTTP-Referer: " + config_.app_url).c_str());

    if (config_.zero_data_retention) {
        // OpenRouter provider preferences for ZDR
        // Handled via request body, not headers
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.timeout_ms));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    // Follow redirects (OpenRouter may redirect)
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        resp.success = true;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
    } else {
        resp.success = false;
        resp.body = curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return resp;
    }

    resp.body = response_body;
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

OpenRouterClient::HttpResponse OpenRouterClient::http_get(const std::string& path) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.success = false;
        resp.body = "Failed to init curl";
        return resp;
    }

    std::string url = config_.base_url + path;
    std::string response_body;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + config_.api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.timeout_ms));

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        resp.success = true;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
    } else {
        resp.success = false;
        resp.body = curl_easy_strerror(res);
    }

    resp.body = response_body;
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

// ── Chat API ────────────────────────────────────────────────────

OpenRouterResponse OpenRouterClient::chat(const std::string& model,
                                           const std::string& prompt,
                                           const nlohmann::json& options) {
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "user"}, {"content", prompt}});
    return chat_messages(model, messages, options);
}

OpenRouterResponse OpenRouterClient::chat_messages(const std::string& model,
                                                    const nlohmann::json& messages,
                                                    const nlohmann::json& options) {
    if (!is_configured()) {
        return {false, "", "", {}, "OpenRouter not configured (no API key)"};
    }

    std::string model_to_use = model.empty() ? config_.default_model : model;

    nlohmann::json body;
    body["model"] = model_to_use;
    body["messages"] = messages;

    // Apply options (temperature, max_tokens, etc.)
    if (options.contains("temperature"))  body["temperature"] = options["temperature"];
    if (options.contains("max_tokens"))   body["max_tokens"] = options["max_tokens"];
    if (options.contains("top_p"))        body["top_p"] = options["top_p"];
    if (options.contains("stop"))         body["stop"] = options["stop"];
    if (options.contains("stream"))       body["stream"] = options["stream"];

    // Provider preferences
    if (config_.zero_data_retention) {
        body["provider"] = {
            {"data_collection", "deny"},
            {"allow_fallbacks", true}
        };
    }

    // Route preferences from options
    if (options.contains("provider")) body["provider"] = options["provider"];
    if (options.contains("transforms")) body["transforms"] = options["transforms"];

    auto http_resp = http_post("/chat/completions", body);
    return parse_chat_response(http_resp);
}

OpenRouterResponse OpenRouterClient::parse_chat_response(const HttpResponse& http_resp) {
    OpenRouterResponse resp;
    resp.raw_json = http_resp.body;

    if (!http_resp.success) {
        resp.success = false;
        resp.error = http_resp.body;
        return resp;
    }

    try {
        auto j = nlohmann::json::parse(http_resp.body);

        // Check for API error
        if (j.contains("error")) {
            resp.success = false;
            if (j["error"].is_object()) {
                resp.error = j["error"].value("message", "Unknown error");
            } else {
                resp.error = j["error"].get<std::string>();
            }
            return resp;
        }

        // Parse successful response
        resp.success = true;

        if (j.contains("choices") && !j["choices"].empty()) {
            auto& choice = j["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                resp.content = choice["message"]["content"].get<std::string>();
            }
        }

        if (j.contains("model")) {
            resp.model_used = j["model"].get<std::string>();
        }

        if (j.contains("usage")) {
            auto& usage = j["usage"];
            resp.usage.prompt_tokens = usage.value("prompt_tokens", 0);
            resp.usage.completion_tokens = usage.value("completion_tokens", 0);
            resp.usage.total_tokens = usage.value("total_tokens", 0);
            // OpenRouter returns cost in the usage object or in a separate field
            if (usage.contains("cost")) {
                resp.usage.cost_usd = usage["cost"].get<double>();
            }
        }

        // OpenRouter may also put cost at top level
        if (j.contains("usage") && j["usage"].contains("cost")) {
            resp.usage.cost_usd = j["usage"]["cost"].get<double>();
        }

    } catch (const std::exception& e) {
        resp.success = false;
        resp.error = std::string("JSON parse error: ") + e.what();
    }

    return resp;
}

// ── Models API ──────────────────────────────────────────────────

std::vector<OpenRouterModel> OpenRouterClient::list_models() {
    // Cache for 5 minutes
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (!model_cache_.empty() && (now - model_cache_time_) < 300) {
        return model_cache_;
    }

    auto http_resp = http_get("/models");
    if (!http_resp.success || http_resp.status_code != 200) {
        return model_cache_;  // Return stale cache on error
    }

    try {
        auto j = nlohmann::json::parse(http_resp.body);
        model_cache_.clear();

        if (j.contains("data") && j["data"].is_array()) {
            for (const auto& m : j["data"]) {
                OpenRouterModel model;
                model.id = m.value("id", "");
                model.name = m.value("name", "");
                model.context_length = m.value("context_length", 0);

                if (m.contains("pricing")) {
                    auto& p = m["pricing"];
                    // Pricing is per-token as a string, convert to per-1M
                    if (p.contains("prompt") && p["prompt"].is_string()) {
                        model.prompt_price = std::stod(p["prompt"].get<std::string>()) * 1000000.0;
                    }
                    if (p.contains("completion") && p["completion"].is_string()) {
                        model.completion_price = std::stod(p["completion"].get<std::string>()) * 1000000.0;
                    }
                }

                model_cache_.push_back(std::move(model));
            }
        }

        model_cache_time_ = now;
    } catch (...) {
        // Keep stale cache
    }

    return model_cache_;
}

bool OpenRouterClient::model_exists(const std::string& model_id) {
    auto models = list_models();
    for (const auto& m : models) {
        if (m.id == model_id) return true;
    }
    return false;
}

// ── Credits API ─────────────────────────────────────────────────

std::optional<double> OpenRouterClient::get_credits() {
    // OpenRouter credits endpoint is at /auth/key
    auto http_resp = http_get("/auth/key");
    if (!http_resp.success || http_resp.status_code != 200) {
        return std::nullopt;
    }

    try {
        auto j = nlohmann::json::parse(http_resp.body);
        if (j.contains("data") && j["data"].contains("limit_remaining")) {
            return j["data"]["limit_remaining"].get<double>();
        }
    } catch (...) {}

    return std::nullopt;
}

} // namespace clove
