/**
 * CLOVE v2 Sandbox Implementation
 *
 * Process isolation using Linux namespaces (PID, NET, MNT, UTS),
 * cgroups v2 for resource limits, Landlock for filesystem restriction,
 * and seccomp for syscall filtering.
 *
 * On macOS: fork-only with egress proxy env vars (degraded mode).
 * Requires root/CAP_SYS_ADMIN for full isolation on Linux.
 */
#include <clove/sandbox.hpp>
#include <spdlog/spdlog.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>

#ifdef __linux__
#include <sys/mount.h>
#include <sched.h>
#endif

namespace fs = std::filesystem;

namespace clove {

#ifdef __linux__
// Stack size for clone()
constexpr size_t STACK_SIZE = 1024 * 1024; // 1MB

// Child process arguments passed through clone()
struct ChildArgs {
    Sandbox* sandbox;
    std::string command;
    std::vector<std::string> args;
    int pipe_fd[2]; // Synchronization pipe
};
#endif

// ============================================================================
// Sandbox Implementation
// ============================================================================

Sandbox::Sandbox(const SandboxConfig& config)
    : config_(config) {
    cgroup_path_ = "/sys/fs/cgroup/clove/" + config_.name;
}

Sandbox::~Sandbox() {
    if (state_ == SandboxState::RUNNING || state_ == SandboxState::PAUSED) {
        stop();
    }
    destroy();
}

bool Sandbox::create() {
    if (state_ != SandboxState::CREATED) {
        spdlog::error("Sandbox {} already created", config_.name);
        return false;
    }

    spdlog::info("Creating sandbox: {}", config_.name);

    if (config_.enable_cgroups) {
        if (!setup_cgroups()) {
            spdlog::error("Failed to setup cgroups for {}", config_.name);
            set_state(SandboxState::FAILED);
            return false;
        }
    }

    spdlog::debug("Sandbox {} created successfully", config_.name);
    return true;
}

// ============================================================================
// cgroups v2 Setup
// ============================================================================

bool Sandbox::setup_cgroups() {
    // Check if cgroup v2 is available
    if (!fs::exists("/sys/fs/cgroup/cgroup.controllers")) {
        spdlog::warn("DEGRADED ISOLATION: cgroup v2 not available - resource limits will NOT be enforced");
        isolation_status_.degraded_reason = "cgroup v2 not available";
        return true;
    }

    isolation_status_.cgroups_available = true;

    // Create clove cgroup directory if needed
    std::string clove_cgroup = "/sys/fs/cgroup/clove";
    if (!fs::exists(clove_cgroup)) {
        try {
            fs::create_directories(clove_cgroup);
        } catch (const std::exception& e) {
            spdlog::warn("DEGRADED ISOLATION: Cannot create cgroup dir (need root): {}", e.what());
            spdlog::warn("  -> Memory limits, CPU quotas, and PID limits will NOT be enforced");
            isolation_status_.degraded_reason = "Cannot create cgroup directory (need root)";
            return true; // Continue without cgroups
        }
    }

    // Create sandbox-specific cgroup
    if (!fs::exists(cgroup_path_)) {
        try {
            fs::create_directories(cgroup_path_);
        } catch (const std::exception& e) {
            spdlog::warn("DEGRADED ISOLATION: Cannot create sandbox cgroup (need root): {}", e.what());
            isolation_status_.degraded_reason = "Cannot create sandbox cgroup (need root)";
            return true;
        }
    }

    // Set memory limit
    std::string memory_max = cgroup_path_ + "/memory.max";
    if (fs::exists(memory_max)) {
        try {
            std::ofstream ofs(memory_max);
            if (ofs.is_open()) {
                ofs << config_.limits.memory_limit_bytes;
                if (ofs.good()) {
                    isolation_status_.memory_limit_applied = true;
                    spdlog::debug("Set memory limit: {} bytes", config_.limits.memory_limit_bytes);
                } else {
                    spdlog::warn("DEGRADED ISOLATION: Failed to write memory limit");
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("DEGRADED ISOLATION: Cannot set memory limit: {}", e.what());
        }
    } else {
        spdlog::warn("DEGRADED ISOLATION: memory.max not available - memory limit NOT enforced");
    }

    // Set CPU quota
    std::string cpu_max = cgroup_path_ + "/cpu.max";
    if (fs::exists(cpu_max)) {
        try {
            std::ofstream ofs(cpu_max);
            if (ofs.is_open()) {
                ofs << config_.limits.cpu_quota_us << " " << config_.limits.cpu_period_us;
                if (ofs.good()) {
                    isolation_status_.cpu_quota_applied = true;
                    spdlog::debug("Set CPU quota: {}us per {}us",
                        config_.limits.cpu_quota_us, config_.limits.cpu_period_us);
                } else {
                    spdlog::warn("DEGRADED ISOLATION: Failed to write CPU quota");
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("DEGRADED ISOLATION: Cannot set CPU quota: {}", e.what());
        }
    } else {
        spdlog::warn("DEGRADED ISOLATION: cpu.max not available - CPU quota NOT enforced");
    }

    // Set max PIDs
    std::string pids_max = cgroup_path_ + "/pids.max";
    if (fs::exists(pids_max)) {
        try {
            std::ofstream ofs(pids_max);
            if (ofs.is_open()) {
                ofs << config_.limits.max_pids;
                if (ofs.good()) {
                    isolation_status_.pids_limit_applied = true;
                    spdlog::debug("Set max PIDs: {}", config_.limits.max_pids);
                } else {
                    spdlog::warn("DEGRADED ISOLATION: Failed to write PID limit");
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("DEGRADED ISOLATION: Cannot set PID limit: {}", e.what());
        }
    } else {
        spdlog::warn("DEGRADED ISOLATION: pids.max not available - PID limit NOT enforced");
    }

    // Set CPU weight (cgroups v2 equivalent of cpu.shares)
    // cpu.weight range: 1-10000, default 100
    // cpu.shares range: 2-262144, default 1024
    // Conversion: weight = shares * 100 / 1024 (roughly)
    std::string cpu_weight = cgroup_path_ + "/cpu.weight";
    if (fs::exists(cpu_weight)) {
        try {
            uint64_t weight = (config_.limits.cpu_shares * 100) / 1024;
            if (weight < 1) weight = 1;
            if (weight > 10000) weight = 10000;

            std::ofstream ofs(cpu_weight);
            if (ofs.is_open()) {
                ofs << weight;
                if (ofs.good()) {
                    spdlog::debug("Set CPU weight: {} (from shares {})",
                        weight, config_.limits.cpu_shares);
                }
            }
        } catch (const std::exception& e) {
            spdlog::debug("Could not set CPU weight: {}", e.what());
        }
    }

    // Log summary of cgroup status
    if (!isolation_status_.memory_limit_applied ||
        !isolation_status_.cpu_quota_applied ||
        !isolation_status_.pids_limit_applied) {
        spdlog::warn("Sandbox {} running with partial cgroup limits: memory={}, cpu={}, pids={}",
            config_.name,
            isolation_status_.memory_limit_applied ? "ON" : "OFF",
            isolation_status_.cpu_quota_applied ? "ON" : "OFF",
            isolation_status_.pids_limit_applied ? "ON" : "OFF");
    }

    return true;
}

bool Sandbox::cleanup_cgroups() {
    if (fs::exists(cgroup_path_)) {
        try {
            fs::remove_all(cgroup_path_);
            spdlog::debug("Cleaned up cgroup: {}", cgroup_path_);
        } catch (const std::exception& e) {
            spdlog::warn("Failed to cleanup cgroup: {}", e.what());
        }
    }
    return true;
}

// ============================================================================
// Linux: clone() with namespace isolation + Landlock + seccomp
// ============================================================================

#ifdef __linux__

int Sandbox::child_entry(void* arg) {
    ChildArgs* child_args = static_cast<ChildArgs*>(arg);
    Sandbox* sandbox = child_args->sandbox;

    // Close write end of pipe
    close(child_args->pipe_fd[1]);

    // Wait for parent to set up cgroups
    char buf;
    if (read(child_args->pipe_fd[0], &buf, 1) < 0) {
        spdlog::warn("Failed to read sync signal from parent: {}", strerror(errno));
    }
    close(child_args->pipe_fd[0]);

    // Set hostname if UTS namespace is enabled
    if (sandbox->config_.enable_uts_namespace) {
        std::string hostname = "clove-" + sandbox->config_.name;
        if (sethostname(hostname.c_str(), hostname.length()) < 0) {
            spdlog::warn("Failed to set hostname: {}", strerror(errno));
        }
    }

    // Mount proc if PID + MNT namespaces are enabled
    if (sandbox->config_.enable_pid_namespace && sandbox->config_.enable_mount_namespace) {
        if (mount("proc", "/proc", "proc", 0, nullptr) < 0) {
            spdlog::debug("Could not mount /proc (may need root)");
        }
    }

    // Apply Landlock filesystem restrictions (v2)
    if (sandbox->config_.enable_landlock) {
        if (!sandbox->apply_landlock()) {
            spdlog::warn("Landlock not available or failed to apply - continuing without filesystem restrictions");
        }
    }

    // Apply seccomp syscall filtering (v2)
    if (sandbox->config_.enable_seccomp) {
        if (!sandbox->apply_seccomp()) {
            spdlog::warn("seccomp not available or failed to apply - continuing without syscall filtering");
        }
    }

    // Set egress proxy env vars if configured (defense-in-depth)
    const char* proxy = getenv("CLOVE_EGRESS_PROXY");
    if (proxy && proxy[0]) {
        setenv("HTTP_PROXY", proxy, 1);
        setenv("HTTPS_PROXY", proxy, 1);
        setenv("http_proxy", proxy, 1);
        setenv("https_proxy", proxy, 1);
    }

    // Build argv for execvp
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(child_args->command.c_str()));
    for (auto& a : child_args->args) {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    execvp(argv[0], argv.data());
    spdlog::error("execvp failed: {}", strerror(errno));
    _exit(127);
}

#endif // __linux__

// ============================================================================
// start() — Platform-specific process creation
// ============================================================================

bool Sandbox::start(const std::string& command, const std::vector<std::string>& args) {
    if (state_ == SandboxState::RUNNING) {
        spdlog::error("Sandbox {} already running", config_.name);
        return false;
    }

    spdlog::info("Starting sandbox {} with command: {}", config_.name, command);

#ifdef __linux__
    // === Linux: try clone() with namespace isolation, fall back to fork() ===

    // Create synchronization pipe
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        spdlog::error("Failed to create pipe: {}", strerror(errno));
        set_state(SandboxState::FAILED);
        return false;
    }

    // Prepare child arguments
    ChildArgs child_args;
    child_args.sandbox = this;
    child_args.command = command;
    child_args.args = args;
    child_args.pipe_fd[0] = pipe_fd[0];
    child_args.pipe_fd[1] = pipe_fd[1];

    // Build clone flags
    int clone_flags = SIGCHLD;

    if (config_.enable_pid_namespace) {
        clone_flags |= CLONE_NEWPID;
    }
    if (config_.enable_mount_namespace) {
        clone_flags |= CLONE_NEWNS;
    }
    if (config_.enable_uts_namespace) {
        clone_flags |= CLONE_NEWUTS;
    }
    if (!config_.enable_network) {
        clone_flags |= CLONE_NEWNET;
    }

    // Allocate stack for child
    char* stack = new char[STACK_SIZE];
    char* stack_top = stack + STACK_SIZE;

    // Clone the process with namespace isolation
    child_pid_ = clone(child_entry, stack_top, clone_flags, &child_args);

    if (child_pid_ < 0) {
        spdlog::error("clone() failed: {}", strerror(errno));
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        delete[] stack;

        // Fallback to fork if clone fails (typically means no root/CAP_SYS_ADMIN)
        spdlog::warn("DEGRADED ISOLATION: clone() failed, falling back to fork()");
        spdlog::warn("  -> Namespace isolation (PID, NET, MNT, UTS) will NOT be available");
        spdlog::warn("  -> Run as root or with CAP_SYS_ADMIN for full isolation");

        isolation_status_.degraded_reason = "clone() failed - no namespace isolation (need root/CAP_SYS_ADMIN)";

        child_pid_ = fork();

        if (child_pid_ < 0) {
            spdlog::error("fork() failed: {}", strerror(errno));
            set_state(SandboxState::FAILED);
            return false;
        }

        if (child_pid_ == 0) {
            // Child process (degraded fork path)

            // Still try Landlock and seccomp — they work without root
            if (config_.enable_landlock) {
                apply_landlock(); // Best-effort
            }
            if (config_.enable_seccomp) {
                apply_seccomp(); // Best-effort
            }

            // Set egress proxy env vars
            const char* proxy = getenv("CLOVE_EGRESS_PROXY");
            if (proxy && proxy[0]) {
                setenv("HTTP_PROXY", proxy, 1);
                setenv("HTTPS_PROXY", proxy, 1);
                setenv("http_proxy", proxy, 1);
                setenv("https_proxy", proxy, 1);
            }

            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(command.c_str()));
            for (const auto& a : args) {
                argv.push_back(const_cast<char*>(a.c_str()));
            }
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        // Parent (degraded fork path)
        isolation_status_.fully_isolated = false;

        // Add child to cgroup even in degraded mode
        if (config_.enable_cgroups && fs::exists(cgroup_path_ + "/cgroup.procs")) {
            try {
                std::ofstream ofs(cgroup_path_ + "/cgroup.procs");
                if (ofs.is_open()) {
                    ofs << child_pid_;
                }
            } catch (...) {}
        }

        set_state(SandboxState::RUNNING);
        spdlog::warn("Sandbox {} started in DEGRADED MODE (PID={}, no namespace isolation)",
            config_.name, child_pid_);
        return true;
    }

    // clone() succeeded — namespace isolation is active
    if (config_.enable_pid_namespace)   isolation_status_.pid_namespace = true;
    if (config_.enable_mount_namespace) isolation_status_.mnt_namespace = true;
    if (config_.enable_uts_namespace)   isolation_status_.uts_namespace = true;
    if (!config_.enable_network)        isolation_status_.net_namespace = true;

    // Parent: close read end of sync pipe
    close(pipe_fd[0]);

    // Add child to cgroup before signaling it to continue
    bool cgroup_assigned = false;
    if (config_.enable_cgroups && fs::exists(cgroup_path_ + "/cgroup.procs")) {
        try {
            std::ofstream ofs(cgroup_path_ + "/cgroup.procs");
            if (ofs.is_open()) {
                ofs << child_pid_;
                if (ofs.good()) {
                    cgroup_assigned = true;
                    spdlog::debug("Added PID {} to cgroup {}", child_pid_, cgroup_path_);
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("DEGRADED ISOLATION: Failed to add process to cgroup: {}", e.what());
        }
    }

    if (config_.enable_cgroups && !cgroup_assigned) {
        spdlog::warn("DEGRADED ISOLATION: Process {} not added to cgroup - resource limits NOT enforced",
            child_pid_);
        isolation_status_.memory_limit_applied = false;
        isolation_status_.cpu_quota_applied = false;
        isolation_status_.pids_limit_applied = false;
    }

    // Signal child to continue (it will apply Landlock + seccomp then exec)
    if (write(pipe_fd[1], "x", 1) < 0) {
        spdlog::warn("Failed to write sync signal to child: {}", strerror(errno));
    }
    close(pipe_fd[1]);

    delete[] stack;

    // Determine overall isolation status
    bool namespaces_ok = (!config_.enable_pid_namespace || isolation_status_.pid_namespace) &&
                         (!config_.enable_mount_namespace || isolation_status_.mnt_namespace) &&
                         (!config_.enable_uts_namespace || isolation_status_.uts_namespace) &&
                         (config_.enable_network || isolation_status_.net_namespace);

    bool cgroups_ok = !config_.enable_cgroups ||
                      (isolation_status_.memory_limit_applied &&
                       isolation_status_.cpu_quota_applied &&
                       isolation_status_.pids_limit_applied);

    // Note: Landlock and seccomp are applied in the child process, so we
    // optimistically assume they will succeed if the kernel supports them.
    // The child will log warnings if they fail.
    isolation_status_.fully_isolated = namespaces_ok && cgroups_ok;

    set_state(SandboxState::RUNNING);

    if (isolation_status_.fully_isolated) {
        spdlog::info("Sandbox {} started with FULL isolation (PID={})", config_.name, child_pid_);
    } else {
        spdlog::warn("Sandbox {} started with PARTIAL isolation (PID={})", config_.name, child_pid_);
        spdlog::warn("  Namespaces: pid={}, mnt={}, uts={}, net={}",
            isolation_status_.pid_namespace ? "ON" : "OFF",
            isolation_status_.mnt_namespace ? "ON" : "OFF",
            isolation_status_.uts_namespace ? "ON" : "OFF",
            isolation_status_.net_namespace ? "ON" : "OFF");
        spdlog::warn("  Cgroups: memory={}, cpu={}, pids={}",
            isolation_status_.memory_limit_applied ? "ON" : "OFF",
            isolation_status_.cpu_quota_applied ? "ON" : "OFF",
            isolation_status_.pids_limit_applied ? "ON" : "OFF");
    }

    return true;

#else
    // === macOS / other platforms: fork() only, no namespace isolation ===

    spdlog::warn("DEGRADED ISOLATION: Namespace isolation not available on this platform");
    spdlog::warn("  -> Using fork() — no PID, NET, MNT, UTS isolation");

    isolation_status_.degraded_reason = "Platform does not support Linux namespaces";

    child_pid_ = fork();

    if (child_pid_ < 0) {
        spdlog::error("fork() failed: {}", strerror(errno));
        set_state(SandboxState::FAILED);
        return false;
    }

    if (child_pid_ == 0) {
        // Child process

        // Set egress proxy env vars (macOS defense-in-depth)
        const char* proxy = getenv("CLOVE_EGRESS_PROXY");
        if (proxy && proxy[0]) {
            setenv("HTTP_PROXY", proxy, 1);
            setenv("HTTPS_PROXY", proxy, 1);
            setenv("http_proxy", proxy, 1);
            setenv("https_proxy", proxy, 1);
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(command.c_str()));
        for (const auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent
    isolation_status_.fully_isolated = false;

    set_state(SandboxState::RUNNING);
    spdlog::warn("Sandbox {} started in DEGRADED MODE (PID={}, fork-only)",
        config_.name, child_pid_);
    return true;
#endif
}

// ============================================================================
// Lifecycle: stop, destroy, pause, resume, wait
// ============================================================================

bool Sandbox::stop(int timeout_ms) {
    if (state_ != SandboxState::RUNNING && state_ != SandboxState::PAUSED) {
        return true;
    }

    spdlog::info("Stopping sandbox {} (PID={})", config_.name, child_pid_);

    // If paused, resume first so it can receive SIGTERM
    if (state_ == SandboxState::PAUSED) {
        kill(child_pid_, SIGCONT);
    }

    // Send SIGTERM for graceful shutdown
    if (kill(child_pid_, SIGTERM) < 0) {
        if (errno == ESRCH) {
            // Process already dead
            set_state(SandboxState::STOPPED);
            return true;
        }
        spdlog::error("kill(SIGTERM) failed: {}", strerror(errno));
    }

    // Wait for process to exit with timeout (polling at 100ms intervals)
    int waited = 0;
    constexpr int interval = 100; // 100ms

    while (waited < timeout_ms) {
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);

        if (result == child_pid_) {
            exit_code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            set_state(SandboxState::STOPPED);
            spdlog::info("Sandbox {} stopped (exit={})", config_.name, exit_code_);
            return true;
        }

        if (result < 0 && errno != EINTR) {
            break;
        }

        usleep(interval * 1000);
        waited += interval;
    }

    // Force kill after timeout
    spdlog::warn("Sandbox {} not responding, sending SIGKILL", config_.name);
    kill(child_pid_, SIGKILL);
    waitpid(child_pid_, nullptr, 0);

    set_state(SandboxState::STOPPED);
    return true;
}

bool Sandbox::destroy() {
    if (state_ == SandboxState::RUNNING || state_ == SandboxState::PAUSED) {
        stop();
    }

    cleanup_cgroups();
    spdlog::debug("Sandbox {} destroyed", config_.name);
    return true;
}

bool Sandbox::pause() {
    if (state_ != SandboxState::RUNNING) {
        spdlog::error("Cannot pause sandbox {} - not running (state={})",
                      config_.name, static_cast<int>(state_));
        return false;
    }

    if (child_pid_ <= 0) {
        spdlog::error("Cannot pause sandbox {} - no valid PID", config_.name);
        return false;
    }

    spdlog::info("Pausing sandbox {} (PID={})", config_.name, child_pid_);

    if (kill(child_pid_, SIGSTOP) < 0) {
        if (errno == ESRCH) {
            spdlog::error("Process {} no longer exists", child_pid_);
            set_state(SandboxState::STOPPED);
            return false;
        }
        spdlog::error("kill(SIGSTOP) failed: {}", strerror(errno));
        return false;
    }

    set_state(SandboxState::PAUSED);
    spdlog::info("Sandbox {} paused", config_.name);
    return true;
}

bool Sandbox::resume() {
    if (state_ != SandboxState::PAUSED) {
        spdlog::error("Cannot resume sandbox {} - not paused (state={})",
                      config_.name, static_cast<int>(state_));
        return false;
    }

    if (child_pid_ <= 0) {
        spdlog::error("Cannot resume sandbox {} - no valid PID", config_.name);
        return false;
    }

    spdlog::info("Resuming sandbox {} (PID={})", config_.name, child_pid_);

    if (kill(child_pid_, SIGCONT) < 0) {
        if (errno == ESRCH) {
            spdlog::error("Process {} no longer exists", child_pid_);
            set_state(SandboxState::STOPPED);
            return false;
        }
        spdlog::error("kill(SIGCONT) failed: {}", strerror(errno));
        return false;
    }

    set_state(SandboxState::RUNNING);
    spdlog::info("Sandbox {} resumed", config_.name);
    return true;
}

int Sandbox::wait() {
    if (child_pid_ <= 0) {
        return -1;
    }

    int status;
    waitpid(child_pid_, &status, 0);

    if (WIFEXITED(status)) {
        exit_code_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code_ = 128 + WTERMSIG(status);
    }

    set_state(SandboxState::STOPPED);
    return exit_code_;
}

bool Sandbox::is_running() const {
    if (child_pid_ <= 0 || (state_ != SandboxState::RUNNING && state_ != SandboxState::PAUSED)) {
        return false;
    }

    // Use waitpid with WNOHANG to reap zombies and check liveness
    int status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);

    if (result == child_pid_) {
        // Child has exited — update state (const_cast needed for const method)
        Sandbox* mutable_this = const_cast<Sandbox*>(this);
        if (WIFEXITED(status)) {
            mutable_this->exit_code_ = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            mutable_this->exit_code_ = 128 + WTERMSIG(status);
        }
        mutable_this->state_ = SandboxState::STOPPED;
        return false;
    } else if (result == 0) {
        return true; // Still running
    } else {
        return false; // Error — process doesn't exist
    }
}

// ============================================================================
// Hot-reload resource limits
// ============================================================================

bool Sandbox::update_resource_limits(const ResourceLimits& new_limits) {
    if (state_ != SandboxState::RUNNING && state_ != SandboxState::PAUSED) {
        spdlog::warn("Cannot update limits for sandbox {} - not running", config_.name);
        return false;
    }

    config_.limits = new_limits;
    bool all_ok = true;

    // Update memory limit
    std::string memory_max = cgroup_path_ + "/memory.max";
    if (fs::exists(memory_max)) {
        try {
            std::ofstream ofs(memory_max);
            if (ofs.is_open()) {
                ofs << new_limits.memory_limit_bytes;
                if (ofs.good()) {
                    spdlog::info("Updated memory limit for {}: {} bytes",
                        config_.name, new_limits.memory_limit_bytes);
                } else { all_ok = false; }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to update memory limit for {}: {}", config_.name, e.what());
            all_ok = false;
        }
    }

    // Update CPU quota
    std::string cpu_max = cgroup_path_ + "/cpu.max";
    if (fs::exists(cpu_max)) {
        try {
            std::ofstream ofs(cpu_max);
            if (ofs.is_open()) {
                ofs << new_limits.cpu_quota_us << " " << new_limits.cpu_period_us;
                if (ofs.good()) {
                    spdlog::info("Updated CPU quota for {}: {}us/{}us",
                        config_.name, new_limits.cpu_quota_us, new_limits.cpu_period_us);
                } else { all_ok = false; }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to update CPU quota for {}: {}", config_.name, e.what());
            all_ok = false;
        }
    }

    // Update PID limit
    std::string pids_max = cgroup_path_ + "/pids.max";
    if (fs::exists(pids_max)) {
        try {
            std::ofstream ofs(pids_max);
            if (ofs.is_open()) {
                ofs << new_limits.max_pids;
                if (ofs.good()) {
                    spdlog::info("Updated PID limit for {}: {}", config_.name, new_limits.max_pids);
                } else { all_ok = false; }
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to update PID limit for {}: {}", config_.name, e.what());
            all_ok = false;
        }
    }

    return all_ok;
}

// ============================================================================
// Event callback + state management
// ============================================================================

void Sandbox::set_event_callback(SandboxEventCallback callback) {
    event_callback_ = std::move(callback);
}

void Sandbox::set_state(SandboxState new_state) {
    state_ = new_state;
    if (event_callback_) {
        event_callback_(this, new_state);
    }
}

// ============================================================================
// SandboxManager Implementation
// ============================================================================

SandboxManager::SandboxManager() {
    cgroup_root_ = "/sys/fs/cgroup/clove";
    init_cgroup_root();
}

SandboxManager::~SandboxManager() {
    cleanup_all();
}

bool SandboxManager::is_available() {
    // Always return true — we fall back to fork() if namespaces are unavailable
    return true;
}

bool SandboxManager::init_cgroup_root() {
    if (!fs::exists("/sys/fs/cgroup/cgroup.controllers")) {
        spdlog::warn("cgroup v2 not available");
        return false;
    }

    if (!fs::exists(cgroup_root_)) {
        try {
            fs::create_directories(cgroup_root_);
            spdlog::info("Created cgroup root: {}", cgroup_root_);
        } catch (const std::exception& e) {
            spdlog::warn("Cannot create cgroup root (need root): {}", e.what());
            return false;
        }
    }

    // Enable controllers for child cgroups
    std::string subtree_control = "/sys/fs/cgroup/cgroup.subtree_control";
    if (fs::exists(subtree_control)) {
        try {
            std::ofstream(subtree_control) << "+cpu +memory +pids";
        } catch (...) {
            spdlog::debug("Could not enable cgroup controllers");
        }
    }

    return true;
}

std::shared_ptr<Sandbox> SandboxManager::create_sandbox(const SandboxConfig& config) {
    if (sandboxes_.count(config.name)) {
        spdlog::error("Sandbox {} already exists", config.name);
        return nullptr;
    }

    auto sandbox = std::make_shared<Sandbox>(config);
    if (!sandbox->create()) {
        return nullptr;
    }

    sandboxes_[config.name] = sandbox;
    return sandbox;
}

std::shared_ptr<Sandbox> SandboxManager::get_sandbox(const std::string& name) {
    auto it = sandboxes_.find(name);
    if (it != sandboxes_.end()) {
        return it->second;
    }
    return nullptr;
}

bool SandboxManager::remove_sandbox(const std::string& name) {
    auto it = sandboxes_.find(name);
    if (it == sandboxes_.end()) {
        return false;
    }

    it->second->destroy();
    sandboxes_.erase(it);
    return true;
}

std::vector<std::string> SandboxManager::list_sandboxes() const {
    std::vector<std::string> names;
    names.reserve(sandboxes_.size());
    for (const auto& [name, _] : sandboxes_) {
        names.push_back(name);
    }
    return names;
}

void SandboxManager::cleanup_all() {
    for (auto& [name, sandbox] : sandboxes_) {
        sandbox->destroy();
    }
    sandboxes_.clear();

    // Cleanup cgroup root
    if (fs::exists(cgroup_root_)) {
        try {
            fs::remove_all(cgroup_root_);
        } catch (...) {}
    }
}

} // namespace clove
