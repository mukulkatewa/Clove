#pragma once

#include <clove/permissions.hpp>

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace clove {

class PermissionsStore {
public:
    AgentPermissions& get_or_create(uint32_t agent_id);
    void set_permissions(uint32_t agent_id, const AgentPermissions& perms);
    void set_level(uint32_t agent_id, PermissionLevel level);
    void remove(uint32_t agent_id);
    [[nodiscard]] bool exists(uint32_t agent_id) const;

    // Budget management
    AgentBudget& get_or_create_budget(uint32_t agent_id);
    void set_budget(uint32_t agent_id, const AgentBudget& budget);
    void remove_budget(uint32_t agent_id);

    // Iterate all budgets (for time budget checks in reactor loop)
    template<typename Fn>
    void for_each_budget(Fn&& fn) {
        std::lock_guard lock(mutex_);
        for (auto& [id, budget] : budgets_) {
            fn(id, budget);
        }
    }

private:
    std::unordered_map<uint32_t, AgentPermissions> permissions_;
    std::unordered_map<uint32_t, AgentBudget> budgets_;
    mutable std::mutex mutex_;
};

} // namespace clove
