#include <clove/job_queue.hpp>
#include <clove/run_engine.hpp>
#include <clove/workspace_db.hpp>
#include <clove/supabase_sync.hpp>
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace clove {

// ── Helpers ────────────────────────────────────────────────────────────────

static std::string gen_uuid() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8)  << (dist(rng) & 0xFFFFFFFF)       << '-'
        << std::setw(4)  << (dist(rng) & 0xFFFF)           << '-'
        << std::setw(4)  << ((dist(rng) & 0x0FFF) | 0x4000)<< '-'
        << std::setw(4)  << ((dist(rng) & 0x3FFF) | 0x8000)<< '-'
        << std::setw(12) << (dist(rng) & 0xFFFFFFFFFFFF);
    return oss.str();
}

static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()).count() % 1000;
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms << 'Z';
    return oss.str();
}

// ── JobQueue ───────────────────────────────────────────────────────────────

JobQueue::JobQueue(JobQueueDeps deps, int num_workers)
    : deps_(std::move(deps)), num_workers_(num_workers) {
    workers_.reserve(num_workers_);
    for (int i = 0; i < num_workers_; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
    spdlog::info("JobQueue: {} workers started", num_workers_);
}

JobQueue::~JobQueue() {
    running_ = false;
    cv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
}

std::string JobQueue::submit(Job job) {
    if (job.id.empty()) job.id = gen_uuid();
    job.status = "queued";
    if (job.submitted_at.empty()) job.submitted_at = now_iso();

    {
        std::lock_guard lock(jobs_mu_);
        jobs_[job.id] = job;
        pending_.push({job.priority, job.submitted_at, job.id});
    }

    db_insert(job);
    cv_.notify_one();

    spdlog::info("JobQueue: submitted job {} ({})", job.id.substr(0,8), job.goal.substr(0, 60));
    return job.id;
}

bool JobQueue::cancel(const std::string& job_id) {
    std::lock_guard lock(jobs_mu_);
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) return false;
    if (it->second.status != "queued") return false;
    it->second.status = "cancelled";
    it->second.error  = "cancelled by user";
    return true;
}

std::optional<Job> JobQueue::get(const std::string& job_id) const {
    std::lock_guard lock(jobs_mu_);
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) return std::nullopt;
    return it->second;
}

std::vector<Job> JobQueue::list(const std::string& status,
                                 const std::string& workspace_id,
                                 int limit) const {
    std::lock_guard lock(jobs_mu_);
    std::vector<Job> result;
    for (const auto& [id, job] : jobs_) {
        if (!status.empty()       && job.status       != status)       continue;
        if (!workspace_id.empty() && job.workspace_id != workspace_id) continue;
        result.push_back(job);
        if ((int)result.size() >= limit) break;
    }
    // Sort by submission order (newest first) — approximated by id length uniqueness
    return result;
}

void JobQueue::subscribe(const std::string& job_id, StepCallback cb) {
    std::lock_guard lock(jobs_mu_);
    subscribers_[job_id] = std::move(cb);
}

void JobQueue::unsubscribe(const std::string& job_id) {
    std::lock_guard lock(jobs_mu_);
    subscribers_.erase(job_id);
}

size_t JobQueue::queued_count() const {
    std::lock_guard lock(jobs_mu_);
    size_t n = 0;
    for (const auto& [_, j] : jobs_) if (j.status == "queued") ++n;
    return n;
}

size_t JobQueue::running_count() const {
    return static_cast<size_t>(active_count_.load());
}

void JobQueue::recover_stale_jobs(int stale_minutes) {
    if (!deps_.run_db) return;
    auto stale_before = std::chrono::system_clock::now()
                      - std::chrono::minutes(stale_minutes);
    auto runs = deps_.run_db->list("", 500);
    int recovered = 0;
    for (const auto& r : runs) {
        if (r.status != "running" && r.status != "queued") continue;
        // Check if the started_at is older than threshold
        // Simple string comparison works for ISO8601 in UTC
        std::string threshold;
        {
            auto t = std::chrono::system_clock::to_time_t(stale_before);
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
            threshold = oss.str();
        }
        if (r.started_at < threshold) {
            deps_.run_db->complete(r.id, "failed", "", r.steps, r.cost_usd);
            if (deps_.supabase) deps_.supabase->patch("agent_runs", "id", r.id, {
                {"status", "failed"}, {"error", "kernel_restart"}
            });
            ++recovered;
        }
    }
    if (recovered > 0)
        spdlog::warn("JobQueue: recovered {} stale jobs (marked failed)", recovered);
}

// ── Worker ─────────────────────────────────────────────────────────────────

void JobQueue::worker_loop() {
    while (running_) {
        std::string job_id;
        int         job_priority = 0;
        std::string job_submitted_at;

        {
            std::unique_lock lock(jobs_mu_);
            cv_.wait(lock, [this] { return !pending_.empty() || !running_; });
            if (!running_) break;
            while (!pending_.empty()) {
                auto top = pending_.top();
                pending_.pop();
                auto it = jobs_.find(top.job_id);
                if (it == jobs_.end()) continue;
                if (it->second.status == "cancelled") continue;
                job_id           = top.job_id;
                job_priority     = top.priority;
                job_submitted_at = top.submitted_at;
                break;
            }
        }
        if (job_id.empty()) continue;

        // ── Dependency check ──────────────────────────────────────────────
        bool dep_unmet  = false;
        bool dep_failed = false;
        Job  failed_job_copy;

        {
            std::lock_guard lock(jobs_mu_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) continue;

            const std::string& dep = it->second.depends_on;
            if (!dep.empty()) {
                auto dep_it = jobs_.find(dep);
                if (dep_it != jobs_.end()) {
                    const auto& ds = dep_it->second.status;
                    if (ds == "queued" || ds == "running") {
                        dep_unmet = true;
                    } else if (ds == "failed" || ds == "cancelled") {
                        dep_failed = true;
                        it->second.status = "failed";
                        it->second.error  = "dependency " + dep.substr(0,8) + " did not complete";
                        failed_job_copy   = it->second;
                    }
                    // ds == "completed" → fall through and execute normally
                }
            }
        }

        if (dep_failed) {
            db_complete(failed_job_copy);
            continue;
        }

        if (dep_unmet) {
            // Re-queue with same priority; sleep briefly to avoid spin
            std::this_thread::sleep_for(std::chrono::seconds(3));
            {
                std::lock_guard lock(jobs_mu_);
                auto it = jobs_.find(job_id);
                if (it != jobs_.end() && it->second.status == "queued") {
                    pending_.push({job_priority, job_submitted_at, job_id});
                    cv_.notify_one();
                }
            }
            continue;
        }

        // ── Execute ───────────────────────────────────────────────────────
        Job job;
        {
            std::lock_guard lock(jobs_mu_);
            auto it = jobs_.find(job_id);
            if (it == jobs_.end()) continue;
            it->second.status = "running";
            job = it->second;
        }

        if (deps_.supabase) deps_.supabase->patch("agent_runs", "id", job_id, {{"status", "running"}});

        ++active_count_;
        spdlog::info("JobQueue: starting job {} — {}", job_id.substr(0,8), job.goal.substr(0,60));
        try {
            execute_job(job);
        } catch (const std::exception& ex) {
            job.status = "failed";
            job.error  = std::string("worker crash: ") + ex.what();
            spdlog::error("JobQueue: worker caught unhandled exception for job {}: {}", job_id.substr(0,8), ex.what());
        } catch (...) {
            job.status = "failed";
            job.error  = "worker crash: unknown exception";
            spdlog::error("JobQueue: worker caught unknown exception for job {}", job_id.substr(0,8));
        }
        --active_count_;

        {
            std::lock_guard lock(jobs_mu_);
            if (auto it = jobs_.find(job_id); it != jobs_.end()) it->second = job;
        }
        db_complete(job);

        // Notify waiting dependents
        cv_.notify_all();
    }
}

void JobQueue::execute_job(Job& job) {
    RunConfig cfg;
    cfg.goal          = job.goal;
    cfg.model         = job.model;
    cfg.budget_usd    = job.budget_usd;
    cfg.max_steps     = job.max_steps;
    cfg.allowed_tools = job.allowed_tools;
    cfg.agent_name    = job.agent_name.empty() ? "agent" : job.agent_name;
    cfg.workspace_id      = job.workspace_id;
    cfg.run_id            = job.id;
    cfg.compress_context  = job.compress_context;
    cfg.use_memory        = job.use_memory;

    RunEngine engine(
        deps_.openrouter, deps_.anthropic, deps_.inference, deps_.privacy,
        deps_.audit, deps_.state, deps_.permissions,
        deps_.artifacts, deps_.chains, deps_.memory, deps_.memory_manager,
        deps_.mcp, deps_.assembler, deps_.config);

    RunResult result;
    try {
    result = engine.execute(cfg, [&](const RunEvent& ev) {
        // Per-step callback — checkpoint to DB + Supabase + notify subscribers
        nlohmann::json step_entry = ev.data;
        step_entry["type"] = ev.type;

        {
            std::lock_guard lock(jobs_mu_);
            auto it = jobs_.find(job.id);
            if (it != jobs_.end()) {
                it->second.steps_log.push_back(step_entry);
                it->second.steps_done = (int)it->second.steps_log.size();
                job.steps_log = it->second.steps_log;
                job.steps_done = it->second.steps_done;

                // Fire subscriber if any
                auto sub_it = subscribers_.find(job.id);
                if (sub_it != subscribers_.end()) {
                    sub_it->second(job.id, ev);
                }
            }
        }

        // Async push step to Supabase (non-blocking)
        if (deps_.supabase && (ev.type == "tool_call" || ev.type == "done" || ev.type == "error")) {
            deps_.supabase->patch("agent_runs", "id", job.id, {
                {"steps", job.steps_done},
                {"status", "running"}
            });
        }
    });
    } catch (const std::exception& ex) {
        result.success = false;
        result.error = std::string("internal error: ") + ex.what();
        spdlog::error("JobQueue: job {} threw exception: {}", job.id.substr(0,8), ex.what());
    } catch (...) {
        result.success = false;
        result.error = "internal error: unknown exception";
        spdlog::error("JobQueue: job {} threw unknown exception", job.id.substr(0,8));
    }

    job.status   = result.success ? "completed" : "failed";
    job.result   = result.content;
    job.error    = result.error;
    job.steps_done = result.steps;
    job.tokens     = result.total_tokens;
    job.cost_usd   = result.total_cost_usd;
}

// ── DB helpers ─────────────────────────────────────────────────────────────

void JobQueue::db_insert(const Job& job) {
    if (!deps_.run_db) return;
    AgentRunRow row;
    row.id           = job.id;
    row.agent_name   = job.agent_name;
    row.workspace_id = job.workspace_id;
    row.goal         = job.goal;
    row.status       = "queued";
    row.model        = job.model;
    deps_.run_db->insert(row);

    if (deps_.supabase) deps_.supabase->upsert("agent_runs", {
        {"id", job.id}, {"agent_name", job.agent_name},
        {"workspace_id", job.workspace_id}, {"goal", job.goal},
        {"status", "queued"}, {"model", job.model},
        {"started_at", job.submitted_at}
    });
}

void JobQueue::db_complete(const Job& job) {
    if (!deps_.run_db) return;
    deps_.run_db->complete(job.id, job.status, job.result, job.steps_done, job.cost_usd);

    if (deps_.supabase) deps_.supabase->patch("agent_runs", "id", job.id, {
        {"status", job.status}, {"result", job.result},
        {"steps", job.steps_done}, {"cost_usd", job.cost_usd},
        {"completed_at", now_iso()}
    });
}

} // namespace clove
