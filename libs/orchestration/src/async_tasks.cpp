#include <clove/async_tasks.hpp>
#include <stdexcept>

namespace clove {

AsyncTaskManager::AsyncTaskManager(size_t worker_count) {
    running_.store(true);
    workers_.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back(&AsyncTaskManager::worker_loop, this);
    }
}

AsyncTaskManager::~AsyncTaskManager() {
    shutdown();
}

uint64_t AsyncTaskManager::submit(uint32_t agent_id, SyscallOp opcode, AsyncTaskFn task) {
    uint64_t request_id = next_request_id_.fetch_add(1);

    {
        std::lock_guard lock(queue_mutex_);
        task_queue_.push(Task{
            .agent_id   = agent_id,
            .request_id = request_id,
            .opcode     = opcode,
            .fn         = std::move(task),
        });
    }

    queue_cv_.notify_one();
    return request_id;
}

std::vector<AsyncResult> AsyncTaskManager::poll(uint32_t agent_id, size_t max_results) {
    std::lock_guard lock(results_mutex_);

    std::vector<AsyncResult> result;
    auto it = results_.find(agent_id);
    if (it == results_.end()) {
        return result;
    }

    auto& queue = it->second;
    while (!queue.empty() && result.size() < max_results) {
        result.push_back(std::move(queue.front()));
        queue.pop();
    }
    return result;
}

void AsyncTaskManager::shutdown() {
    {
        std::lock_guard lock(queue_mutex_);
        if (!running_.load()) {
            return;
        }
        running_.store(false);
    }
    queue_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void AsyncTaskManager::worker_loop() {
    while (true) {
        Task task;

        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !task_queue_.empty() || !running_.load();
            });

            if (!running_.load() && task_queue_.empty()) {
                return;
            }

            if (task_queue_.empty()) {
                continue;
            }

            task = std::move(task_queue_.front());
            task_queue_.pop();
        }

        // Execute the task outside the queue lock
        std::string payload;
        try {
            payload = task.fn();
        } catch (const std::exception& e) {
            payload = std::string("{\"error\":\"") + e.what() + "\"}";
        } catch (...) {
            payload = R"({"error":"unknown exception"})";
        }

        // Store the result keyed by agent_id
        {
            std::lock_guard lock(results_mutex_);
            results_[task.agent_id].push(AsyncResult{
                .request_id = task.request_id,
                .opcode     = task.opcode,
                .payload    = std::move(payload),
            });
        }
    }
}

} // namespace clove
