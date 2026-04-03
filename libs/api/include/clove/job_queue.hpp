#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <optional>

namespace clove {

struct RunEvent;  // from run_engine.hpp — avoid EventCallback alias conflict with reactor.hpp

class AgentRunDb;
class SupabaseSync;
class OpenRouterClient;
class AnthropicClient;
class InferenceGateway;
class PrivacyFilter;
class AuditLogger;
class StateStore;
class PermissionsStore;
class ArtifactStore;
class ChainStore;
class MemoryBlockStore;
class McpBridge;
class ContextAssembler;
struct KernelConfig;

/// A single job submitted to the pipeline.
struct Job {
    std::string id;           // UUID
    std::string agent_name;
    std::string workspace_id;
    std::string goal;
    std::string model;
    double      budget_usd = 1.0;
    int         max_steps  = 20;
    int         priority   = 0;   // higher = picked sooner
    std::vector<std::string> allowed_tools;

    std::string depends_on;   // job_id that must complete before this one starts
    std::string submitted_at; // ISO timestamp, set on submit

    // Runtime state (not submitted by caller)
    std::string status;       // queued|running|completed|failed|cancelled
    std::string error;
    nlohmann::json steps_log = nlohmann::json::array();
    int         steps_done  = 0;
    double      cost_usd    = 0.0;
    std::string result;
};

/// Dependency bundle the queue needs to execute jobs.
struct JobQueueDeps {
    OpenRouterClient&  openrouter;
    AnthropicClient*   anthropic;   // nullable — used for claude-* models
    InferenceGateway&  inference;
    PrivacyFilter&     privacy;
    AuditLogger&       audit;
    StateStore&        state;
    PermissionsStore&  permissions;
    ArtifactStore*     artifacts;
    ChainStore*        chains;
    MemoryBlockStore*  memory;
    McpBridge*         mcp;
    ContextAssembler*  assembler;
    const KernelConfig& config;
    AgentRunDb*        run_db;     // nullable
    SupabaseSync*      supabase;   // nullable
};

/// Async job pipeline. Accepts jobs, executes them in a worker pool,
/// checkpoints each step to AgentRunDb + Supabase in real time.
class JobQueue {
public:
    explicit JobQueue(JobQueueDeps deps, int num_workers = 3);
    ~JobQueue();

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    /// Submit a new job. Returns the job_id immediately (non-blocking).
    std::string submit(Job job);

    /// Cancel a queued (not yet running) job. Returns false if already running/done.
    bool cancel(const std::string& job_id);

    /// Get current snapshot of a job (thread-safe copy).
    std::optional<Job> get(const std::string& job_id) const;

    /// List jobs, optionally filtered by status or workspace.
    std::vector<Job> list(const std::string& status = "",
                          const std::string& workspace_id = "",
                          int limit = 50) const;

    /// Subscribe to step events for a specific job.
    /// Callback is called from a worker thread — must be thread-safe.
    using StepCallback = std::function<void(const std::string& job_id, const RunEvent&)>;
    void subscribe(const std::string& job_id, StepCallback cb);
    void unsubscribe(const std::string& job_id);

    size_t queued_count() const;
    size_t running_count() const;

    /// On kernel boot: mark any jobs stuck in "running" as failed.
    void recover_stale_jobs(int stale_minutes = 5);

private:
    JobQueueDeps deps_;
    int          num_workers_;

    // Priority queue: higher priority + earlier submission = first
    struct PendingJob {
        int         priority;
        std::string submitted_at;
        std::string job_id;
        bool operator<(const PendingJob& o) const {
            if (priority != o.priority) return priority < o.priority;
            return submitted_at > o.submitted_at; // earlier = higher priority
        }
    };
    std::priority_queue<PendingJob> pending_;

    mutable std::mutex              jobs_mu_;
    std::condition_variable         cv_;
    std::unordered_map<std::string, Job> jobs_;
    std::unordered_map<std::string, StepCallback> subscribers_;

    std::vector<std::thread> workers_;
    std::atomic<bool>        running_{true};
    std::atomic<int>         active_count_{0};

    void worker_loop();
    void execute_job(Job& job);
    void update_job(const std::string& id,
                    std::function<void(Job&)> mutate,
                    bool sync_to_db = true);
    std::string gen_id() const;
    void db_insert(const Job& job);
    void db_complete(const Job& job);
    void db_step(const Job& job);
};

} // namespace clove
