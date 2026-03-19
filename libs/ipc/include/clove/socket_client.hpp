#pragma once
#include <clove/protocol.hpp>
#include <string>
#include <optional>

namespace clove {

class SocketClient {
public:
    explicit SocketClient(const std::string& socket_path);
    ~SocketClient();

    SocketClient(const SocketClient&) = delete;
    SocketClient& operator=(const SocketClient&) = delete;

    bool connect();
    void disconnect();
    bool is_connected() const { return fd_ >= 0; }

    // Send a message and wait for response (synchronous)
    std::optional<Message> send(const Message& msg, int timeout_ms = 5000);

private:
    std::string socket_path_;
    int fd_ = -1;
};

} // namespace clove
