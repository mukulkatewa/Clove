#pragma once
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace clove {

struct IpcMessage {
    uint32_t from_agent_id;
    uint32_t to_agent_id;
    std::string content;
    uint64_t timestamp_ms;
};

class AgentMailboxRegistry {
public:
    // Register an agent name → id mapping
    void register_agent(uint32_t agent_id, const std::string& name);
    void unregister(uint32_t agent_id);

    // Resolve name to id
    uint32_t resolve(const std::string& name) const;

    // Send message to a specific agent
    bool send(uint32_t from_id, uint32_t to_id, const std::string& content);
    bool send_by_name(uint32_t from_id, const std::string& to_name, const std::string& content);

    // Broadcast to all agents
    void broadcast(uint32_t from_id, const std::string& content);

    // Receive pending messages (drains queue, up to max_count)
    std::vector<IpcMessage> receive(uint32_t agent_id, size_t max_count = 100);

    // Check if agent has pending messages
    bool has_messages(uint32_t agent_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::queue<IpcMessage>> mailboxes_;
    std::unordered_map<std::string, uint32_t> name_to_id_;
    std::unordered_map<uint32_t, std::string> id_to_name_;

    uint64_t now_ms() const;
};

} // namespace clove
