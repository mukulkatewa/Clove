#pragma once
#include <string>
#include <queue>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <atomic>
#include <functional>
#include <cstdint>

namespace clove {

struct LlmRequest {
    uint32_t agent_id;
    std::string payload;
    std::promise<std::string> promise;
};

// Callback type for actually calling the LLM API
// Takes (agent_id, prompt) -> returns response string (or throws)
using LlmBackend = std::function<std::string(uint32_t agent_id, const std::string& prompt)>;

class LlmQueue {
public:
    explicit LlmQueue(size_t worker_count = 8);
    ~LlmQueue();

    LlmQueue(const LlmQueue&) = delete;
    LlmQueue& operator=(const LlmQueue&) = delete;

    // Set the backend that actually calls the LLM
    void set_backend(LlmBackend backend);

    // Submit a request, get a future for the result
    std::future<std::string> submit(uint32_t agent_id, const std::string& prompt);

    // Shutdown the queue (waits for workers to finish)
    void shutdown();

    // Stats
    uint64_t total_requests() const { return total_requests_.load(); }
    uint64_t total_completed() const { return total_completed_.load(); }

private:
    std::vector<std::thread> workers_;
    std::queue<LlmRequest> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    LlmBackend backend_;

    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_completed_{0};

    void worker_loop();
};

} // namespace clove
