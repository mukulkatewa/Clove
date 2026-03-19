#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace clove {

// ---------------------------------------------------------------------------
// Restart policy
// ---------------------------------------------------------------------------
enum class RestartPolicy : uint8_t {
    NEVER,
    ALWAYS,
    ON_FAILURE,
};

[[nodiscard]] const char* restart_policy_to_string(RestartPolicy p) noexcept;
[[nodiscard]] RestartPolicy restart_policy_from_string(const std::string& s);

struct RestartConfig {
    RestartPolicy policy              = RestartPolicy::NEVER;
    int           max_restarts        = 3;
    int           restart_window_sec  = 300;
    int           backoff_initial_ms  = 1000;
    int           backoff_max_ms      = 30000;
    double        backoff_multiplier  = 2.0;
};

// ---------------------------------------------------------------------------
// Resource limits
// ---------------------------------------------------------------------------
struct ResourceLimits {
    size_t   memory_limit_bytes = 256 * 1024 * 1024; // 256 MB
    uint64_t cpu_shares         = 1024;
    uint64_t cpu_quota_us       = 100000;
    uint64_t cpu_period_us      = 100000;
    int      max_pids           = 64;
};

// ---------------------------------------------------------------------------
// Agent configuration
// ---------------------------------------------------------------------------
struct AgentConfig {
    std::string    name;
    std::string    script_path;
    std::string    python_path    = "python3";
    std::string    socket_path;
    ResourceLimits limits;
    bool           sandboxed      = true;
    bool           enable_network = false;
    RestartConfig  restart;
};

// ---------------------------------------------------------------------------
// Agent state machine
// ---------------------------------------------------------------------------
enum class AgentState : uint8_t {
    CREATED,
    STARTING,
    RUNNING,
    PAUSED,
    STOPPING,
    STOPPED,
    FAILED,
};

[[nodiscard]] const char* agent_state_to_string(AgentState s) noexcept;

// ---------------------------------------------------------------------------
// Agent metrics
// ---------------------------------------------------------------------------
struct AgentMetrics {
    uint32_t             id               = 0;
    std::string          name;
    int                  pid              = -1;
    AgentState           state            = AgentState::CREATED;
    size_t               memory_bytes     = 0;
    double               cpu_percent      = 0.0;
    double               uptime_seconds   = 0.0;
    uint64_t             llm_request_count = 0;
    uint64_t             llm_tokens_used  = 0;
    uint32_t             parent_id        = 0;
    std::vector<uint32_t> child_ids;
    uint64_t             created_at_ms    = 0;
};

// ---------------------------------------------------------------------------
// Permission levels
// ---------------------------------------------------------------------------
enum class PermissionLevel : uint8_t {
    UNRESTRICTED,
    STANDARD,
    SANDBOXED,
    READONLY,
    MINIMAL,
};

} // namespace clove
