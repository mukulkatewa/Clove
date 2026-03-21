#pragma once

#include "kernel/src/context.hpp"
#include "kernel/src/syscall_router.hpp"
#include <clove/protocol.hpp>
#include <clove/execution_log.hpp>
#include <nlohmann/json.hpp>
#include <mutex>
#include <vector>

namespace clove {

// Base class for syscall handler modules
class KernelModule {
public:
    virtual ~KernelModule() = default;
    virtual void register_syscalls(SyscallRouter& router) = 0;
    virtual void on_tick() {}  // Called every reactor iteration
};

// ── Syscall module implementations ──────────────────────────────

class StateSyscalls : public KernelModule {
public:
    explicit StateSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class IpcSyscalls : public KernelModule {
public:
    explicit IpcSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class EventSyscalls : public KernelModule {
public:
    explicit EventSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class PermissionSyscalls : public KernelModule {
public:
    explicit PermissionSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class AuditSyscalls : public KernelModule {
public:
    explicit AuditSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class LlmSyscalls : public KernelModule {
public:
    explicit LlmSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class PiiSyscalls : public KernelModule {
public:
    explicit PiiSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class ReplaySyscalls : public KernelModule {
public:
    ReplaySyscalls(KernelContext& ctx, SyscallRouter* router)
        : ctx_(ctx), router_(router) {}
    void register_syscalls(SyscallRouter& router) override;
    void on_tick() override;
private:
    KernelContext& ctx_;
    SyscallRouter* router_;
    // Replay state
    ReplayState replay_state_ = ReplayState::IDLE;
    std::vector<ExecutionLogEntry> replay_entries_;
    size_t replay_index_ = 0;
};

class AgentSyscalls : public KernelModule {
public:
    explicit AgentSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class AsyncSyscalls : public KernelModule {
public:
    explicit AsyncSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class NetworkSyscalls : public KernelModule {
public:
    explicit NetworkSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
    static std::string extract_domain(const std::string& url);
};

class FileIoSyscalls : public KernelModule {
public:
    explicit FileIoSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class MetricsSyscalls : public KernelModule {
public:
    explicit MetricsSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class McpBridge;      // Forward declare
class A2aBridge;      // Forward declare
class TunnelBridge;   // Forward declare
class WorldEngine;    // Forward declare

class McpSyscalls : public KernelModule {
public:
    McpSyscalls(KernelContext& ctx, McpBridge* bridge) : ctx_(ctx), bridge_(bridge) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
    McpBridge* bridge_;
};

class OtelSyscalls : public KernelModule {
public:
    explicit OtelSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
    std::mutex spans_mutex_;
    std::vector<nlohmann::json> spans_;
};

class CredsSyscalls : public KernelModule {
public:
    explicit CredsSyscalls(KernelContext& ctx) : ctx_(ctx) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
};

class A2aSyscalls : public KernelModule {
public:
    A2aSyscalls(KernelContext& ctx, A2aBridge* bridge) : ctx_(ctx), bridge_(bridge) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
    A2aBridge* bridge_;
};

class TunnelSyscalls : public KernelModule {
public:
    TunnelSyscalls(KernelContext& ctx, TunnelBridge* bridge) : ctx_(ctx), bridge_(bridge) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
    TunnelBridge* bridge_;
};

class WorldSyscalls : public KernelModule {
public:
    WorldSyscalls(KernelContext& ctx, WorldEngine* engine) : ctx_(ctx), engine_(engine) {}
    void register_syscalls(SyscallRouter& router) override;
private:
    KernelContext& ctx_;
    WorldEngine* engine_;
};

} // namespace clove
