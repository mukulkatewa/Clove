#pragma once
#include <string>

namespace clove {

struct IsolationStatus {
    bool pid_namespace = false;
    bool net_namespace = false;
    bool mnt_namespace = false;
    bool uts_namespace = false;
    bool cgroups_available = false;
    bool memory_limit_applied = false;
    bool cpu_quota_applied = false;
    bool pids_limit_applied = false;
    bool landlock_active = false;
    bool seccomp_active = false;
    bool fully_isolated = false;
    std::string degraded_reason;

    bool is_degraded() const { return !fully_isolated && !degraded_reason.empty(); }
};

} // namespace clove
