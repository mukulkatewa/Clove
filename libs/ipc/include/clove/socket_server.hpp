#pragma once
#include <clove/protocol.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace clove {

using MessageHandler = std::function<Message(const Message&)>;

class SocketServer {
public:
    explicit SocketServer(const std::string& socket_path);
    ~SocketServer();

    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;

    void set_handler(MessageHandler handler);
    bool init();
    void stop();

    int get_server_fd() const { return server_fd_; }
    int accept_connection();
    bool handle_client(int fd);
    bool flush_client(int fd);
    uint32_t remove_client(int fd);
    bool client_wants_write(int fd) const;

private:
    std::string socket_path_;
    int server_fd_ = -1;
    MessageHandler handler_;
    uint32_t next_agent_id_ = 1;

    struct ClientState {
        uint32_t agent_id = 0;
        std::vector<uint8_t> recv_buf;
        std::vector<uint8_t> send_buf;
    };
    std::unordered_map<int, ClientState> clients_;

    bool process_messages(int fd, ClientState& state);
};

} // namespace clove
