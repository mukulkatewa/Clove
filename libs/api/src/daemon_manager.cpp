#include <clove/daemon_manager.hpp>
#include <clove/state_store.hpp>
#include <clove/audit_log.hpp>
#include <clove/memory_block_store.hpp>

#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <future>
using json = nlohmann::json;

namespace clove {

DaemonManager::DaemonManager(StateStore& state_store, AuditLogger& audit_logger,
                             MemoryBlockStore* memory_blocks)
    : state_store_(state_store), audit_logger_(audit_logger), memory_blocks_(memory_blocks) {}

std::string DaemonManager::now_iso() const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

DaemonConfig DaemonManager::load_daemon_config(const std::string& agent_name) {
    DaemonConfig cfg;
    auto val = state_store_.fetch("agent-def:" + agent_name, 0);
    if (!val) return cfg;

    auto def = *val;
    if (def.contains("daemon")) {
        auto& d = def["daemon"];
        cfg.enabled = d.value("enabled", false);
        cfg.tick_interval_s = d.value("tick_interval_s", 60);
        cfg.blocking_budget_ms = d.value("blocking_budget_ms", 15000);
        cfg.subscriptions = d.value("subscriptions", json::array());
        cfg.dream_enabled = d.value("dream_enabled", true);
        cfg.dream_min_hours = d.value("dream_min_hours", 24);
        cfg.dream_min_runs = d.value("dream_min_runs", 5);
    }
    return cfg;
}

bool DaemonManager::start_daemon(const std::string& agent_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_flags_.count(agent_name) && running_flags_[agent_name].load()) {
        spdlog::warn("Daemon already running: {}", agent_name);
        return false;
    }

    // Verify agent exists
    auto val = state_store_.fetch("agent-def:" + agent_name, 0);
    if (!val) {
        spdlog::warn("Daemon start failed — agent not found: {}", agent_name);
        return false;
    }

    // Init state
    DaemonState state;
    state.agent_name = agent_name;
    state.status = "running";
    state.started_at = std::chrono::steady_clock::now();
    state.last_tick_at = now_iso();
    daemons_[agent_name] = state;

    // Set running flag
    running_flags_[agent_name].store(true);

    // Spawn tick thread
    threads_[agent_name] = std::make_unique<std::thread>([this, agent_name]() {
        tick_loop(agent_name);
    });

    spdlog::info("Daemon started: {}", agent_name);
    audit_logger_.log(AuditCategory::RESOURCE, "DAEMON_STARTED", 0, agent_name, {});

    // Store daemon state
    state_store_.store("daemon:" + agent_name, json({
        {"status", "running"}, {"started_at", now_iso()},
    }), 0);

    append_log(agent_name, "lifecycle", "Daemon started");
    return true;
}

bool DaemonManager::stop_daemon(const std::string& agent_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_flags_.count(agent_name) || !running_flags_[agent_name].load()) {
        return false;
    }

    running_flags_[agent_name].store(false);

    // Wait for thread to finish (non-blocking from mutex perspective)
    if (threads_.count(agent_name) && threads_[agent_name]->joinable()) {
        // Unlock mutex before join to avoid deadlock
        // We'll detach instead and let it clean up
        threads_[agent_name]->detach();
    }
    threads_.erase(agent_name);

    if (daemons_.count(agent_name)) {
        daemons_[agent_name].status = "stopped";
    }

    spdlog::info("Daemon stopped: {}", agent_name);
    audit_logger_.log(AuditCategory::RESOURCE, "DAEMON_STOPPED", 0, agent_name, {});

    state_store_.store("daemon:" + agent_name, json({{"status", "stopped"}}), 0);
    append_log(agent_name, "lifecycle", "Daemon stopped");
    return true;
}

void DaemonManager::stop_all() {
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, flag] : running_flags_) {
            if (flag.load()) names.push_back(name);
        }
    }
    for (auto& name : names) {
        stop_daemon(name);
    }
}

json DaemonManager::list_daemons() const {
    std::lock_guard<std::mutex> lock(mutex_);
    json result = json::array();
    for (auto& [name, state] : daemons_) {
        auto elapsed = std::chrono::steady_clock::now() - state.started_at;
        auto uptime_s = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        result.push_back({
            {"agent_name", state.agent_name},
            {"status", state.status},
            {"uptime_s", uptime_s},
            {"last_tick_at", state.last_tick_at},
            {"ticks_total", state.ticks_total},
            {"actions_today", state.actions_today},
            {"cost_today", state.cost_today},
            {"current_action", state.current_action},
        });
    }
    return result;
}

json DaemonManager::get_state(const std::string& agent_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!daemons_.count(agent_name)) return json({{"error", "not found"}});
    auto& s = daemons_.at(agent_name);
    auto elapsed = std::chrono::steady_clock::now() - s.started_at;
    return {
        {"agent_name", s.agent_name}, {"status", s.status},
        {"uptime_s", std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()},
        {"last_tick_at", s.last_tick_at}, {"ticks_total", s.ticks_total},
        {"actions_today", s.actions_today}, {"cost_today", s.cost_today},
        {"runs_since_consolidation", s.runs_since_consolidation},
        {"current_action", s.current_action},
    };
}

json DaemonManager::get_logs(const std::string& agent_name, int limit) const {
    // Read from state store (daemon-log:<agent>)
    auto val = state_store_.fetch("daemon-log:" + agent_name, 0);
    if (!val) return json::array();
    auto logs = *val;
    if (!logs.is_array()) return json::array();
    // Return last N
    if ((int)logs.size() > limit) {
        return json(std::vector<json>(logs.end() - limit, logs.end()));
    }
    return logs;
}

void DaemonManager::append_log(const std::string& agent_name, const std::string& type,
                                const std::string& detail, double cost) {
    auto entry = json({
        {"ts", now_iso()}, {"type", type}, {"detail", detail}, {"cost", cost}
    });

    // Append to state store
    auto val = state_store_.fetch("daemon-log:" + agent_name, 0);
    json logs = (val && val->is_array()) ? *val : json::array();
    logs.push_back(entry);

    // Cap at 500 entries
    if (logs.size() > 500) {
        logs = json(std::vector<json>(logs.end() - 500, logs.end()));
    }
    state_store_.store("daemon-log:" + agent_name, logs, 0);
}

bool DaemonManager::is_running(const std::string& agent_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_flags_.count(agent_name) && running_flags_.at(agent_name).load();
}

// ── Tick Loop ──────────────────────────────────────────────────
void DaemonManager::tick_loop(const std::string& agent_name) {
    spdlog::info("Daemon tick loop started: {}", agent_name);

    while (running_flags_[agent_name].load()) {
        auto cfg = load_daemon_config(agent_name);
        auto tick_start = std::chrono::steady_clock::now();

        // Update state
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (daemons_.count(agent_name)) {
                daemons_[agent_name].status = "sleeping";
                daemons_[agent_name].last_tick_at = now_iso();
                daemons_[agent_name].ticks_total++;
            }
        }

        // 1. Check subscriptions for events
        auto events = check_subscriptions(agent_name, cfg);

        if (!events.empty() && run_fn_) {
            // 2. Act
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (daemons_.count(agent_name)) {
                    daemons_[agent_name].status = "acting";
                    daemons_[agent_name].current_action = events.dump().substr(0, 100);
                }
            }

            // Build goal from events
            std::string goal = "You are a daemon agent in tick mode. These events occurred since your last tick:\n";
            for (auto& ev : events) {
                goal += "- " + ev.value("type", "") + ": " + ev.value("detail", "") + "\n";
            }
            goal += "\nDecide if any action is needed and act within your scope.";

            // Load agent budget
            auto agent_val = state_store_.fetch("agent-def:" + agent_name, 0);
            double budget = 0.10; // default tick budget
            if (agent_val && agent_val->contains("budget")) {
                budget = agent_val->at("budget").value("per_run", 0.10);
            }

            try {
                // Run with timeout (blocking budget)
                auto future = std::async(std::launch::async, [&]() {
                    return run_fn_(agent_name, goal, budget);
                });

                auto timeout = std::chrono::milliseconds(cfg.blocking_budget_ms);
                if (future.wait_for(timeout) == std::future_status::ready) {
                    auto result = future.get();
                    double cost = result.value("total_cost_usd", 0.0);

                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (daemons_.count(agent_name)) {
                            daemons_[agent_name].actions_today++;
                            daemons_[agent_name].cost_today += cost;
                            daemons_[agent_name].runs_since_consolidation++;
                            daemons_[agent_name].current_action = "";
                        }
                    }

                    append_log(agent_name, "action",
                        result.value("content", "").substr(0, 200), cost);

                    audit_logger_.log(AuditCategory::RESOURCE, "DAEMON_TICK_ACTION",
                        0, agent_name, {{"cost", cost}, {"events", events.size()}});
                } else {
                    // Timed out — action took too long
                    append_log(agent_name, "timeout", "Action exceeded blocking budget, deferred");
                    spdlog::warn("Daemon {} tick action timed out ({}ms budget)", agent_name, cfg.blocking_budget_ms);
                }
            } catch (const std::exception& e) {
                append_log(agent_name, "error", e.what());
                spdlog::warn("Daemon {} tick error: {}", agent_name, e.what());
            }
        }

        // 3. Check if dream is needed
        maybe_dream(agent_name);

        // 4. Sleep until next tick
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (daemons_.count(agent_name)) {
                daemons_[agent_name].status = "sleeping";
                daemons_[agent_name].current_action = "";
            }
        }

        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tick_start).count();
        auto sleep_ms = cfg.tick_interval_s * 1000 - elapsed_ms;
        if (sleep_ms > 0) {
            // Sleep in 1s increments so we can exit quickly
            for (int i = 0; i < sleep_ms / 1000 && running_flags_[agent_name].load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    spdlog::info("Daemon tick loop ended: {}", agent_name);
}

// ── Subscriptions ──────────────────────────────────────────────
json DaemonManager::check_subscriptions(const std::string& agent_name, const DaemonConfig& config) {
    json events = json::array();

    for (auto& sub : config.subscriptions) {
        std::string type = sub.value("type", "");

        if (type == "schedule.tick") {
            // Always fires — the agent gets a heartbeat every tick
            events.push_back({{"type", "heartbeat"}, {"detail", "periodic tick"}});
        }
        else if (type == "github.events") {
            // Check for new GitHub events via MCP (if connected)
            // For now, check state store for queued webhook events
            auto webhook_key = "webhook-queue:" + agent_name;
            auto queued = state_store_.fetch(webhook_key, 0);
            if (queued && queued->is_array() && !queued->empty()) {
                for (auto& ev : *queued) {
                    events.push_back({{"type", "github"}, {"detail", ev.dump().substr(0, 200)}});
                }
                // Clear queue
                state_store_.store(webhook_key, json::array(), 0);
            }
        }
        else if (type == "agent.output") {
            // Check if a specific agent has completed a run since last tick
            std::string watch_agent = sub.value("agent", "");
            if (!watch_agent.empty()) {
                auto key = "agent-last-output:" + watch_agent;
                auto output = state_store_.fetch(key, 0);
                if (output) {
                    events.push_back({{"type", "agent_output"}, {"agent", watch_agent}, {"detail", output->dump().substr(0, 200)}});
                    state_store_.erase(key, 0);
                }
            }
        }
        else if (type == "queue.depth") {
            // Check internal task queue depth
            int threshold = sub.value("threshold", 10);
            // Simple: count pending items in state store
            auto keys = state_store_.keys("task-queue:", 0);
            if ((int)keys.size() > threshold) {
                events.push_back({{"type", "queue_depth"}, {"detail", "queue depth " + std::to_string(keys.size()) + " > " + std::to_string(threshold)}});
            }
        }
    }

    return events;
}

// ── Dream Engine ───────────────────────────────────────────────
void DaemonManager::maybe_dream(const std::string& agent_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!daemons_.count(agent_name)) return;
    auto& state = daemons_[agent_name];

    auto cfg = load_daemon_config(agent_name);
    if (!cfg.dream_enabled) return;

    // Check conditions
    if (state.runs_since_consolidation < cfg.dream_min_runs) return;

    // Check time since last consolidation
    if (!state.last_consolidated.empty()) {
        // Simple: just check runs threshold for now
        // Full time-based check would parse ISO timestamps
    }

    // Trigger dream
    state.status = "dreaming";

    // Run consolidation in-place (simple version)
    if (memory_blocks_) {
        append_log(agent_name, "dream", "Memory consolidation started");

        // Phase 1: Orient — count memories
        // Phase 2-4: Use LLM to consolidate (via run_fn_)
        if (run_fn_) {
            try {
                auto result = run_fn_(agent_name,
                    "You are consolidating your memory. Review your recent memories, "
                    "resolve any contradictions, remove stale entries, and update your "
                    "index. Be concise — this is maintenance, not a task.",
                    0.05 // dream budget
                );
                double cost = result.value("total_cost_usd", 0.0);
                state.cost_today += cost;
                append_log(agent_name, "dream", "Consolidation complete", cost);
            } catch (const std::exception& e) {
                append_log(agent_name, "dream_error", e.what());
            }
        }

        state.runs_since_consolidation = 0;
        state.last_consolidated = now_iso();
    }

    state.status = "sleeping";
}

bool DaemonManager::dream(const std::string& agent_name) {
    if (!is_running(agent_name)) return false;
    maybe_dream(agent_name);
    return true;
}

} // namespace clove
