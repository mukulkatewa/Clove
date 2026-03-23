#include "mod.hpp"
#include <clove/permissions_store.hpp>
#include <clove/agent_scheduler.hpp>
#include <clove/audit_log.hpp>
#include <clove/policy_recommender.hpp>
#include <curl/curl.h>

namespace clove {

static size_t http_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

void NetworkSyscalls::register_syscalls(SyscallRouter& router) {
    using json = nlohmann::json;

    // SYS_HTTP — permission-gated HTTP request
    router.register_handler(SyscallOp::SYS_HTTP,
        [this](const Message& msg) -> Message {
            json response;
            try {
                auto req = json::parse(msg.payload_str());
                std::string url = req.at("url").get<std::string>();
                std::string method = req.value("method", "GET");
                std::string body = req.value("body", "");
                json headers_obj = req.value("headers", json::object());

                // Permission check
                auto& perms = ctx_.permissions_store.get_or_create(msg.agent_id());
                if (!perms.can_http) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "http", url,
                        "http not allowed", 0
                    });
                    ctx_.audit_logger.log(AuditCategory::NETWORK, "HTTP_DENIED",
                        msg.agent_id(), "", {{"url", url}, {"reason", "can_http=false"}}, false);
                    response["success"] = false;
                    response["error"] = "HTTP requests not permitted";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_HTTP, response.dump());
                }

                // Domain check
                std::string domain = extract_domain(url);
                if (!perms.can_access_domain(domain)) {
                    ctx_.policy_recommender.record_denial({
                        msg.agent_id(), "http", domain,
                        "domain not in allowlist", 0
                    });
                    ctx_.audit_logger.log(AuditCategory::NETWORK, "HTTP_DOMAIN_DENIED",
                        msg.agent_id(), "", {{"url", url}, {"domain", domain}}, false);
                    response["success"] = false;
                    response["error"] = "domain not allowed: " + domain;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_HTTP, response.dump());
                }

                // Method check
                if (!perms.can_http_method(method)) {
                    response["success"] = false;
                    response["error"] = "HTTP method not allowed: " + method;
                    return Message::create(msg.agent_id(), SyscallOp::SYS_HTTP, response.dump());
                }

                // Execute HTTP request
                CURL* curl = curl_easy_init();
                if (!curl) {
                    response["success"] = false;
                    response["error"] = "failed to initialize HTTP client";
                    return Message::create(msg.agent_id(), SyscallOp::SYS_HTTP, response.dump());
                }

                std::string response_body;
                struct curl_slist* curl_headers = nullptr;

                for (auto& [key, val] : headers_obj.items()) {
                    std::string h = key + ": " + val.get<std::string>();
                    curl_headers = curl_slist_append(curl_headers, h.c_str());
                }

                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
                curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

                if (curl_headers) {
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
                }

                if (method == "POST" || method == "PUT" || method == "PATCH") {
                    if (method == "POST") curl_easy_setopt(curl, CURLOPT_POST, 1L);
                    else if (method == "PUT") curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
                    else curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
                } else if (method == "DELETE") {
                    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
                } else if (method == "HEAD") {
                    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                }

                // Track tool wait state
                if (ctx_.scheduler) ctx_.scheduler->mark_waiting_tool(msg.agent_id());

                CURLcode res = curl_easy_perform(curl);

                // Back to ready
                if (ctx_.scheduler) ctx_.scheduler->mark_ready(msg.agent_id());

                if (res == CURLE_OK) {
                    long status_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

                    response["success"] = true;
                    response["status_code"] = status_code;
                    response["body"] = response_body;

                    ctx_.audit_logger.log(AuditCategory::NETWORK, "HTTP_REQUEST",
                        msg.agent_id(), "", {
                            {"url", url}, {"method", method},
                            {"status_code", status_code},
                            {"response_bytes", response_body.size()}
                        });
                } else {
                    response["success"] = false;
                    response["error"] = curl_easy_strerror(res);
                }

                if (curl_headers) curl_slist_free_all(curl_headers);
                curl_easy_cleanup(curl);

            } catch (const std::exception& e) {
                response["success"] = false;
                response["error"] = e.what();
            }
            return Message::create(msg.agent_id(), SyscallOp::SYS_HTTP, response.dump());
        });
}

std::string NetworkSyscalls::extract_domain(const std::string& url) {
    // Simple domain extraction: skip protocol, take host
    size_t start = url.find("://");
    if (start != std::string::npos) start += 3;
    else start = 0;

    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();

    // Strip port
    std::string host = url.substr(start, end - start);
    size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);

    return host;
}

} // namespace clove
