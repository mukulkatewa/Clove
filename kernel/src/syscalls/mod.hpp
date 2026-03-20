#pragma once

#include "kernel/src/context.hpp"
#include "kernel/src/syscall_router.hpp"
#include <clove/protocol.hpp>
#include <nlohmann/json.hpp>

namespace clove {

// Base class for syscall handler modules
class KernelModule {
public:
    virtual ~KernelModule() = default;
    virtual void register_syscalls(SyscallRouter& router) = 0;
    virtual void on_tick() {}  // Called every reactor iteration
};

// Forward declarations for all syscall modules
class AgentSyscalls;
class LlmSyscalls;
class IpcSyscalls;
class StateSyscalls;
class EventSyscalls;
class PermissionSyscalls;
class NetworkSyscalls;
class AuditSyscalls;
class ReplaySyscalls;
class MetricsSyscalls;
class AsyncSyscalls;

} // namespace clove
