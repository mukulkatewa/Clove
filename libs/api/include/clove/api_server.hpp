#pragma once
#include <clove/config.hpp>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

namespace clove {

// Forward declarations — the API server references kernel subsystems
class AgentManager;
class StateStore;
class EventBus;
class PermissionsStore;
class InferenceGateway;
class PrivacyFilter;
class AuditLogger;
class ExecutionLogger;
class PolicyRecommender;
class McpBridge;
class A2aBridge;
class TunnelBridge;
class WorldEngine;

struct ApiContext {
    KernelConfig& config;
    AgentManager& agent_manager;
    StateStore& state_store;
    EventBus& event_bus;
    PermissionsStore& permissions_store;
    InferenceGateway& inference_gateway;
    PrivacyFilter& privacy_filter;
    AuditLogger& audit_logger;
    ExecutionLogger& execution_logger;
    PolicyRecommender& policy_recommender;
    McpBridge* mcp_bridge;      // nullable
    A2aBridge* a2a_bridge;      // nullable
    TunnelBridge* tunnel_bridge; // nullable
    WorldEngine* world_engine;   // nullable
};

class ApiServer {
public:
    explicit ApiServer(ApiContext ctx);
    ~ApiServer();

    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    bool start(uint16_t port, const std::string& api_key = "");
    void stop();
    bool is_running() const { return running_; }

private:
    ApiContext ctx_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> server_thread_;
    std::string api_key_;
    std::chrono::steady_clock::time_point start_time_;

    class Impl;
    std::unique_ptr<Impl> impl_;

    void setup_routes();
};

} // namespace clove
