#pragma once
#include <clove/protocol.hpp>
#include <string>
#include <queue>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <atomic>
#include <cstdint>

namespace clove {

struct AsyncResult {
    uint64_t request_id;
    SyscallOp opcode;
    std::string payload;
};

using AsyncTaskFn = std::function<std::string()>;

class AsyncTaskManager {
public:
    explicit AsyncTaskManager(size_t worker_count = 8);
    ~AsyncTaskManager();

    AsyncTaskManager(const AsyncTaskManager&) = delete;
    AsyncTaskManager& operator=(const AsyncTaskManager&) = delete;

    // Submit an async task, returns request_id
    uint64_t submit(uint32_t agent_id, SyscallOp opcode, AsyncTaskFn task);

    // Poll completed results for an agent
    std::vector<AsyncResult> poll(uint32_t agent_id, size_t max_results = 10);

    // Shutdown
    void shutdown();

private:
    struct Task {
        uint32_t agent_id;
        uint64_t request_id;
        SyscallOp opcode;
        AsyncTaskFn fn;
    };

    std::vector<std::thread> workers_;
    std::queue<Task> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> next_request_id_{1};

    std::mutex results_mutex_;
    std::unordered_map<uint32_t, std::queue<AsyncResult>> results_;

    void worker_loop();
};

} // namespace clove
