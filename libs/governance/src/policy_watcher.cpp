#include <clove/policy_watcher.hpp>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

#if defined(__linux__)
#include <sys/inotify.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/event.h>
#include <sys/types.h>
#endif

namespace clove {

PolicyWatcher::PolicyWatcher(const std::string& file_path)
    : file_path_(file_path) {}

PolicyWatcher::~PolicyWatcher() {
    stop();
}

// ---------------------------------------------------------------------------
// Read file contents and invoke callback
// ---------------------------------------------------------------------------

void PolicyWatcher::deliver_file_contents() {
    if (!callback_) return;

    std::ifstream ifs(file_path_);
    if (!ifs.is_open()) return;

    std::ostringstream oss;
    oss << ifs.rdbuf();
    callback_(oss.str());
}

// ---------------------------------------------------------------------------
// Platform: Linux (inotify)
// ---------------------------------------------------------------------------

#if defined(__linux__)

int PolicyWatcher::start() {
    if (running_.load()) return watch_fd_;

    watch_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch_fd_ < 0) return -1;

    inotify_wd_ = inotify_add_watch(watch_fd_, file_path_.c_str(),
                                     IN_MODIFY | IN_CLOSE_WRITE);
    if (inotify_wd_ < 0) {
        ::close(watch_fd_);
        watch_fd_ = -1;
        return -1;
    }

    running_.store(true);
    return watch_fd_;
}

void PolicyWatcher::handle_event() {
    if (!running_.load() || watch_fd_ < 0) return;

    // Drain all pending inotify events
    alignas(struct inotify_event) char buf[4096];
    for (;;) {
        ssize_t n = ::read(watch_fd_, buf, sizeof(buf));
        if (n <= 0) break;

        // We don't need to inspect individual events — any modification
        // triggers a full re-read of the file.
    }

    deliver_file_contents();
}

void PolicyWatcher::stop() {
    running_.store(false);

    if (watch_fd_ >= 0) {
        if (inotify_wd_ >= 0) {
            inotify_rm_watch(watch_fd_, inotify_wd_);
            inotify_wd_ = -1;
        }
        ::close(watch_fd_);
        watch_fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// Platform: macOS (kqueue)
// ---------------------------------------------------------------------------

#elif defined(__APPLE__)

int PolicyWatcher::start() {
    if (running_.load()) return watch_fd_;

    watch_fd_ = kqueue();
    if (watch_fd_ < 0) return -1;

    file_fd_ = ::open(file_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (file_fd_ < 0) {
        ::close(watch_fd_);
        watch_fd_ = -1;
        return -1;
    }

    struct kevent change{};
    EV_SET(&change, static_cast<uintptr_t>(file_fd_), EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_ATTRIB | NOTE_RENAME, 0, nullptr);

    if (kevent(watch_fd_, &change, 1, nullptr, 0, nullptr) < 0) {
        ::close(file_fd_);
        ::close(watch_fd_);
        file_fd_  = -1;
        watch_fd_ = -1;
        return -1;
    }

    running_.store(true);
    return watch_fd_;
}

void PolicyWatcher::handle_event() {
    if (!running_.load() || watch_fd_ < 0) return;

    // Drain pending kqueue events
    struct kevent events[8];
    struct timespec timeout = {0, 0}; // non-blocking
    int n = kevent(watch_fd_, nullptr, 0, events, 8, &timeout);

    if (n > 0) {
        deliver_file_contents();
    }
}

void PolicyWatcher::stop() {
    running_.store(false);

    if (file_fd_ >= 0) {
        ::close(file_fd_);
        file_fd_ = -1;
    }
    if (watch_fd_ >= 0) {
        ::close(watch_fd_);
        watch_fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// Platform: Unsupported (stub)
// ---------------------------------------------------------------------------

#else

int PolicyWatcher::start() { return -1; }
void PolicyWatcher::handle_event() {}
void PolicyWatcher::stop() {}

#endif

} // namespace clove
