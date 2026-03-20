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

private:
    std::unordered_map<uint32_t, AgentPermissions> permissions_;
    mutable std::mutex mutex_;
};

} // namespace clove
