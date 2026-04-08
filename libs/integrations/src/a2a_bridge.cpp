#include <clove/a2a_bridge.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <curl/curl.h>
#include <chrono>

namespace clove {

A2aBridge::A2aBridge() = default;

A2aBridge::~A2aBridge() {
    stop();
}

// ── HTTP Server ────────────────────────────────────────────────

bool A2aBridge::start(uint16_t port) {
    if (running_) return false;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) return false;

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd_); server_fd_ = -1;
        return false;
    }

    if (listen(server_fd_, 16) < 0) {
        close(server_fd_); server_fd_ = -1;
        return false;
    }

    port_ = port;
    running_ = true;
    server_thread_ = std::thread(&A2aBridge::server_loop, this);
    return true;
}

void A2aBridge::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void A2aBridge::server_loop() {
    while (running_) {
        struct pollfd pfd;
        pfd.fd = server_fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 200); // 200ms timeout
        if (ret <= 0) continue;

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        handle_connection(client_fd);
        close(client_fd);
    }
}

void A2aBridge::handle_connection(int client_fd) {
    // Read HTTP request (simple: read up to 64KB)
    char buf[65536];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    std::string request(buf, static_cast<size_t>(n));

    std::string method = parse_request_method(request);
    std::string path = parse_request_path(request);

    std::string response;

    if (method == "GET" && path == "/.well-known/agent.json") {
        // Agent Card discovery
        auto card = agent_card();
        response = build_response(200, card.dump());
    }
    else if (method == "POST" && path == "/a2a/message") {
        // Inbound A2A message
        std::string body = parse_request_body(request);
        try {
            auto j = nlohmann::json::parse(body);
            std::string to = j.value("to", "");
            std::string from = j.value("from", "external");
            std::string content = j.value("content", "");
            nlohmann::json metadata = j.value("metadata", nlohmann::json::object());

            if (to.empty()) {
                response = build_response(400, R"({"error":"missing 'to' field"})");
            } else {
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                A2aMessage msg{from, to, content, metadata, static_cast<uint64_t>(now)};

                {
                    std::lock_guard lock(mutex_);
                    inbound_[to].push_back(std::move(msg));
                    // Cap at 1000 per agent
                    if (inbound_[to].size() > 1000) {
                        inbound_[to].pop_front();
                    }
                }

                nlohmann::json resp;
                resp["success"] = true;
                resp["delivered_to"] = to;
                response = build_response(200, resp.dump());
            }
        } catch (const std::exception& e) {
            response = build_response(400, std::string(R"({"error":"invalid JSON: )") + e.what() + "\"}");
        }
    }
    else if (method == "GET" && path == "/a2a/agents") {
        // List available agents
        auto card = agent_card();
        response = build_response(200, card.dump());
    }
    else {
        response = build_response(404, R"({"error":"not found"})");
    }

    write(client_fd, response.data(), response.size());
}

std::string A2aBridge::build_response(int status, const std::string& body) {
    std::string status_text;
    switch (status) {
        case 200: status_text = "OK"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        default: status_text = "Error"; break;
    }

    return "HTTP/1.1 " + std::to_string(status) + " " + status_text + "\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "\r\n" + body;
}

std::string A2aBridge::parse_request_method(const std::string& request) {
    auto sp = request.find(' ');
    return sp != std::string::npos ? request.substr(0, sp) : "";
}

std::string A2aBridge::parse_request_path(const std::string& request) {
    auto sp1 = request.find(' ');
    if (sp1 == std::string::npos) return "";
    auto sp2 = request.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return "";
    return request.substr(sp1 + 1, sp2 - sp1 - 1);
}

std::string A2aBridge::parse_request_body(const std::string& request) {
    auto pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return request.substr(pos + 4);
}

// ── Agent Registration ─────────────────────────────────────────

void A2aBridge::register_agent(const std::string& name, const std::string& description,
                                const std::vector<std::string>& capabilities) {
    std::lock_guard lock(mutex_);
    agents_[name] = {name, description, capabilities};
}

void A2aBridge::unregister_agent(const std::string& name) {
    std::lock_guard lock(mutex_);
    agents_.erase(name);
    inbound_.erase(name);
}

// ── Message Receive ────────────────────────────────────────────

std::vector<A2aMessage> A2aBridge::receive(const std::string& agent_name, size_t max) {
    std::lock_guard lock(mutex_);
    auto it = inbound_.find(agent_name);
    if (it == inbound_.end()) return {};

    std::vector<A2aMessage> result;
    size_t count = std::min(max, it->second.size());
    for (size_t i = 0; i < count; i++) {
        result.push_back(std::move(it->second.front()));
        it->second.pop_front();
    }
    return result;
}

bool A2aBridge::has_messages(const std::string& agent_name) const {
    std::lock_guard lock(mutex_);
    auto it = inbound_.find(agent_name);
    return it != inbound_.end() && !it->second.empty();
}

// ── Outbound Send ──────────────────────────────────────────────

static size_t a2a_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

A2aSendResult A2aBridge::send(const std::string& target_url,
                               const std::string& from_agent,
                               const std::string& content,
                               const nlohmann::json& metadata) {
    auto t0 = std::chrono::steady_clock::now();

    A2aSendResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "failed to init curl";
        return result;
    }

    nlohmann::json body;
    body["from"] = from_agent;
    body["content"] = content;
    if (!metadata.empty()) body["metadata"] = metadata;
    std::string body_str = body.dump();

    std::string response_body;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string url = target_url;
    if (url.back() != '/') url += "/";
    url += "a2a/message";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, a2a_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);

    auto t1 = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (res == CURLE_OK) {
        long status;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        result.status_code = static_cast<int>(status);
        result.success = (status >= 200 && status < 300);
        result.response_content = response_body;
    } else {
        result.error = curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

// ── Agent Card ─────────────────────────────────────────────────

nlohmann::json A2aBridge::agent_card() const {
    std::lock_guard lock(mutex_);

    nlohmann::json card;
    card["name"] = "clove";
    card["version"] = "2.0.0";
    card["protocol"] = "a2a";
    card["url"] = "http://localhost:" + std::to_string(port_);

    nlohmann::json agents_arr = nlohmann::json::array();
    for (const auto& [name, agent] : agents_) {
        agents_arr.push_back({
            {"name", agent.name},
            {"description", agent.description},
            {"capabilities", agent.capabilities}
        });
    }
    card["agents"] = agents_arr;
    return card;
}

} // namespace clove
