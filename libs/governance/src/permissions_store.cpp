#include <clove/permissions_store.hpp>

namespace clove {

AgentPermissions& PermissionsStore::get_or_create(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    return permissions_[agent_id]; // default-inserts if missing
}

void PermissionsStore::set_permissions(uint32_t agent_id,
                                       const AgentPermissions& perms) {
    std::lock_guard lock(mutex_);
    permissions_[agent_id] = perms;
}

void PermissionsStore::set_level(uint32_t agent_id, PermissionLevel level) {
    std::lock_guard lock(mutex_);
    permissions_[agent_id] = AgentPermissions::from_level(level);
}

void PermissionsStore::remove(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    permissions_.erase(agent_id);
}

bool PermissionsStore::exists(uint32_t agent_id) const {
    std::lock_guard lock(mutex_);
    return permissions_.contains(agent_id);
}

// Budget management
AgentBudget& PermissionsStore::get_or_create_budget(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    return budgets_[agent_id];
}

void PermissionsStore::set_budget(uint32_t agent_id, const AgentBudget& budget) {
    std::lock_guard lock(mutex_);
    budgets_[agent_id] = budget;
}

void PermissionsStore::remove_budget(uint32_t agent_id) {
    std::lock_guard lock(mutex_);
    budgets_.erase(agent_id);
}

} // namespace clove
