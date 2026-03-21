#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clove {

struct A2aMessage {
    std::string from;          // External agent identifier
    std::string to;            // CLOVE agent name
    std::string content;
    nlohmann::json metadata;
    uint64_t timestamp_ms;
};

struct A2aAgentCard {
    std::string name;
    std::string description;
    std::vector<std::string> capabilities;
};

struct A2aSendResult {
    bool success = false;
    std::string response_content;
    int status_code = 0;
    std::string error;
    double duration_ms = 0;
};

class A2aBridge {
public:
    A2aBridge();
    ~A2aBridge();

    A2aBridge(const A2aBridge&) = delete;
    A2aBridge& operator=(const A2aBridge&) = delete;

    // Configure and start HTTP server
    bool start(uint16_t port);
    void stop();
    bool is_running() const { return running_; }

    // Register CLOVE agents for A2A discovery
    void register_agent(const std::string& name, const std::string& description,
                        const std::vector<std::string>& capabilities = {});
    void unregister_agent(const std::string& name);

    // Get inbound messages for a CLOVE agent
    std::vector<A2aMessage> receive(const std::string& agent_name, size_t max = 100);
    bool has_messages(const std::string& agent_name) const;

    // Send outbound message to external A2A agent
    A2aSendResult send(const std::string& target_url,
                       const std::string& from_agent,
                       const std::string& content,
                       const nlohmann::json& metadata = {});

    // Get agent card JSON
    nlohmann::json agent_card() const;

    uint16_t port() const { return port_; }

private:
    std::atomic<bool> running_{false};
    uint16_t port_ = 0;
    int server_fd_ = -1;
    std::thread server_thread_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, A2aAgentCard> agents_;
    std::unordered_map<std::string, std::deque<A2aMessage>> inbound_;

    void server_loop();
    void handle_connection(int client_fd);
    std::string build_response(int status, const std::string& body);
    std::string parse_request_body(const std::string& request);
    std::string parse_request_path(const std::string& request);
    std::string parse_request_method(const std::string& request);
};

} // namespace clove
