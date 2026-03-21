#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace clove {

enum class TunnelState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR,
};

struct TunnelConfig {
    std::string relay_url;
    std::string machine_id;
    std::string machine_token;
    bool auto_reconnect = false;
};

struct RemoteMachine {
    std::string machine_id;
    std::string name;
    std::string status;   // "online", "offline"
    uint64_t last_seen_ms = 0;
};

class TunnelBridge {
public:
    TunnelBridge();
    ~TunnelBridge();

    TunnelBridge(const TunnelBridge&) = delete;
    TunnelBridge& operator=(const TunnelBridge&) = delete;

    // Connection management
    bool connect(const TunnelConfig& config);
    void disconnect();

    // Status
    TunnelState state() const;
    std::string session_id() const;
    TunnelConfig config() const;
    nlohmann::json status_json() const;

    // Remote machines
    std::vector<RemoteMachine> list_remotes() const;

    // Config update
    void update_config(const TunnelConfig& config);

private:
    mutable std::mutex mutex_;
    TunnelState state_ = TunnelState::DISCONNECTED;
    TunnelConfig config_;
    std::string session_id_;
    std::vector<RemoteMachine> remotes_;
};

} // namespace clove
