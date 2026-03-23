#pragma once

#include <clove/config.hpp>

// Forward declarations for all subsystems
namespace clove {

class Reactor;
class SocketServer;
class AgentManager;
class AgentMailboxRegistry;
class StateStore;
class EventBus;
class LlmQueue;
class AsyncTaskManager;
class PermissionsStore;
class InferenceGateway;
class PrivacyFilter;
class AuditLogger;
class ExecutionLogger;
class PolicyWatcher;
class PolicyRecommender;
class AgentScheduler;

// Reference bundle passed to all syscall handlers
struct KernelContext {
    KernelConfig& config;
    Reactor& reactor;
    SocketServer& socket_server;
    AgentManager& agent_manager;
    AgentMailboxRegistry& mailbox_registry;
    StateStore& state_store;
    EventBus& event_bus;
    LlmQueue& llm_queue;
    AsyncTaskManager& async_tasks;
    PermissionsStore& permissions_store;
    InferenceGateway& inference_gateway;
    PrivacyFilter& privacy_filter;
    AuditLogger& audit_logger;
    ExecutionLogger& execution_logger;
    PolicyRecommender& policy_recommender;
    AgentScheduler* scheduler = nullptr;  // optional, may be null
};

} // namespace clove
