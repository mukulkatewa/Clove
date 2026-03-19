#include "clove/protocol.hpp"

namespace clove {

const char* opcode_to_string(SyscallOp op) noexcept {
    switch (op) {
        // Core
        case SyscallOp::SYS_NOOP:             return "SYS_NOOP";
        case SyscallOp::SYS_THINK:            return "SYS_THINK";
        case SyscallOp::SYS_EXEC:             return "SYS_EXEC";
        case SyscallOp::SYS_READ:             return "SYS_READ";
        case SyscallOp::SYS_WRITE:            return "SYS_WRITE";

        // Agent
        case SyscallOp::SYS_SPAWN:            return "SYS_SPAWN";
        case SyscallOp::SYS_KILL:             return "SYS_KILL";
        case SyscallOp::SYS_LIST:             return "SYS_LIST";
        case SyscallOp::SYS_PAUSE:            return "SYS_PAUSE";
        case SyscallOp::SYS_RESUME:           return "SYS_RESUME";

        // IPC
        case SyscallOp::SYS_SEND:             return "SYS_SEND";
        case SyscallOp::SYS_RECV:             return "SYS_RECV";
        case SyscallOp::SYS_BROADCAST:        return "SYS_BROADCAST";
        case SyscallOp::SYS_REGISTER:         return "SYS_REGISTER";

        // State
        case SyscallOp::SYS_STORE:            return "SYS_STORE";
        case SyscallOp::SYS_FETCH:            return "SYS_FETCH";
        case SyscallOp::SYS_DELETE:           return "SYS_DELETE";
        case SyscallOp::SYS_KEYS:             return "SYS_KEYS";

        // Permissions
        case SyscallOp::SYS_GET_PERMS:        return "SYS_GET_PERMS";
        case SyscallOp::SYS_SET_PERMS:        return "SYS_SET_PERMS";
        case SyscallOp::SYS_AUTH:             return "SYS_AUTH";
        case SyscallOp::SYS_POLICY_UPDATE:    return "SYS_POLICY_UPDATE";

        // Network
        case SyscallOp::SYS_HTTP:             return "SYS_HTTP";

        // Events
        case SyscallOp::SYS_SUBSCRIBE:        return "SYS_SUBSCRIBE";
        case SyscallOp::SYS_UNSUBSCRIBE:      return "SYS_UNSUBSCRIBE";
        case SyscallOp::SYS_POLL_EVENTS:      return "SYS_POLL_EVENTS";
        case SyscallOp::SYS_EMIT:             return "SYS_EMIT";

        // Replay
        case SyscallOp::SYS_RECORD_START:     return "SYS_RECORD_START";
        case SyscallOp::SYS_RECORD_STOP:      return "SYS_RECORD_STOP";
        case SyscallOp::SYS_RECORD_STATUS:    return "SYS_RECORD_STATUS";
        case SyscallOp::SYS_REPLAY_START:     return "SYS_REPLAY_START";
        case SyscallOp::SYS_REPLAY_STATUS:    return "SYS_REPLAY_STATUS";

        // Audit
        case SyscallOp::SYS_GET_AUDIT_LOG:    return "SYS_GET_AUDIT_LOG";
        case SyscallOp::SYS_SET_AUDIT_CONFIG: return "SYS_SET_AUDIT_CONFIG";

        // Async
        case SyscallOp::SYS_ASYNC_POLL:       return "SYS_ASYNC_POLL";

        // World
        case SyscallOp::SYS_WORLD_CREATE:     return "SYS_WORLD_CREATE";
        case SyscallOp::SYS_WORLD_DESTROY:    return "SYS_WORLD_DESTROY";
        case SyscallOp::SYS_WORLD_LIST:       return "SYS_WORLD_LIST";
        case SyscallOp::SYS_WORLD_JOIN:       return "SYS_WORLD_JOIN";
        case SyscallOp::SYS_WORLD_LEAVE:      return "SYS_WORLD_LEAVE";
        case SyscallOp::SYS_WORLD_EVENT:      return "SYS_WORLD_EVENT";
        case SyscallOp::SYS_WORLD_STATE:      return "SYS_WORLD_STATE";
        case SyscallOp::SYS_WORLD_SNAPSHOT:   return "SYS_WORLD_SNAPSHOT";
        case SyscallOp::SYS_WORLD_RESTORE:    return "SYS_WORLD_RESTORE";

        // Tunnel
        case SyscallOp::SYS_TUNNEL_CONNECT:      return "SYS_TUNNEL_CONNECT";
        case SyscallOp::SYS_TUNNEL_DISCONNECT:   return "SYS_TUNNEL_DISCONNECT";
        case SyscallOp::SYS_TUNNEL_STATUS:       return "SYS_TUNNEL_STATUS";
        case SyscallOp::SYS_TUNNEL_LIST_REMOTES: return "SYS_TUNNEL_LIST_REMOTES";
        case SyscallOp::SYS_TUNNEL_CONFIG:       return "SYS_TUNNEL_CONFIG";

        // Metrics
        case SyscallOp::SYS_METRICS_SYSTEM:      return "SYS_METRICS_SYSTEM";
        case SyscallOp::SYS_METRICS_AGENT:       return "SYS_METRICS_AGENT";
        case SyscallOp::SYS_METRICS_ALL_AGENTS:  return "SYS_METRICS_ALL_AGENTS";
        case SyscallOp::SYS_METRICS_CGROUP:      return "SYS_METRICS_CGROUP";

        // Inference Gateway
        case SyscallOp::SYS_LLM_CONFIG:          return "SYS_LLM_CONFIG";

        // v2 opcodes
        case SyscallOp::SYS_PII_SCAN:            return "SYS_PII_SCAN";
        case SyscallOp::SYS_PII_REDACT:          return "SYS_PII_REDACT";
        case SyscallOp::SYS_MCP_CALL:            return "SYS_MCP_CALL";
        case SyscallOp::SYS_MCP_LIST:            return "SYS_MCP_LIST";
        case SyscallOp::SYS_A2A_SEND:            return "SYS_A2A_SEND";
        case SyscallOp::SYS_A2A_RECV:            return "SYS_A2A_RECV";
        case SyscallOp::SYS_POLICY_RECOMMEND:    return "SYS_POLICY_RECOMMEND";
        case SyscallOp::SYS_CREDS_GET:           return "SYS_CREDS_GET";
        case SyscallOp::SYS_OTEL_SPAN:           return "SYS_OTEL_SPAN";

        // Meta
        case SyscallOp::SYS_LLM_REPORT:          return "SYS_LLM_REPORT";
        case SyscallOp::SYS_HELLO:               return "SYS_HELLO";
        case SyscallOp::SYS_EXIT:                return "SYS_EXIT";
    }
    return "UNKNOWN";
}

} // namespace clove
