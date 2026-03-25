#pragma once
#include <clove/sandbox.hpp>
#include <clove/audit_log.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <clove/config.hpp>

namespace clove {

/// Configuration for a single OpenClaw instance.
struct OpenClawInstanceConfig {
    std::string name = "openclaw";
    std::string soul;                              // Agent personality (SOUL.md content)
    std::string model;                             // LLM model override
    double budget_usd = 10.0;
    std::vector<std::string> channels;             // telegram, slack, whatsapp, discord, etc.
    std::vector<std::string> skills;               // research, coding, email, etc.
    std::vector<std::string> allowed_domains;      // Network allowlist
    std::vector<std::string> allowed_read_paths;   // Filesystem read ACL
    std::vector<std::string> allowed_write_paths;  // Filesystem write ACL
    int sandbox_memory_mb = 512;
    int sandbox_cpu_percent = 50;
};

/// Tracks a running OpenClaw instance.
struct OpenClawInstance {
    std::string id;                                // Unique instance ID
    std::string name;
    OpenClawInstanceConfig config;
    std::shared_ptr<Sandbox> sandbox;              // The kernel sandbox it runs in
    std::string config_dir;                        // Temp dir with openclaw.json + SOUL.md
    int api_port;                                  // Kernel API port for LLM routing
    std::string state = "starting";                // starting, running, stopped, failed
    double cost_usd = 0.0;
    int64_t started_at_ms = 0;
};

/// Manages OpenClaw instances running inside CLOVE sandboxes.
class OpenClawManager {
public:
    OpenClawManager(
        SandboxManager& sandbox_mgr,
        AuditLogger& audit,
        const KernelConfig& config
    );

    /// Spawn a single OpenClaw instance.
    /// Returns instance ID on success, empty string on failure.
    std::string spawn(const OpenClawInstanceConfig& cfg);

    /// Spawn multiple OpenClaw instances (fleet).
    std::vector<std::string> spawn_fleet(const std::vector<OpenClawInstanceConfig>& configs);

    /// Stop an instance.
    bool stop(const std::string& id);

    /// Stop all instances.
    void stop_all();

    /// Get instance status.
    const OpenClawInstance* get(const std::string& id) const;

    /// List all instances.
    std::vector<const OpenClawInstance*> list() const;

    /// Get instance count.
    size_t count() const;

private:
    SandboxManager& sandbox_mgr_;
    AuditLogger& audit_;
    const KernelConfig& config_;
    std::unordered_map<std::string, OpenClawInstance> instances_;
    mutable std::mutex mtx_;
    int next_port_ = 18800;  // OpenClaw gateway ports start here

    /// Find the OpenClaw binary.
    std::string find_openclaw_binary() const;

    /// Create a temp config directory for an instance.
    std::string create_config_dir(const OpenClawInstanceConfig& cfg, int api_port) const;

    /// Generate a unique instance ID.
    std::string generate_id() const;
};

} // namespace clove
