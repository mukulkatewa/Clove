#pragma once
#include <functional>
#include <unordered_map>
#include <cstdint>

namespace clove {

enum class EventType : uint32_t {
    READABLE  = 0x001,
    WRITABLE  = 0x004,
    ERROR     = 0x008,
    HANGUP    = 0x010
};

inline uint32_t operator|(EventType a, EventType b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}
inline uint32_t operator|(uint32_t a, EventType b) {
    return a | static_cast<uint32_t>(b);
}

using EventCallback = std::function<void(int fd, uint32_t events)>;

class Reactor {
public:
    Reactor();
    ~Reactor();

    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    bool init();
    bool add(int fd, uint32_t events, EventCallback callback);
    bool modify(int fd, uint32_t events);
    bool remove(int fd);
    int poll(int timeout_ms = -1);
    void stop();
    bool is_running() const { return running_; }

private:
    int poll_fd_ = -1;
    bool running_ = false;
    std::unordered_map<int, EventCallback> callbacks_;
};

} // namespace clove
