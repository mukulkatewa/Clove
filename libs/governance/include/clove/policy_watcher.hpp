#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace clove {

class PolicyWatcher {
public:
    using Callback = std::function<void(const std::string& contents)>;

    explicit PolicyWatcher(const std::string& file_path);
    ~PolicyWatcher();

    PolicyWatcher(const PolicyWatcher&)            = delete;
    PolicyWatcher& operator=(const PolicyWatcher&) = delete;

    void set_callback(Callback callback) { callback_ = std::move(callback); }

    /// Initialize the watcher and return its fd (for reactor integration).
    /// Returns -1 on failure.
    int start();

    /// Process a ready event on the watch fd.
    void handle_event();

    /// Stop watching.
    void stop();

    /// Get the watch fd for polling.
    [[nodiscard]] int fd() const { return watch_fd_; }

private:
    std::string        file_path_;
    int                watch_fd_ = -1;
    std::atomic<bool>  running_{false};
    Callback           callback_;

    void deliver_file_contents();

#if defined(__linux__)
    int inotify_wd_ = -1;
#elif defined(__APPLE__)
    int file_fd_ = -1;
#endif
};

} // namespace clove
