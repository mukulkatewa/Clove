#include "clove/types.hpp"

#include <stdexcept>

namespace clove {

const char* agent_state_to_string(AgentState s) noexcept {
    switch (s) {
        case AgentState::CREATED:  return "CREATED";
        case AgentState::STARTING: return "STARTING";
        case AgentState::RUNNING:  return "RUNNING";
        case AgentState::PAUSED:   return "PAUSED";
        case AgentState::STOPPING: return "STOPPING";
        case AgentState::STOPPED:  return "STOPPED";
        case AgentState::FAILED:   return "FAILED";
    }
    return "UNKNOWN";
}

const char* restart_policy_to_string(RestartPolicy p) noexcept {
    switch (p) {
        case RestartPolicy::NEVER:      return "never";
        case RestartPolicy::ALWAYS:     return "always";
        case RestartPolicy::ON_FAILURE: return "on_failure";
    }
    return "unknown";
}

RestartPolicy restart_policy_from_string(const std::string& s) {
    if (s == "never")      return RestartPolicy::NEVER;
    if (s == "always")     return RestartPolicy::ALWAYS;
    if (s == "on_failure") return RestartPolicy::ON_FAILURE;
    throw std::invalid_argument("Unknown restart policy: " + s);
}

} // namespace clove
