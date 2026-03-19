#pragma once
#include <clove/resource_limits.hpp>
#include <clove/isolation_status.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <sys/types.h>

namespace clove {

struct SandboxConfig {
    std::string name;
    std::string root_path = "/tmp";
    std::string socket_path;
    ResourceLimits limits;
    bool enable_network = false;
    bool enable_pid_namespace = true;
    bool enable_mount_namespace = true;
    bool enable_uts_namespace = true;
    bool enable_cgroups = true;
    bool enable_landlock = true;     // NEW v2
    bool enable_seccomp = true;      // NEW v2
    std::vector<std::string> allowed_paths;     // For Landlock
    std::vector<std::string> writable_paths;    // For Landlock
};

enum class SandboxState {
    CREATED, RUNNING, PAUSED, STOPPED, FAILED
};

class Sandbox;
using SandboxEventCallback = std::function<void(Sandbox*, SandboxState)>;

class Sandbox {
public:
    explicit Sandbox(const SandboxConfig& config);
    ~Sandbox();

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    bool create();
    bool start(const std::string& command, const std::vector<std::string>& args = {});
    bool stop(int timeout_ms = 5000);
    bool destroy();
    bool pause();
    bool resume();
    bool update_resource_limits(const ResourceLimits& new_limits);

    SandboxState state() const { return state_; }
    pid_t pid() const { return child_pid_; }
    const std::string& name() const { return config_.name; }
    const SandboxConfig& config() const { return config_; }
    const IsolationStatus& isolation_status() const { return isolation_status_; }
    int wait();
    bool is_running() const;
    int exit_code() const { return exit_code_; }
    void set_event_callback(SandboxEventCallback callback);

private:
    SandboxConfig config_;
    SandboxState state_ = SandboxState::CREATED;
    pid_t child_pid_ = -1;
    int exit_code_ = -1;
    SandboxEventCallback event_callback_;
    IsolationStatus isolation_status_;
    std::string cgroup_path_;

    bool setup_cgroups();
    bool cleanup_cgroups();
    void set_state(SandboxState new_state);

#ifdef __linux__
    static int child_entry(void* arg);
    bool apply_landlock();
    bool apply_seccomp();
#endif
};

class SandboxManager {
public:
    SandboxManager();
    ~SandboxManager();
    static bool is_available();
    std::shared_ptr<Sandbox> create_sandbox(const SandboxConfig& config);
    std::shared_ptr<Sandbox> get_sandbox(const std::string& name);
    bool remove_sandbox(const std::string& name);
    std::vector<std::string> list_sandboxes() const;
    void cleanup_all();

private:
    std::unordered_map<std::string, std::shared_ptr<Sandbox>> sandboxes_;
    std::string cgroup_root_;
    bool init_cgroup_root();
};

} // namespace clove
