#include <clove/socket_server.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace clove {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

SocketServer::SocketServer(const std::string& socket_path)
    : socket_path_(socket_path) {}

SocketServer::~SocketServer() {
    stop();
}

void SocketServer::set_handler(MessageHandler handler) {
    handler_ = std::move(handler);
}

// ---------------------------------------------------------------------------
// init — create, bind, listen on a Unix domain socket
// ---------------------------------------------------------------------------

bool SocketServer::init() {
    // Remove any stale socket file.
    ::unlink(socket_path_.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    // Guard against path overflow (sun_path is fixed-size).
    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    std::memcpy(addr.sun_path, socket_path_.c_str(), socket_path_.size() + 1);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 32) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        ::unlink(socket_path_.c_str());
        return false;
    }

    // Non-blocking so accept() / epoll integration works.
    int flags = ::fcntl(server_fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        ::unlink(socket_path_.c_str());
        return false;
    }

    // Owner-only access.
    ::chmod(socket_path_.c_str(), 0600);

    return true;
}

// ---------------------------------------------------------------------------
// stop — tear down the listening socket
// ---------------------------------------------------------------------------

void SocketServer::stop() {
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
    ::unlink(socket_path_.c_str());
}

// ---------------------------------------------------------------------------
// accept_connection — accept a new client, assign an agent id
// ---------------------------------------------------------------------------

int SocketServer::accept_connection() {
    int client_fd = ::accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0) {
        // EAGAIN / EWOULDBLOCK is expected in non-blocking mode.
        return -1;
    }

    // Set client fd to non-blocking.
    int flags = ::fcntl(client_fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ::close(client_fd);
        return -1;
    }

    ClientState state;
    state.agent_id = next_agent_id_++;
    clients_[client_fd] = std::move(state);

    return client_fd;
}

// ---------------------------------------------------------------------------
// handle_client — read available data and process complete messages
// ---------------------------------------------------------------------------

bool SocketServer::handle_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }

    uint8_t tmp[4096];
    for (;;) {
        ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            it->second.recv_buf.insert(it->second.recv_buf.end(), tmp, tmp + n);
        } else if (n == 0) {
            // Peer closed the connection.
            return false;
        } else {
            // EAGAIN means no more data right now — that's fine.
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            // Real error.
            return false;
        }
    }

    return process_messages(fd, it->second);
}

// ---------------------------------------------------------------------------
// process_messages — parse and dispatch all complete messages in recv_buf
// ---------------------------------------------------------------------------

bool SocketServer::process_messages(int fd, ClientState& state) {
    constexpr size_t hdr_size = sizeof(MessageHeader);

    while (state.recv_buf.size() >= hdr_size) {
        // Peek at the header to learn the payload length.
        MessageHeader hdr{};
        std::memcpy(&hdr, state.recv_buf.data(), hdr_size);

        if (hdr.magic != PROTOCOL_MAGIC) {
            // Corrupt stream — disconnect.
            return false;
        }

        if (hdr.payload_size > MAX_PAYLOAD_SIZE) {
            return false;
        }

        size_t total = hdr_size + hdr.payload_size;
        if (state.recv_buf.size() < total) {
            // Incomplete message — wait for more data.
            break;
        }

        // Full message available — deserialize.
        auto msg = Message::deserialize(state.recv_buf.data(), total);
        if (!msg) {
            return false;
        }

        // Override the agent_id with the server-assigned one.
        msg->header.agent_id = state.agent_id;

        // Dispatch to handler and queue the response.
        if (handler_) {
            Message response = handler_(*msg);
            auto wire = response.serialize();
            state.send_buf.insert(state.send_buf.end(), wire.begin(), wire.end());
        }

        // Consume the processed bytes.
        state.recv_buf.erase(state.recv_buf.begin(),
                             state.recv_buf.begin() + static_cast<ptrdiff_t>(total));
    }

    return true;
}

// ---------------------------------------------------------------------------
// flush_client — write pending response data
// ---------------------------------------------------------------------------

bool SocketServer::flush_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }

    auto& buf = it->second.send_buf;
    while (!buf.empty()) {
        ssize_t n = ::write(fd, buf.data(), buf.size());
        if (n > 0) {
            buf.erase(buf.begin(), buf.begin() + n);
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Kernel write buffer full — try again later.
                return true;
            }
            // Real error.
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// remove_client — close fd and clean up state
// ---------------------------------------------------------------------------

uint32_t SocketServer::remove_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        ::close(fd);
        return 0;
    }

    uint32_t agent_id = it->second.agent_id;
    clients_.erase(it);
    ::close(fd);
    return agent_id;
}

// ---------------------------------------------------------------------------
// client_wants_write — check if there is pending outbound data
// ---------------------------------------------------------------------------

bool SocketServer::client_wants_write(int fd) const {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return false;
    }
    return !it->second.send_buf.empty();
}

} // namespace clove
