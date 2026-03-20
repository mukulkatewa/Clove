#include <clove/llm_queue.hpp>
#include <stdexcept>

namespace clove {

LlmQueue::LlmQueue(size_t worker_count) {
    running_.store(true);
    workers_.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&LlmQueue::worker_loop, this);
    }
}

LlmQueue::~LlmQueue() {
    shutdown();
}

void LlmQueue::set_backend(LlmBackend backend) {
    std::lock_guard lock(mutex_);
    backend_ = std::move(backend);
}

std::future<std::string> LlmQueue::submit(uint32_t agent_id, const std::string& prompt) {
    LlmRequest req;
    req.agent_id = agent_id;
    req.payload  = prompt;
    auto future  = req.promise.get_future();

    {
        std::lock_guard lock(mutex_);
        if (!running_.load()) {
            req.promise.set_exception(
                std::make_exception_ptr(std::runtime_error("LlmQueue is shut down")));
            return future;
        }
        queue_.push(std::move(req));
    }

    total_requests_.fetch_add(1);
    cv_.notify_one();
    return future;
}

void LlmQueue::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (!running_.load()) {
            return; // already shut down
        }
        running_.store(false);
    }
    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    // Reject any remaining requests in the queue
    std::lock_guard lock(mutex_);
    while (!queue_.empty()) {
        auto& req = queue_.front();
        req.promise.set_exception(
            std::make_exception_ptr(std::runtime_error("LlmQueue shut down with pending request")));
        queue_.pop();
    }
}

void LlmQueue::worker_loop() {
    while (true) {
        LlmRequest req;

        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || !running_.load();
            });

            if (!running_.load() && queue_.empty()) {
                return;
            }

            if (queue_.empty()) {
                continue;
            }

            req = std::move(queue_.front());
            queue_.pop();
        }

        // Execute the LLM call outside the lock
        try {
            LlmBackend backend_copy;
            {
                std::lock_guard lock(mutex_);
                backend_copy = backend_;
            }

            if (!backend_copy) {
                req.promise.set_exception(
                    std::make_exception_ptr(std::runtime_error("No LLM backend configured")));
            } else {
                std::string result = backend_copy(req.agent_id, req.payload);
                req.promise.set_value(std::move(result));
            }
        } catch (...) {
            try {
                req.promise.set_exception(std::current_exception());
            } catch (...) {
                // Promise may already be fulfilled; ignore
            }
        }

        total_completed_.fetch_add(1);
    }
}

} // namespace clove
