#include <clove/tunnel_bridge.hpp>
#include <random>
#include <sstream>
#include <iomanip>

namespace clove {

TunnelBridge::TunnelBridge() = default;
TunnelBridge::~TunnelBridge() = default;

static std::string generate_session_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    std::ostringstream ss;
    ss << "tun-" << std::hex << std::setfill('0')
       << std::setw(8) << dist(gen) << "-"
       << std::setw(8) << dist(gen);
    return ss.str();
}

bool TunnelBridge::connect(const TunnelConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config.relay_url.empty() || config.machine_id.empty()) {
        state_ = TunnelState::ERROR;
        return false;
    }
    config_ = config;
    // Framework-only: simulate connection (no real relay server)
    session_id_ = generate_session_id();
    state_ = TunnelState::CONNECTED;
    return true;
}

void TunnelBridge::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = TunnelState::DISCONNECTED;
    session_id_.clear();
    remotes_.clear();
}

TunnelState TunnelBridge::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string TunnelBridge::session_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_id_;
}

TunnelConfig TunnelBridge::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

nlohmann::json TunnelBridge::status_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j;
    switch (state_) {
        case TunnelState::DISCONNECTED: j["state"] = "disconnected"; break;
        case TunnelState::CONNECTING:   j["state"] = "connecting"; break;
        case TunnelState::CONNECTED:    j["state"] = "connected"; break;
        case TunnelState::ERROR:        j["state"] = "error"; break;
    }
    j["session_id"] = session_id_;
    j["relay_url"] = config_.relay_url;
    j["machine_id"] = config_.machine_id;
    return j;
}

std::vector<RemoteMachine> TunnelBridge::list_remotes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return remotes_;  // Empty until real relay exists
}

void TunnelBridge::update_config(const TunnelConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config.relay_url.empty()) config_.relay_url = config.relay_url;
    if (!config.machine_id.empty()) config_.machine_id = config.machine_id;
    if (!config.machine_token.empty()) config_.machine_token = config.machine_token;
    config_.auto_reconnect = config.auto_reconnect;
}

} // namespace clove
