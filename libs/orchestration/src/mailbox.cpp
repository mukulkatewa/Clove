#include <clove/mailbox.hpp>
#include <chrono>
#include <stdexcept>

namespace clove {

void AgentMailboxRegistry::register_agent(uint32_t agent_id, const std::string& name) {
    std::lock_guard lock(mutex_);

    // Remove any previous registration for this agent
    if (auto it = id_to_name_.find(agent_id); it != id_to_name_.end()) {
        name_to_id_.erase(it->second);
        id_to_name_.erase(it);
    }

    name_to_id_[name] = agent_id;
    id_to_name_[agent_id] = name;
    // Ensure the mailbox queue exists
    mailboxes_[agent_id];
}

void AgentMailboxRegistry::unregister(uint32_t agent_id) {
    std::lock_guard lock(mutex_);

    if (auto it = id_to_name_.find(agent_id); it != id_to_name_.end()) {
        name_to_id_.erase(it->second);
        id_to_name_.erase(it);
    }
    mailboxes_.erase(agent_id);
}

uint32_t AgentMailboxRegistry::resolve(const std::string& name) const {
    std::lock_guard lock(mutex_);

    auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) {
        return 0; // 0 = not found
    }
    return it->second;
}

bool AgentMailboxRegistry::send(uint32_t from_id, uint32_t to_id, const std::string& content) {
    std::lock_guard lock(mutex_);

    auto it = mailboxes_.find(to_id);
    if (it == mailboxes_.end()) {
        return false; // target agent not registered
    }

    IpcMessage msg{
        .from_agent_id = from_id,
        .to_agent_id   = to_id,
        .content        = content,
        .timestamp_ms   = now_ms(),
    };
    it->second.push(std::move(msg));
    return true;
}

bool AgentMailboxRegistry::send_by_name(uint32_t from_id, const std::string& to_name,
                                        const std::string& content) {
    std::lock_guard lock(mutex_);

    auto name_it = name_to_id_.find(to_name);
    if (name_it == name_to_id_.end()) {
        return false;
    }

    uint32_t to_id = name_it->second;
    auto mbox_it = mailboxes_.find(to_id);
    if (mbox_it == mailboxes_.end()) {
        return false;
    }

    IpcMessage msg{
        .from_agent_id = from_id,
        .to_agent_id   = to_id,
        .content        = content,
        .timestamp_ms   = now_ms(),
    };
    mbox_it->second.push(std::move(msg));
    return true;
}

void AgentMailboxRegistry::broadcast(uint32_t from_id, const std::string& content) {
    std::lock_guard lock(mutex_);

    uint64_t ts = now_ms();

    for (auto& [agent_id, queue] : mailboxes_) {
        if (agent_id == from_id) {
            continue; // don't send to self
        }
        IpcMessage msg{
            .from_agent_id = from_id,
            .to_agent_id   = agent_id,
            .content        = content,
            .timestamp_ms   = ts,
        };
        queue.push(std::move(msg));
    }
}

std::vector<IpcMessage> AgentMailboxRegistry::receive(uint32_t agent_id, size_t max_count) {
    std::lock_guard lock(mutex_);

    std::vector<IpcMessage> result;
    auto it = mailboxes_.find(agent_id);
    if (it == mailboxes_.end()) {
        return result;
    }

    auto& queue = it->second;
    while (!queue.empty() && result.size() < max_count) {
        result.push_back(std::move(queue.front()));
        queue.pop();
    }
    return result;
}

bool AgentMailboxRegistry::has_messages(uint32_t agent_id) const {
    std::lock_guard lock(mutex_);

    auto it = mailboxes_.find(agent_id);
    if (it == mailboxes_.end()) {
        return false;
    }
    return !it->second.empty();
}

uint64_t AgentMailboxRegistry::now_ms() const {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

} // namespace clove
