#include <clove/reactor.hpp>
#include <unistd.h>
#include <cerrno>

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#else
#error "Unsupported platform"
#endif

namespace clove {

Reactor::Reactor() = default;

Reactor::~Reactor() {
    if (poll_fd_ >= 0) {
        ::close(poll_fd_);
        poll_fd_ = -1;
    }
    running_ = false;
}

void Reactor::stop() {
    running_ = false;
}

// ---------------------------------------------------------------------------
// Linux — epoll
// ---------------------------------------------------------------------------
#ifdef __linux__

bool Reactor::init() {
    poll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (poll_fd_ < 0) return false;
    running_ = true;
    return true;
}

static uint32_t to_epoll_events(uint32_t ev) {
    uint32_t out = 0;
    if (ev & static_cast<uint32_t>(EventType::READABLE)) out |= EPOLLIN;
    if (ev & static_cast<uint32_t>(EventType::WRITABLE)) out |= EPOLLOUT;
    if (ev & static_cast<uint32_t>(EventType::ERROR))    out |= EPOLLERR;
    if (ev & static_cast<uint32_t>(EventType::HANGUP))   out |= EPOLLHUP;
    return out;
}

static uint32_t from_epoll_events(uint32_t ep) {
    uint32_t out = 0;
    if (ep & EPOLLIN)  out |= static_cast<uint32_t>(EventType::READABLE);
    if (ep & EPOLLOUT) out |= static_cast<uint32_t>(EventType::WRITABLE);
    if (ep & EPOLLERR) out |= static_cast<uint32_t>(EventType::ERROR);
    if (ep & EPOLLHUP) out |= static_cast<uint32_t>(EventType::HANGUP);
    return out;
}

bool Reactor::add(int fd, uint32_t events, EventCallback callback) {
    struct epoll_event ev{};
    ev.events = to_epoll_events(events);
    ev.data.fd = fd;

    if (epoll_ctl(poll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) return false;
    callbacks_[fd] = std::move(callback);
    return true;
}

bool Reactor::modify(int fd, uint32_t events) {
    struct epoll_event ev{};
    ev.events = to_epoll_events(events);
    ev.data.fd = fd;

    return epoll_ctl(poll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool Reactor::remove(int fd) {
    if (epoll_ctl(poll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) return false;
    callbacks_.erase(fd);
    return true;
}

int Reactor::poll(int timeout_ms) {
    static constexpr int kMaxEvents = 64;
    struct epoll_event events[kMaxEvents];

    int n = epoll_wait(poll_fd_, events, kMaxEvents, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        auto it = callbacks_.find(fd);
        if (it != callbacks_.end()) {
            it->second(fd, from_epoll_events(events[i].events));
        }
    }

    return n;
}

// ---------------------------------------------------------------------------
// macOS — kqueue
// ---------------------------------------------------------------------------
#elif defined(__APPLE__)

bool Reactor::init() {
    poll_fd_ = kqueue();
    if (poll_fd_ < 0) return false;
    running_ = true;
    return true;
}

bool Reactor::add(int fd, uint32_t events, EventCallback callback) {
    struct kevent changelist[2];
    int nchanges = 0;

    if (events & static_cast<uint32_t>(EventType::READABLE) ||
        events & static_cast<uint32_t>(EventType::HANGUP)) {
        EV_SET(&changelist[nchanges], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        ++nchanges;
    }

    if (events & static_cast<uint32_t>(EventType::WRITABLE)) {
        EV_SET(&changelist[nchanges], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        ++nchanges;
    }

    if (nchanges == 0) return false;

    if (kevent(poll_fd_, changelist, nchanges, nullptr, 0, nullptr) < 0) return false;
    callbacks_[fd] = std::move(callback);
    return true;
}

bool Reactor::modify(int fd, uint32_t events) {
    // kqueue has no modify — delete old filters, add new ones.
    struct kevent changelist[4];
    int nchanges = 0;

    // Remove both filters first.
    EV_SET(&changelist[nchanges], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    ++nchanges;
    EV_SET(&changelist[nchanges], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    ++nchanges;

    // Ignore errors from delete — filter may not have been registered.
    kevent(poll_fd_, changelist, nchanges, nullptr, 0, nullptr);

    // Now add the requested filters.
    nchanges = 0;

    if (events & static_cast<uint32_t>(EventType::READABLE) ||
        events & static_cast<uint32_t>(EventType::HANGUP)) {
        EV_SET(&changelist[nchanges], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        ++nchanges;
    }

    if (events & static_cast<uint32_t>(EventType::WRITABLE)) {
        EV_SET(&changelist[nchanges], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        ++nchanges;
    }

    if (nchanges == 0) return false;

    return kevent(poll_fd_, changelist, nchanges, nullptr, 0, nullptr) == 0;
}

bool Reactor::remove(int fd) {
    struct kevent changelist[2];

    EV_SET(&changelist[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changelist[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

    // Ignore individual errors — one filter may not be registered.
    kevent(poll_fd_, changelist, 2, nullptr, 0, nullptr);

    callbacks_.erase(fd);
    return true;
}

int Reactor::poll(int timeout_ms) {
    static constexpr int kMaxEvents = 64;
    struct kevent events[kMaxEvents];

    struct timespec ts;
    struct timespec* ts_ptr = nullptr;

    if (timeout_ms >= 0) {
        ts.tv_sec  = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        ts_ptr = &ts;
    }

    int n = kevent(poll_fd_, nullptr, 0, events, kMaxEvents, ts_ptr);
    if (n < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    for (int i = 0; i < n; ++i) {
        int fd = static_cast<int>(events[i].ident);
        uint32_t mapped = 0;

        if (events[i].filter == EVFILT_READ)  mapped |= static_cast<uint32_t>(EventType::READABLE);
        if (events[i].filter == EVFILT_WRITE) mapped |= static_cast<uint32_t>(EventType::WRITABLE);
        if (events[i].flags & EV_EOF)         mapped |= static_cast<uint32_t>(EventType::HANGUP);
        if (events[i].flags & EV_ERROR)       mapped |= static_cast<uint32_t>(EventType::ERROR);

        auto it = callbacks_.find(fd);
        if (it != callbacks_.end()) {
            it->second(fd, mapped);
        }
    }

    return n;
}

#endif // platform

} // namespace clove
