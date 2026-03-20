#include "kernel.hpp"
#include "context.hpp"
#include "syscall_router.hpp"
#include "syscalls/mod.hpp"

#include <clove/reactor.hpp>
#include <clove/socket_server.hpp>
#include <clove/agent_manager.hpp>
#include <clove/mailbox.hpp>
#include <clove/state_store.hpp>
#include <clove/event_bus.hpp>
#include <clove/llm_queue.hpp>
#include <clove/async_tasks.hpp>
#include <clove/permissions_store.hpp>
#include <clove/inference_gateway.hpp>
#include <clove/privacy_filter.hpp>
#include <clove/audit_log.hpp>
#include <clove/execution_log.hpp>
#include <clove/policy_watcher.hpp>
#include <clove/policy_recommender.hpp>
#include <clove/manifest.hpp>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <sys/wait.h>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace clove {

// Signal handling
static Kernel* g_kernel = nullptr;
static int g_sigchld_pipe[2] = {-1, -1};

static void signal_handler(int) {
    if (g_kernel) g_kernel->shutdown();
}

static void sigchld_handler(int) {
    if (g_sigchld_pipe[1] >= 0) {
        char c = 1;
        (void)write(g_sigchld_pipe[1], &c, 1);
    }
}

Kernel::Kernel() : Kernel(KernelConfig{}) {}

Kernel::Kernel(const KernelConfig& config) : config_(config) {
    // Create all subsystems
    reactor_ = std::make_unique<Reactor>();
    socket_server_ = std::make_unique<SocketServer>(config.socket_path);
    agent_manager_ = std::make_unique<AgentManager>(config.socket_path);
    mailbox_registry_ = std::make_unique<AgentMailboxRegistry>();
    state_store_ = std::make_unique<StateStore>();
    event_bus_ = std::make_unique<EventBus>();
    llm_queue_ = std::make_unique<LlmQueue>(config.llm_worker_count);
    async_tasks_ = std::make_unique<AsyncTaskManager>(config.async_worker_count);
    permissions_store_ = std::make_unique<PermissionsStore>();
    inference_gateway_ = std::make_unique<InferenceGateway>();
    privacy_filter_ = std::make_unique<PrivacyFilter>();
    audit_logger_ = std::make_unique<AuditLogger>();
    execution_logger_ = std::make_unique<ExecutionLogger>();
    policy_recommender_ = std::make_unique<PolicyRecommender>();

    // Configure inference gateway
    if (config.llm_proxy_enabled) {
        InferenceGatewayConfig gw_config;
        gw_config.enabled = true;
        gw_config.allowed_providers = config.llm_allowed_providers;
        gw_config.allowed_models = config.llm_allowed_models;
        gw_config.max_cost_usd = config.llm_max_cost_usd;
        inference_gateway_->configure(gw_config);
    }

    // Configure privacy filter
    if (config.privacy_enabled) {
        PrivacyFilterConfig pf_config;
        pf_config.enabled = true;
        pf_config.mode = PrivacyFilter::mode_from_string(config.privacy_mode);
        pf_config.enabled_patterns = config.privacy_patterns;
        privacy_filter_->configure(pf_config);
    }

    // Create kernel context (reference bundle for syscall handlers)
    context_ = std::make_unique<KernelContext>(KernelContext{
        config_,
        *reactor_,
        *socket_server_,
        *agent_manager_,
        *mailbox_registry_,
        *state_store_,
        *event_bus_,
        *llm_queue_,
        *async_tasks_,
        *permissions_store_,
        *inference_gateway_,
        *privacy_filter_,
        *audit_logger_,
        *execution_logger_,
        *policy_recommender_
    });

    // Create syscall router with built-in handlers
    syscall_router_ = std::make_unique<SyscallRouter>();

    // Register built-in handlers
    syscall_router_->register_handler(SyscallOp::SYS_NOOP,
        [](const Message& msg) {
            return Message::create(msg.header.agent_id, SyscallOp::SYS_NOOP, msg.payload);
        });

    syscall_router_->register_handler(SyscallOp::SYS_EXIT,
        [](const Message& msg) {
            return Message::create(msg.header.agent_id, SyscallOp::SYS_EXIT, "goodbye");
        });

    syscall_router_->register_handler(SyscallOp::SYS_HELLO,
        [](const Message& msg) {
            json response;
            response["success"] = true;
            response["protocol_version"] = PROTOCOL_VERSION;
            response["kernel_version"] = "2.0.0";
            response["features"] = {
                {"multi_agent", true},
                {"execution_replay", true},
                {"inference_gateway", true},
                {"privacy_filter", true},
                {"policy_hot_reload", true},
                {"mcp_bridge", false},
                {"a2a_bridge", false},
                {"otel_export", false}
            };
            return Message::create(msg.header.agent_id, SyscallOp::SYS_HELLO, response.dump());
        });

    // Register module syscall handlers
    for (auto& module : modules_) {
        module->register_syscalls(*syscall_router_);
    }
}

Kernel::~Kernel() {
    if (g_kernel == this) {
        g_kernel = nullptr;
        if (g_sigchld_pipe[0] >= 0) { close(g_sigchld_pipe[0]); g_sigchld_pipe[0] = -1; }
        if (g_sigchld_pipe[1] >= 0) { close(g_sigchld_pipe[1]); g_sigchld_pipe[1] = -1; }
    }
}

bool Kernel::init() {
    spdlog::info("Initializing CLOVE Kernel v2.0.0...");

    if (!reactor_->init()) {
        spdlog::error("Failed to initialize reactor");
        return false;
    }

    socket_server_->set_handler([this](const Message& msg) {
        return handle_message(msg);
    });

    if (!socket_server_->init()) {
        spdlog::error("Failed to initialize socket server");
        return false;
    }

    int server_fd = socket_server_->get_server_fd();
    reactor_->add(server_fd, static_cast<uint32_t>(EventType::READABLE),
        [this](int fd, uint32_t events) { on_server_event(fd, events); });

    // Signal handlers
    g_kernel = this;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // SIGCHLD self-pipe for async child reaping
    if (pipe(g_sigchld_pipe) == 0) {
        fcntl(g_sigchld_pipe[0], F_SETFL, fcntl(g_sigchld_pipe[0], F_GETFL) | O_NONBLOCK);
        fcntl(g_sigchld_pipe[1], F_SETFL, fcntl(g_sigchld_pipe[1], F_GETFL) | O_NONBLOCK);

        reactor_->add(g_sigchld_pipe[0], static_cast<uint32_t>(EventType::READABLE),
            [this](int fd, uint32_t) {
                char buf[64];
                while (read(fd, buf, sizeof(buf)) > 0) {}
                int status;
                pid_t pid;
                while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                    agent_manager_->notify_child_exit(pid, status);
                }
            });

        struct sigaction sa;
        sa.sa_handler = sigchld_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        sigaction(SIGCHLD, &sa, nullptr);
    }

    // Policy file watcher
    if (!config_.policy_file.empty()) {
        policy_watcher_ = std::make_unique<PolicyWatcher>(config_.policy_file);
        policy_watcher_->set_callback([this](const std::string& contents) {
            on_policy_change(contents);
        });
        int watch_fd = policy_watcher_->start();
        if (watch_fd >= 0) {
            reactor_->add(watch_fd, static_cast<uint32_t>(EventType::READABLE),
                [this](int, uint32_t) { policy_watcher_->handle_event(); });
            spdlog::info("Policy watcher active: {}", config_.policy_file);
        }
    }

    // Load deployment manifest
    if (!config_.manifest_file.empty()) {
        if (!load_manifest(config_.manifest_file)) {
            spdlog::error("Failed to load manifest: {}", config_.manifest_file);
            return false;
        }
    }

    spdlog::info("Kernel initialized successfully");
    spdlog::info("Socket: {}", config_.socket_path);
    spdlog::info("Sandboxing: {}", config_.enable_sandboxing ? "enabled" : "disabled");
    spdlog::info("Inference gateway: {}", config_.llm_proxy_enabled ? "enabled" : "disabled");
    spdlog::info("Privacy filter: {}", config_.privacy_enabled ? "enabled" : "disabled");
    return true;
}

void Kernel::run() {
    running_ = true;
    spdlog::info("CLOVE Kernel v2.0.0 running on {}", config_.socket_path);

    while (running_) {
        int n = reactor_->poll(100);
        if (n < 0) {
            spdlog::error("Reactor error, exiting");
            break;
        }

        for (auto& module : modules_) {
            module->on_tick();
        }

        agent_manager_->reap_and_restart_agents();
        agent_manager_->process_pending_restarts();
        state_store_->evict_expired();
    }

    spdlog::info("Kernel shutting down...");
    if (policy_watcher_) policy_watcher_->stop();
    agent_manager_->stop_all();
    socket_server_->stop();
    llm_queue_->shutdown();
    async_tasks_->shutdown();
    spdlog::info("Kernel stopped");
}

void Kernel::shutdown() {
    running_ = false;
}

AgentManager& Kernel::agents() {
    return *agent_manager_;
}

void Kernel::on_server_event(int, uint32_t events) {
    if (events & static_cast<uint32_t>(EventType::READABLE)) {
        while (true) {
            int client_fd = socket_server_->accept_connection();
            if (client_fd < 0) break;

            reactor_->add(client_fd,
                static_cast<uint32_t>(EventType::READABLE) |
                static_cast<uint32_t>(EventType::HANGUP) |
                static_cast<uint32_t>(EventType::ERROR),
                [this](int cfd, uint32_t ev) { on_client_event(cfd, ev); });
        }
    }
}

void Kernel::on_client_event(int fd, uint32_t events) {
    if (events & (static_cast<uint32_t>(EventType::HANGUP) | static_cast<uint32_t>(EventType::ERROR))) {
        reactor_->remove(fd);
        uint32_t agent_id = socket_server_->remove_client(fd);
        if (agent_id > 0) mailbox_registry_->unregister(agent_id);
        return;
    }

    if (events & static_cast<uint32_t>(EventType::READABLE)) {
        if (!socket_server_->handle_client(fd)) {
            reactor_->remove(fd);
            uint32_t agent_id = socket_server_->remove_client(fd);
            if (agent_id > 0) mailbox_registry_->unregister(agent_id);
            return;
        }
    }

    if (events & static_cast<uint32_t>(EventType::WRITABLE)) {
        if (!socket_server_->flush_client(fd)) {
            reactor_->remove(fd);
            uint32_t agent_id = socket_server_->remove_client(fd);
            if (agent_id > 0) mailbox_registry_->unregister(agent_id);
            return;
        }
    }

    update_client_events(fd);
}

void Kernel::update_client_events(int fd) {
    uint32_t events = static_cast<uint32_t>(EventType::READABLE) |
                      static_cast<uint32_t>(EventType::HANGUP) |
                      static_cast<uint32_t>(EventType::ERROR);
    if (socket_server_->client_wants_write(fd)) {
        events |= static_cast<uint32_t>(EventType::WRITABLE);
    }
    reactor_->modify(fd, events);
}

Message Kernel::handle_message(const Message& msg) {
    // Record in execution log
    auto start = std::chrono::steady_clock::now();
    Message response = syscall_router_->handle(msg);
    auto end = std::chrono::steady_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    execution_logger_->record(
        msg.header.agent_id, msg.opcode(),
        msg.payload_str(), response.payload_str(),
        static_cast<uint64_t>(duration_us), true);

    return response;
}

bool Kernel::load_manifest(const std::string& path) {
    spdlog::info("Loading manifest: {}", path);
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::stringstream ss;
    ss << file.rdbuf();

    try {
        auto manifest = parse_manifest(ss.str());
        spdlog::info("Manifest '{}' v{} loaded (sha256={}...)",
            manifest.name, manifest.version, manifest.sha256_digest.substr(0, 16));

        audit_logger_->log(AuditCategory::SECURITY, "MANIFEST_LOADED", 0, "kernel", {
            {"name", manifest.name}, {"version", manifest.version},
            {"sha256", manifest.sha256_digest}
        });

        // Apply LLM policy
        if (!manifest.policy.llm_allowed_providers.empty()) {
            InferenceGatewayConfig gw;
            gw.enabled = true;
            gw.allowed_providers = manifest.policy.llm_allowed_providers;
            gw.max_cost_usd = manifest.policy.llm_max_cost_usd;
            inference_gateway_->configure(gw);
        }

        // Spawn agents
        for (const auto& def : manifest.agents) {
            if (def.command.empty()) continue;
            AgentConfig ac;
            ac.name = def.name;
            ac.script_path = def.command;
            ac.socket_path = config_.socket_path;
            auto agent = agent_manager_->spawn_agent(ac);
            if (agent) {
                auto perms = AgentPermissions::from_level(
                    def.level == "unrestricted" ? PermissionLevel::UNRESTRICTED :
                    def.level == "sandboxed" ? PermissionLevel::SANDBOXED :
                    def.level == "readonly" ? PermissionLevel::READONLY :
                    def.level == "minimal" ? PermissionLevel::MINIMAL :
                    PermissionLevel::STANDARD);
                perms.can_think = def.can_think;
                permissions_store_->set_permissions(agent->id(), perms);
                spdlog::info("Manifest: spawned '{}'", def.name);
            }
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse manifest: {}", e.what());
        return false;
    }
}

void Kernel::on_policy_change(const std::string& contents) {
    spdlog::info("Policy file changed, applying updates...");
    try {
        json policy = json::parse(contents);

        if (policy.contains("llm")) {
            inference_gateway_->update_config(policy["llm"]);
        }

        if (policy.contains("agents") && policy["agents"].is_object()) {
            for (auto& [name, agent_policy] : policy["agents"].items()) {
                auto agent = agent_manager_->get_agent(name);
                if (!agent) continue;

                if (agent_policy.contains("permissions")) {
                    auto current = permissions_store_->get_or_create(agent->id());
                    auto delta = agent_policy["permissions"];
                    if (delta.contains("can_exec")) current.can_exec = delta["can_exec"].get<bool>();
                    if (delta.contains("can_read")) current.can_read = delta["can_read"].get<bool>();
                    if (delta.contains("can_write")) current.can_write = delta["can_write"].get<bool>();
                    if (delta.contains("can_think")) current.can_think = delta["can_think"].get<bool>();
                    if (delta.contains("can_http")) current.can_http = delta["can_http"].get<bool>();
                    if (delta.contains("allowed_domains")) {
                        current.allowed_domains.clear();
                        for (const auto& d : delta["allowed_domains"])
                            current.allowed_domains.push_back(d.get<std::string>());
                    }
                    permissions_store_->set_permissions(agent->id(), current);
                    spdlog::info("Policy update applied to '{}'", name);
                }
            }
        }

        audit_logger_->log(AuditCategory::SECURITY, "POLICY_RELOADED", 0, "kernel",
            {{"source", "file_watcher"}});
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse policy: {}", e.what());
    }
}

} // namespace clove
