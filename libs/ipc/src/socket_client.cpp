#include <clove/socket_client.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <poll.h>

namespace clove {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

SocketClient::SocketClient(const std::string& socket_path)
    : socket_path_(socket_path) {}

SocketClient::~SocketClient() {
    disconnect();
}

// ---------------------------------------------------------------------------
// connect — open a blocking Unix-domain connection to the kernel
// ---------------------------------------------------------------------------

bool SocketClient::connect() {
    if (fd_ >= 0) {
        return true; // already connected
    }

    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    std::memcpy(addr.sun_path, socket_path_.c_str(), socket_path_.size() + 1);

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// disconnect
// ---------------------------------------------------------------------------

void SocketClient::disconnect() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// send — write a message and synchronously wait for the response
// ---------------------------------------------------------------------------

std::optional<Message> SocketClient::send(const Message& msg, int timeout_ms) {
    if (fd_ < 0) {
        return std::nullopt;
    }

    // Serialize and write the full request.
    auto wire = msg.serialize();
    size_t written = 0;
    while (written < wire.size()) {
        ssize_t n = ::write(fd_, wire.data() + written, wire.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::nullopt;
        }
        written += static_cast<size_t>(n);
    }

    // Read the response, using poll() to enforce the timeout.
    std::vector<uint8_t> buf;
    constexpr size_t hdr_size = sizeof(MessageHeader);

    pollfd pfd{};
    pfd.fd     = fd_;
    pfd.events = POLLIN;

    // We may need multiple reads: first the header, then the payload.
    while (true) {
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret <= 0) {
            // Timeout or error.
            return std::nullopt;
        }

        uint8_t tmp[4096];
        ssize_t n = ::read(fd_, tmp, sizeof(tmp));
        if (n <= 0) {
            return std::nullopt;
        }
        buf.insert(buf.end(), tmp, tmp + n);

        // Check if we have a full message yet.
        if (buf.size() < hdr_size) {
            continue;
        }

        MessageHeader hdr{};
        std::memcpy(&hdr, buf.data(), hdr_size);

        if (hdr.magic != PROTOCOL_MAGIC) {
            return std::nullopt;
        }
        if (hdr.payload_size > MAX_PAYLOAD_SIZE) {
            return std::nullopt;
        }

        size_t total = hdr_size + hdr.payload_size;
        if (buf.size() >= total) {
            return Message::deserialize(buf.data(), total);
        }

        // Still need more data — loop.
    }
}

} // namespace clove
