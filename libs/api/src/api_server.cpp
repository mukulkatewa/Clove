#include <clove/api_server.hpp>

#include <clove/agent_manager.hpp>
#include <clove/agent_process.hpp>
#include <clove/state_store.hpp>
#include <clove/event_bus.hpp>
#include <clove/permissions_store.hpp>
#include <clove/permissions.hpp>
#include <clove/inference_gateway.hpp>
#include <clove/privacy_filter.hpp>
#include <clove/audit_log.hpp>
#include <clove/execution_log.hpp>
#include <clove/policy_recommender.hpp>
#include <clove/mcp_bridge.hpp>
#include <clove/a2a_bridge.hpp>
#include <clove/tunnel_bridge.hpp>
#include <clove/world_engine.hpp>
#include <clove/openrouter.hpp>
#include <clove/llm_queue.hpp>
#include <clove/artifact_store.hpp>
#include <clove/chain_store.hpp>
#include <clove/context_assembler.hpp>
#include <clove/memory_block_store.hpp>
#include <clove/run_engine.hpp>
#include <clove/openclaw_manager.hpp>
#include <clove/mailbox.hpp>

#include "dashboard_html.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <sstream>
#include <iomanip>
#include <sys/wait.h>
#include <algorithm>
#include <future>
#include <atomic>
#include <unordered_map>

using json = nlohmann::json;

namespace clove {

// ---------------------------------------------------------------------------
// Impl (pimpl) — holds the httplib::Server
// ---------------------------------------------------------------------------
class ApiServer::Impl {
public:
    httplib::Server svr;

    bool listen(uint16_t port) {
        return svr.listen("0.0.0.0", port);
    }

    void stop() {
        svr.stop();
    }
};

// ---------------------------------------------------------------------------
// ApiServer lifecycle
// ---------------------------------------------------------------------------
ApiServer::ApiServer(ApiContext ctx)
    : ctx_(ctx)
    , impl_(std::make_unique<Impl>())
{
}

ApiServer::~ApiServer() {
    stop();
}

bool ApiServer::start(uint16_t port, const std::string& api_key) {
    if (running_) return false;

    api_key_ = api_key;
    start_time_ = std::chrono::steady_clock::now();

    setup_routes();

    server_thread_ = std::make_unique<std::thread>([this, port]() {
        running_ = true;
        spdlog::info("API server thread starting on port {}", port);
        if (!impl_->listen(port)) {
            spdlog::error("API server failed to bind port {}", port);
        }
        running_ = false;
    });

    // Give the server a moment to start up
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Start scheduler thread (checks cron every 60s)
    scheduler_running_ = true;
    scheduler_thread_ = std::make_unique<std::thread>([this]() { run_scheduler(); });

    return running_;
}

void ApiServer::stop() {
    if (!running_) return;
    scheduler_running_ = false;
    if (scheduler_thread_ && scheduler_thread_->joinable()) {
        scheduler_thread_->join();
    }
    impl_->stop();
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    running_ = false;
}

// ── Cron scheduler — checks every 60s, fires matching schedules ──

static bool cron_matches_now(const std::string& cron_expr) {
    // Simple cron parser: "min hour dom month dow"
    // Supports: numbers, * (any), ranges not supported (keep it simple)
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto* tm = std::localtime(&time_t);

    std::istringstream ss(cron_expr);
    std::string min_s, hour_s, dom_s, mon_s, dow_s;
    ss >> min_s >> hour_s >> dom_s >> mon_s >> dow_s;

    auto matches = [](const std::string& field, int value) -> bool {
        if (field == "*") return true;
        try { return std::stoi(field) == value; } catch (...) { return false; }
    };

    return matches(min_s, tm->tm_min) &&
           matches(hour_s, tm->tm_hour) &&
           matches(dom_s, tm->tm_mday) &&
           matches(mon_s, tm->tm_mon + 1) &&
           matches(dow_s, tm->tm_wday);
}

void ApiServer::run_scheduler() {
    spdlog::info("Scheduler thread started (60s interval)");
    while (scheduler_running_) {
        // Sleep 60s in 1s increments so we can exit quickly
        for (int i = 0; i < 60 && scheduler_running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!scheduler_running_) break;

        // Check all schedules
        auto all_keys = ctx_.state_store.keys("schedule:", 0);
        for (const auto& key : all_keys) {
            auto val = ctx_.state_store.fetch(key, 0);
            if (!val) continue;

            try {
                auto schedule = *val;
                if (!schedule.value("enabled", true)) continue;

                std::string cron = schedule.value("cron", "");
                if (cron.empty() || !cron_matches_now(cron)) continue;

                // Fire the run
                auto run_cfg = schedule.value("run", json::object());
                std::string goal = run_cfg.value("goal", "");
                double budget = run_cfg.value("budget", 1.0);
                int agents = run_cfg.value("agents", 1);

                if (goal.empty()) continue;

                spdlog::info("Scheduler firing: {} ({})", schedule.value("name", "?"), cron);

                ctx_.audit_logger.log(AuditCategory::RESOURCE, "SCHEDULE_FIRED",
                    0, "", {{"name", schedule.value("name", "")}, {"cron", cron}});

                // Run as fleet or single
                if (agents > 1 && ctx_.openrouter && ctx_.openrouter->is_configured()) {
                    // Use fleet — but we'd need to call the fleet handler
                    // For now, run as single agent
                    RunEngine engine(*ctx_.openrouter, ctx_.inference_gateway, ctx_.privacy_filter,
                        ctx_.audit_logger, ctx_.state_store, ctx_.permissions_store,
                        ctx_.artifact_store, ctx_.chain_store,
                        ctx_.memory_blocks, ctx_.mcp_bridge, ctx_.assembler, ctx_.config);
                    RunConfig cfg;
                    cfg.goal = goal;
                    cfg.budget_usd = budget;
                    cfg.agent_name = "scheduled-" + schedule.value("name", "agent");
                    auto result = engine.execute(cfg);

                    fire_webhooks("run_complete", {
                        {"schedule", schedule.value("name", "")},
                        {"success", result.success},
                        {"cost_usd", result.total_cost_usd},
                        {"content_preview", result.content.substr(0, 200)},
                    });
                }
            } catch (const std::exception& e) {
                spdlog::warn("Scheduler error for {}: {}", key, e.what());
            }
        }
    }
    spdlog::info("Scheduler thread stopped");
}

// ── Webhook dispatcher ──

void ApiServer::fire_webhooks(const std::string& event_type, const json& data) {
    auto all_keys = ctx_.state_store.keys("webhook:", 0);
    for (const auto& key : all_keys) {
        auto val = ctx_.state_store.fetch(key, 0);
        if (!val) continue;

        try {
            auto webhook = *val;
            if (!webhook.value("enabled", true)) continue;

            auto events = webhook.value("events", json::array());
            bool matched = false;
            for (const auto& ev : events) {
                if (ev.get<std::string>() == event_type) { matched = true; break; }
            }
            if (!matched) continue;

            std::string url = webhook.value("url", "");
            if (url.empty()) continue;

            // Fire webhook via CURL (non-blocking — fire and forget)
            json payload;
            payload["event"] = event_type;
            payload["data"] = data;
            payload["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            std::string body = payload.dump();
            std::string cmd = "curl -s -X POST '" + url + "' -H 'Content-Type: application/json' -d '" + body + "' > /dev/null 2>&1 &";
            std::system(cmd.c_str());

            ctx_.audit_logger.log(AuditCategory::RESOURCE, "WEBHOOK_FIRED",
                0, "", {{"url", url}, {"event", event_type}});

        } catch (...) {}
    }
}

// ---------------------------------------------------------------------------
// Route setup
// ---------------------------------------------------------------------------
void ApiServer::setup_routes() {
    auto& svr = impl_->svr;

    // -----------------------------------------------------------------------
    // Auth middleware
    // -----------------------------------------------------------------------
    svr.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse {
        if (api_key_.empty()) return httplib::Server::HandlerResponse::Unhandled;
        if (req.path == "/api/health") return httplib::Server::HandlerResponse::Unhandled;
        // Dashboard pages and fragment endpoints — no auth required
        if (req.path == "/" || req.path.rfind("/dashboard", 0) == 0 ||
            req.path.rfind("/api/fragments/", 0) == 0) {
            return httplib::Server::HandlerResponse::Unhandled;
        }

        auto auth = req.get_header_value("Authorization");
        if (auth != "Bearer " + api_key_) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // -----------------------------------------------------------------------
    // Health
    // -----------------------------------------------------------------------
    svr.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();

        json j;
        j["status"] = "ok";
        j["version"] = "2.0.0";
        j["uptime_s"] = uptime;
        j["syscall_count"] = 86;
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Agents — list
    // -----------------------------------------------------------------------
    svr.Get("/api/agents", [this](const httplib::Request&, httplib::Response& res) {
        auto agents = ctx_.agent_manager.list_agents();
        json j = json::array();
        for (const auto& agent : agents) {
            auto metrics = agent->get_metrics();
            json aj;
            aj["id"] = agent->id();
            aj["name"] = agent->name();
            aj["state"] = agent_state_to_string(agent->state());
            aj["pid"] = agent->pid();
            aj["uptime_s"] = metrics.uptime_seconds;
            j.push_back(aj);
        }
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Agents — spawn
    // -----------------------------------------------------------------------
    svr.Post("/api/agents", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            AgentConfig ac;
            ac.name = body.value("name", "");
            ac.script_path = body.value("command", "");
            ac.socket_path = ctx_.config.socket_path;

            if (ac.name.empty() || ac.script_path.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name and command are required"})", "application/json");
                return;
            }

            auto agent = ctx_.agent_manager.spawn_agent(ac);
            if (agent) {
                json j;
                j["id"] = agent->id();
                j["name"] = agent->name();
                j["state"] = agent_state_to_string(agent->state());
                j["pid"] = agent->pid();
                res.status = 201;
                res.set_content(j.dump(), "application/json");
            } else {
                res.status = 500;
                res.set_content(R"({"error":"failed to spawn agent"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Agents — kill
    // -----------------------------------------------------------------------
    svr.Delete(R"(/api/agents/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        if (ctx_.agent_manager.kill_agent(id)) {
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"agent not found"})", "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Agent Definitions (persistent registry via state store)
    // GET  /api/agent-defs      — list all definitions
    // POST /api/agent-defs      — create definition
    // GET  /api/agent-defs/:name — get one
    // PUT  /api/agent-defs/:name — update
    // DELETE /api/agent-defs/:name — delete
    // POST /api/agent-defs/:name/run — manually trigger
    // -----------------------------------------------------------------------
    svr.Get("/api/agent-defs", [this](const httplib::Request&, httplib::Response& res) {
        auto keys = ctx_.state_store.keys("agent-def:", 0);
        json agents = json::array();
        for (const auto& key : keys) {
            auto val = ctx_.state_store.fetch(key, 0);
            if (val.has_value()) {
                try { agents.push_back(json::parse(val.value().dump())); } catch (...) {}
            }
        }
        json resp;
        resp["agents"] = agents;
        resp["count"] = agents.size();
        res.set_content(resp.dump(), "application/json");
    });

    svr.Post("/api/agent-defs", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string name = body.value("name", "");
            if (name.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name is required"})", "application/json");
                return;
            }
            body["created_at"] = body.value("created_at", "");
            body["updated_at"] = "";
            ctx_.state_store.store("agent-def:" + name, body, 0);
            res.status = 201;
            res.set_content(body.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    svr.Get(R"(/api/agent-defs/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        auto val = ctx_.state_store.fetch("agent-def:" + name, 0);
        if (val.has_value()) {
            res.set_content(val.value().dump(), "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"agent definition not found"})", "application/json");
        }
    });

    svr.Put(R"(/api/agent-defs/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string name = req.matches[1];
            auto body = json::parse(req.body);
            body["name"] = name;
            ctx_.state_store.store("agent-def:" + name, body, 0);
            res.set_content(body.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    svr.Delete(R"(/api/agent-defs/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        ctx_.state_store.erase("agent-def:" + name, 0);
        res.set_content(R"({"success":true})", "application/json");
    });

    // Manually trigger a defined agent
    svr.Post(R"(/api/agent-defs/([^/]+)/run)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        auto val = ctx_.state_store.fetch("agent-def:" + name, 0);
        if (!val.has_value()) {
            res.status = 404;
            res.set_content(R"({"error":"agent definition not found"})", "application/json");
            return;
        }
        auto def = val.value();
        auto action = def.value("action", json::object());
        std::string goal = action.value("goal", "");
        double budget = 0.5;
        auto budget_obj = def.value("budget", json::object());
        budget = budget_obj.value("per_run", 0.5);
        int max_steps = action.value("max_steps", 10);
        auto tools = action.value("tools", std::vector<std::string>{});

        if (goal.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"agent has no goal defined"})", "application/json");
            return;
        }

        RunConfig cfg;
        cfg.goal = goal;
        cfg.budget_usd = budget;
        cfg.max_steps = max_steps;
        cfg.allowed_tools = tools;
        cfg.agent_name = name;
        cfg.model = action.value("model", ctx_.config.llm_model);

        RunEngine engine(*ctx_.openrouter, ctx_.inference_gateway, ctx_.privacy_filter,
                         ctx_.audit_logger, ctx_.state_store, ctx_.permissions_store,
                         ctx_.artifact_store, ctx_.chain_store, ctx_.memory_blocks,
                         ctx_.mcp_bridge, ctx_.assembler, ctx_.config);

        auto result = engine.execute(cfg);
        json j;
        j["success"] = result.success;
        j["content"] = result.content;
        j["total_cost_usd"] = result.total_cost_usd;
        j["total_tokens"] = result.total_tokens;
        j["steps"] = result.steps;
        j["chain_id"] = result.chain_id;
        j["agent_name"] = name;
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Agent metrics
    // -----------------------------------------------------------------------
    svr.Get(R"(/api/agents/(\d+)/metrics)", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        auto agent = ctx_.agent_manager.get_agent(id);
        if (!agent) {
            res.status = 404;
            res.set_content(R"({"error":"agent not found"})", "application/json");
            return;
        }
        auto m = agent->get_metrics();
        json j;
        j["id"] = m.id;
        j["name"] = m.name;
        j["pid"] = m.pid;
        j["state"] = agent_state_to_string(m.state);
        j["memory_bytes"] = m.memory_bytes;
        j["cpu_percent"] = m.cpu_percent;
        j["uptime_seconds"] = m.uptime_seconds;
        j["llm_request_count"] = m.llm_request_count;
        j["llm_tokens_used"] = m.llm_tokens_used;
        j["parent_id"] = m.parent_id;
        j["child_ids"] = m.child_ids;
        j["created_at_ms"] = m.created_at_ms;
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Agent permissions — get
    // -----------------------------------------------------------------------
    svr.Get(R"(/api/agents/(\d+)/permissions)", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        if (!ctx_.permissions_store.exists(id)) {
            res.status = 404;
            res.set_content(R"({"error":"agent not found"})", "application/json");
            return;
        }
        auto perms = ctx_.permissions_store.get_or_create(id);
        res.set_content(perms.to_json().dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Agent permissions — update (delta merge)
    // -----------------------------------------------------------------------
    svr.Put(R"(/api/agents/(\d+)/permissions)", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        try {
            auto delta = json::parse(req.body);
            auto perms = ctx_.permissions_store.get_or_create(id);

            if (delta.contains("can_exec"))  perms.can_exec  = delta["can_exec"].get<bool>();
            if (delta.contains("can_read"))  perms.can_read  = delta["can_read"].get<bool>();
            if (delta.contains("can_write")) perms.can_write = delta["can_write"].get<bool>();
            if (delta.contains("can_think")) perms.can_think = delta["can_think"].get<bool>();
            if (delta.contains("can_spawn")) perms.can_spawn = delta["can_spawn"].get<bool>();
            if (delta.contains("can_http"))  perms.can_http  = delta["can_http"].get<bool>();
            if (delta.contains("allowed_domains")) {
                perms.allowed_domains.clear();
                for (const auto& d : delta["allowed_domains"])
                    perms.allowed_domains.push_back(d.get<std::string>());
            }

            ctx_.permissions_store.set_permissions(id, perms);
            res.set_content(perms.to_json().dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Audit — query
    // -----------------------------------------------------------------------
    svr.Get("/api/audit", [this](const httplib::Request& req, httplib::Response& res) {
        // Parse query params
        AuditCategory* cat_ptr = nullptr;
        AuditCategory cat_val{};
        if (req.has_param("category")) {
            cat_val = audit_category_from_string(req.get_param_value("category"));
            cat_ptr = &cat_val;
        }

        uint32_t* agent_ptr = nullptr;
        uint32_t agent_val = 0;
        if (req.has_param("agent_id")) {
            agent_val = static_cast<uint32_t>(std::stoul(req.get_param_value("agent_id")));
            agent_ptr = &agent_val;
        }

        uint64_t since_id = 0;
        if (req.has_param("since_id")) {
            since_id = std::stoull(req.get_param_value("since_id"));
        }

        size_t limit = 100;
        if (req.has_param("limit")) {
            limit = std::stoull(req.get_param_value("limit"));
        }

        auto entries = ctx_.audit_logger.get_entries(cat_ptr, agent_ptr, since_id, limit);
        json j = json::array();
        for (const auto& entry : entries) {
            j.push_back(entry.to_json());
        }
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Audit — JSONL export
    // -----------------------------------------------------------------------
    svr.Get("/api/audit/export", [this](const httplib::Request& req, httplib::Response& res) {
        size_t limit = 0;
        if (req.has_param("limit")) {
            limit = std::stoull(req.get_param_value("limit"));
        }
        auto jsonl = ctx_.audit_logger.export_jsonl(limit);
        res.set_content(jsonl, "application/x-ndjson");
    });

    // -----------------------------------------------------------------------
    // Replay — status
    // -----------------------------------------------------------------------
    svr.Get("/api/replay", [this](const httplib::Request&, httplib::Response& res) {
        json j;
        auto state = ctx_.execution_logger.recording_state();
        j["recording"] = (state == RecordingState::RECORDING);
        j["state"] = (state == RecordingState::RECORDING) ? "recording" :
                     (state == RecordingState::PAUSED)    ? "paused" : "idle";
        j["entry_count"] = ctx_.execution_logger.entry_count();
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Replay — start
    // -----------------------------------------------------------------------
    svr.Post("/api/replay/start", [this](const httplib::Request&, httplib::Response& res) {
        bool ok = ctx_.execution_logger.start_recording();
        json j;
        j["success"] = ok;
        j["state"] = ok ? "recording" : "already_recording";
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Replay — stop
    // -----------------------------------------------------------------------
    svr.Post("/api/replay/stop", [this](const httplib::Request&, httplib::Response& res) {
        bool ok = ctx_.execution_logger.stop_recording();
        json j;
        j["success"] = ok;
        j["entry_count"] = ctx_.execution_logger.entry_count();
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Inference — get config
    // -----------------------------------------------------------------------
    svr.Get("/api/inference", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(ctx_.inference_gateway.to_json().dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Inference — update config
    // -----------------------------------------------------------------------
    svr.Put("/api/inference", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto delta = json::parse(req.body);
            ctx_.inference_gateway.update_config(delta);
            res.set_content(ctx_.inference_gateway.to_json().dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Privacy — scan text for PII
    // -----------------------------------------------------------------------
    svr.Post("/api/privacy/scan", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string text = body.value("text", "");
            if (text.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"text field is required"})", "application/json");
                return;
            }

            auto matches = ctx_.privacy_filter.scan(text);
            json j;
            j["contains_pii"] = !matches.empty();
            j["match_count"] = matches.size();
            j["matches"] = json::array();
            for (const auto& m : matches) {
                json mj;
                mj["type"] = m.type_name;
                mj["start"] = m.start_pos;
                mj["end"] = m.end_pos;
                j["matches"].push_back(mj);
            }
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Policy — recommendations
    // -----------------------------------------------------------------------
    svr.Get("/api/policy/recommendations", [this](const httplib::Request&, httplib::Response& res) {
        auto recs = ctx_.policy_recommender.get_recommendations();
        json j = json::array();
        for (const auto& r : recs) {
            json rj;
            rj["category"] = r.category;
            rj["action"] = r.action;
            rj["resource"] = r.resource;
            rj["occurrence_count"] = r.occurrence_count;
            rj["suggested_change"] = r.suggested_change;
            j.push_back(rj);
        }
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Metrics — system overview
    // -----------------------------------------------------------------------
    svr.Get("/api/metrics", [this](const httplib::Request&, httplib::Response& res) {
        auto agents = ctx_.agent_manager.list_agents();
        auto gw_config = ctx_.inference_gateway.get_config();

        json j;
        j["state_store_size"] = ctx_.state_store.size();
        j["audit_entries"] = ctx_.audit_logger.entry_count();
        j["agent_count"] = agents.size();
        j["llm"] = {
            {"enabled", gw_config.enabled},
            {"current_cost_usd", gw_config.current_cost_usd},
            {"max_cost_usd", gw_config.max_cost_usd},
        };
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // MCP — list servers
    // -----------------------------------------------------------------------
    svr.Get("/api/mcp/servers", [this](const httplib::Request&, httplib::Response& res) {
        if (!ctx_.mcp_bridge) {
            res.set_content(R"({"enabled":false,"servers":[]})", "application/json");
            return;
        }
        auto statuses = ctx_.mcp_bridge->status();
        json j;
        j["enabled"] = true;
        j["servers"] = json::array();
        for (const auto& s : statuses) {
            json sj;
            sj["name"] = s.name;
            sj["connected"] = s.connected;
            sj["pid"] = s.pid;
            sj["tool_count"] = s.tool_count;
            sj["total_calls"] = s.total_calls;
            j["servers"].push_back(sj);
        }
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // MCP — list tools
    // -----------------------------------------------------------------------
    svr.Get("/api/mcp/tools", [this](const httplib::Request&, httplib::Response& res) {
        if (!ctx_.mcp_bridge) {
            res.set_content(R"({"enabled":false,"tools":[]})", "application/json");
            return;
        }
        auto tools = ctx_.mcp_bridge->list_tools();
        json j;
        j["enabled"] = true;
        j["tools"] = json::array();
        for (const auto& t : tools) {
            json tj;
            tj["server"] = t.server_name;
            tj["name"] = t.name;
            tj["description"] = t.description;
            j["tools"].push_back(tj);
        }
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Worlds — list
    // -----------------------------------------------------------------------
    svr.Get("/api/worlds", [this](const httplib::Request&, httplib::Response& res) {
        if (!ctx_.world_engine) {
            res.set_content(R"({"enabled":false,"worlds":[]})", "application/json");
            return;
        }
        auto worlds = ctx_.world_engine->list();
        json j = json::array();
        for (const auto& w : worlds) {
            json wj;
            wj["id"] = w.id;
            wj["name"] = w.name;
            wj["member_count"] = w.member_count;
            wj["metadata"] = w.metadata;
            j.push_back(wj);
        }
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Worlds — create
    // -----------------------------------------------------------------------
    svr.Post("/api/worlds", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.world_engine) {
            res.status = 503;
            res.set_content(R"({"error":"world engine not enabled"})", "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            std::string name = body.value("name", "");
            if (name.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name is required"})", "application/json");
                return;
            }
            auto metadata = body.value("metadata", json::object());
            uint32_t id = ctx_.world_engine->create(name, metadata);
            json j;
            j["id"] = id;
            j["name"] = name;
            res.status = 201;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Worlds — destroy
    // -----------------------------------------------------------------------
    svr.Delete(R"(/api/worlds/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        if (!ctx_.world_engine) {
            res.status = 503;
            res.set_content(R"({"error":"world engine not enabled"})", "application/json");
            return;
        }
        if (ctx_.world_engine->destroy(id)) {
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"world not found"})", "application/json");
        }
    });

    // ===================================================================
    // Dashboard — serve embedded HTML pages
    // ===================================================================

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/dashboard");
    });

    svr.Get("/dashboard", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(DASHBOARD_INDEX_HTML, "text/html");
    });

    svr.Get("/dashboard/agents", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(DASHBOARD_AGENTS_HTML, "text/html");
    });

    svr.Get("/dashboard/audit", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(DASHBOARD_AUDIT_HTML, "text/html");
    });

    svr.Get("/dashboard/style.css", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(DASHBOARD_CSS, "text/css");
    });

    // ===================================================================
    // HTMX Fragment endpoints — return HTML partials
    // ===================================================================

    // -- Uptime fragment --
    svr.Get("/api/fragments/uptime", [this](const httplib::Request&, httplib::Response& res) {
        auto now = std::chrono::steady_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        auto h = secs / 3600;
        auto m = (secs % 3600) / 60;
        auto s = secs % 60;
        std::ostringstream oss;
        oss << "Uptime: " << std::setfill('0')
            << std::setw(2) << h << ":"
            << std::setw(2) << m << ":"
            << std::setw(2) << s;
        res.set_content(oss.str(), "text/html");
    });

    // -- Agent count fragment --
    svr.Get("/api/fragments/agent-count", [this](const httplib::Request&, httplib::Response& res) {
        auto agents = ctx_.agent_manager.list_agents();
        res.set_content(std::to_string(agents.size()) + " total", "text/html");
    });

    // -- Agent list fragment (sidebar items) --
    svr.Get("/api/fragments/agents", [this](const httplib::Request&, httplib::Response& res) {
        auto agents = ctx_.agent_manager.list_agents();
        if (agents.empty()) {
            res.set_content(R"(<div class="empty-state">No agents running</div>)", "text/html");
            return;
        }
        std::ostringstream html;
        for (const auto& agent : agents) {
            auto metrics = agent->get_metrics();
            auto state = agent_state_to_string(agent->state());
            std::string state_lower = state;
            std::transform(state_lower.begin(), state_lower.end(), state_lower.begin(), ::tolower);

            html << R"(<div class="agent-item">)"
                 << R"(<div class="agent-info">)"
                 << R"(<div class="agent-name">)" << agent->name() << "</div>"
                 << R"(<div class="agent-meta">ID: )" << agent->id()
                 << " | PID: " << agent->pid()
                 << " | " << metrics.uptime_seconds << "s</div>"
                 << "</div>"
                 << R"(<span class="badge badge-)" << state_lower << R"(">)" << state << "</span>"
                 << "</div>";
        }
        res.set_content(html.str(), "text/html");
    });

    // -- Agent table fragment (full table for agents page) --
    svr.Get("/api/fragments/agent-table", [this](const httplib::Request&, httplib::Response& res) {
        auto agents = ctx_.agent_manager.list_agents();
        if (agents.empty()) {
            res.set_content(R"(<div class="empty-state">No agents running. Use the form above to spawn one.</div>)", "text/html");
            return;
        }
        std::ostringstream html;
        html << "<table><thead><tr>"
             << "<th>ID</th><th>Name</th><th>State</th><th>PID</th>"
             << "<th>Uptime</th><th>Memory</th><th>CPU</th><th>LLM Calls</th><th>Actions</th>"
             << "</tr></thead><tbody>";
        for (const auto& agent : agents) {
            auto m = agent->get_metrics();
            auto state = agent_state_to_string(agent->state());
            std::string state_lower = state;
            std::transform(state_lower.begin(), state_lower.end(), state_lower.begin(), ::tolower);

            // Format uptime
            std::ostringstream uptime;
            auto total_sec = static_cast<int>(m.uptime_seconds);
            auto h = total_sec / 3600;
            auto min = (total_sec % 3600) / 60;
            auto sec = total_sec % 60;
            uptime << std::setfill('0') << std::setw(2) << h << ":"
                   << std::setw(2) << min << ":" << std::setw(2) << sec;

            // Format memory
            std::ostringstream mem;
            if (m.memory_bytes > 1048576)
                mem << std::fixed << std::setprecision(1) << (m.memory_bytes / 1048576.0) << " MB";
            else if (m.memory_bytes > 1024)
                mem << std::fixed << std::setprecision(1) << (m.memory_bytes / 1024.0) << " KB";
            else
                mem << m.memory_bytes << " B";

            html << "<tr>"
                 << "<td>" << agent->id() << "</td>"
                 << "<td><strong>" << agent->name() << "</strong></td>"
                 << R"(<td><span class="badge badge-)" << state_lower << R"(">)" << state << "</span></td>"
                 << "<td>" << agent->pid() << "</td>"
                 << "<td>" << uptime.str() << "</td>"
                 << "<td>" << mem.str() << "</td>"
                 << "<td>" << std::fixed << std::setprecision(1) << m.cpu_percent << "%</td>"
                 << "<td>" << m.llm_request_count << "</td>"
                 << R"(<td><button class="btn-danger" )"
                 << R"(hx-delete="/api/agents/)" << agent->id() << R"(" )"
                 << R"(hx-confirm="Kill agent ')" << agent->name() << R"(' (ID )" << agent->id() << R"()?" )"
                 << R"(hx-target="closest tr" hx-swap="outerHTML">Kill</button></td>)"
                 << "</tr>";
        }
        html << "</tbody></table>";
        res.set_content(html.str(), "text/html");
    });

    // -- Metrics fragment --
    svr.Get("/api/fragments/metrics", [this](const httplib::Request&, httplib::Response& res) {
        auto agents = ctx_.agent_manager.list_agents();
        auto gw_config = ctx_.inference_gateway.get_config();
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();

        size_t running = 0;
        for (const auto& a : agents) {
            if (agent_state_to_string(a->state()) == std::string("RUNNING")) running++;
        }

        std::ostringstream html;
        html << R"(<div class="metrics-grid">)";

        html << R"(<div class="metric green"><div class="metric-value">)" << agents.size()
             << R"(</div><div class="metric-label">Agents</div></div>)";

        html << R"(<div class="metric green"><div class="metric-value">)" << running
             << R"(</div><div class="metric-label">Running</div></div>)";

        html << R"(<div class="metric"><div class="metric-value">)" << ctx_.audit_logger.entry_count()
             << R"(</div><div class="metric-label">Audit Entries</div></div>)";

        html << R"(<div class="metric"><div class="metric-value">)" << ctx_.state_store.size()
             << R"(</div><div class="metric-label">State Keys</div></div>)";

        // LLM cost
        std::ostringstream cost;
        cost << std::fixed << std::setprecision(4) << "$" << gw_config.current_cost_usd;
        std::string cost_class = gw_config.current_cost_usd > gw_config.max_cost_usd * 0.8 ? "red" :
                                 gw_config.current_cost_usd > gw_config.max_cost_usd * 0.5 ? "yellow" : "";
        html << R"(<div class="metric )" << cost_class << R"("><div class="metric-value">)" << cost.str()
             << R"(</div><div class="metric-label">LLM Cost</div></div>)";

        // Uptime
        auto h = uptime / 3600;
        auto m = (uptime % 3600) / 60;
        std::ostringstream ut;
        ut << h << "h " << m << "m";
        html << R"(<div class="metric"><div class="metric-value">)" << ut.str()
             << R"(</div><div class="metric-label">Uptime</div></div>)";

        html << "</div>";
        res.set_content(html.str(), "text/html");
    });

    // -- Inference fragment --
    svr.Get("/api/fragments/inference", [this](const httplib::Request&, httplib::Response& res) {
        auto gw = ctx_.inference_gateway.get_config();
        std::ostringstream html;

        std::string status_class = gw.enabled ? "badge-running" : "badge-stopped";
        std::string status_text = gw.enabled ? "ENABLED" : "DISABLED";

        html << R"(<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px;">)"
             << R"(<span>Status</span>)"
             << R"(<span class="badge )" << status_class << R"(">)" << status_text << "</span></div>";

        std::ostringstream cost;
        cost << std::fixed << std::setprecision(4) << "$" << gw.current_cost_usd
             << " / $" << std::setprecision(2) << gw.max_cost_usd;

        html << R"(<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px;">)"
             << R"(<span style="color:var(--text-secondary);font-size:12px;">Cost</span>)"
             << R"(<span class="ts">)" << cost.str() << "</span></div>";

        // Cost bar
        double pct = gw.max_cost_usd > 0 ? (gw.current_cost_usd / gw.max_cost_usd * 100.0) : 0;
        if (pct > 100) pct = 100;
        std::string bar_color = pct > 80 ? "var(--accent-red)" :
                                pct > 50 ? "var(--accent-yellow)" : "var(--accent-green)";
        html << R"(<div style="background:var(--bg-primary);border-radius:4px;height:6px;overflow:hidden;">)"
             << R"(<div style="width:)" << std::fixed << std::setprecision(1) << pct
             << R"(%;height:100%;background:)" << bar_color << R"(;border-radius:4px;transition:width 0.3s;"></div>)"
             << "</div>";

        res.set_content(html.str(), "text/html");
    });

    // -- Audit fragment --
    svr.Get("/api/fragments/audit", [this](const httplib::Request& req, httplib::Response& res) {
        AuditCategory* cat_ptr = nullptr;
        AuditCategory cat_val{};
        if (req.has_param("category")) {
            cat_val = audit_category_from_string(req.get_param_value("category"));
            cat_ptr = &cat_val;
        }

        size_t limit = 100;
        if (req.has_param("limit")) {
            limit = std::stoull(req.get_param_value("limit"));
        }

        auto entries = ctx_.audit_logger.get_entries(cat_ptr, nullptr, 0, limit);

        if (entries.empty()) {
            res.set_content(R"(<div class="empty-state">No audit entries found</div>)", "text/html");
            return;
        }

        std::ostringstream html;
        html << "<table><thead><tr>"
             << "<th>ID</th><th>Time</th><th>Category</th><th>Event</th>"
             << "<th>Agent</th><th>Status</th><th>Details</th>"
             << "</tr></thead><tbody>";

        for (const auto& e : entries) {
            // Format timestamp
            auto tp = std::chrono::system_clock::to_time_t(e.timestamp);
            std::ostringstream ts;
            ts << std::put_time(std::localtime(&tp), "%H:%M:%S");

            auto cat_str = audit_category_to_string(e.category);
            std::string cat_lower = cat_str;
            std::transform(cat_lower.begin(), cat_lower.end(), cat_lower.begin(), ::tolower);

            std::string status_class = e.success ? "badge-ok" : "badge-error";
            std::string status_text = e.success ? "OK" : "FAIL";

            // Truncate details
            std::string details = e.details.dump();
            if (details.size() > 80) details = details.substr(0, 77) + "...";

            html << "<tr>"
                 << "<td>" << e.id << "</td>"
                 << R"(<td class="ts">)" << ts.str() << "</td>"
                 << R"(<td><span class="badge badge-)" << cat_lower << R"(">)" << cat_str << "</span></td>"
                 << "<td>" << e.event_type << "</td>"
                 << "<td>";
            if (e.agent_id > 0) {
                html << e.agent_name << " <span style=\"color:var(--text-muted);\">#" << e.agent_id << "</span>";
            } else {
                html << "<span style=\"color:var(--text-muted);\">kernel</span>";
            }
            html << "</td>"
                 << R"(<td><span class="badge )" << status_class << R"(">)" << status_text << "</span></td>"
                 << R"(<td class="details-cell" title=")" << e.details.dump() << R"(">)" << details << "</td>"
                 << "</tr>";
        }
        html << "</tbody></table>";
        res.set_content(html.str(), "text/html");
    });

    // ===================================================================
    // Product API — /api/think, /api/run, /api/fleet
    // ===================================================================

    // -----------------------------------------------------------------------
    // Exec — direct shell command execution (no LLM, no agent loop)
    // POST /api/exec  {"command": "ls -la", "workdir": "/tmp", "timeout_ms": 30000}
    // Used by sandbox providers (OpenClaw CLOVE provider) for direct tool execution.
    // -----------------------------------------------------------------------
    svr.Post("/api/exec", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string command = body.value("command", "");
            std::string workdir = body.value("workdir", "");
            int timeout_ms = body.value("timeout_ms", 30000);

            if (command.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"command is required"})", "application/json");
                return;
            }

            // Permission check
            auto& perms = ctx_.permissions_store.get_or_create(0);
            if (!perms.can_exec) {
                ctx_.audit_logger.log(AuditCategory::SECURITY, "EXEC_DENIED", 0, "", {{"command", command}}, false);
                res.status = 403;
                res.set_content(R"({"error":"exec permission denied"})", "application/json");
                return;
            }

            // Audit
            ctx_.audit_logger.log(AuditCategory::SYSCALL, "DIRECT_EXEC", 0, "", {{"command", command}});

            // Execute via popen
            std::string full_cmd = command;
            if (!workdir.empty()) {
                full_cmd = "cd " + workdir + " && " + command;
            }
            full_cmd += " 2>&1";

            FILE* pipe = popen(full_cmd.c_str(), "r");
            if (!pipe) {
                res.status = 500;
                res.set_content(R"({"error":"popen failed"})", "application/json");
                return;
            }

            std::string output;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe)) {
                output += buf;
                if (output.size() > 512 * 1024) break; // 512KB cap
            }
            int exit_code = pclose(pipe);
            exit_code = WIFEXITED(exit_code) ? WEXITSTATUS(exit_code) : -1;

            json j;
            j["stdout"] = output;
            j["stderr"] = "";
            j["exit_code"] = exit_code;
            j["command"] = command;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j; j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Think — one-shot LLM call with cost tracking
    // POST /api/think  {"prompt": "...", "model": "optional"}
    // -----------------------------------------------------------------------
    svr.Post("/api/think", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openrouter || !ctx_.openrouter->is_configured()) {
            res.status = 503;
            res.set_content(R"({"error":"LLM not configured. Set OPENROUTER_API_KEY."})", "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            std::string prompt = body.value("prompt", "");
            std::string model = body.value("model", ctx_.config.llm_model);
            auto messages = body.value("messages", json::array());

            if (prompt.empty() && messages.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"prompt or messages required"})", "application/json");
                return;
            }

            // PII filter
            std::string filtered = prompt;
            size_t pii_count = 0;
            if (ctx_.privacy_filter.is_enabled() && !prompt.empty()) {
                auto pii_result = ctx_.privacy_filter.redact(prompt);
                filtered = pii_result.cleaned_text;
                pii_count = pii_result.matches.size();
            }

            // Call LLM
            OpenRouterResponse llm_resp;
            if (!messages.empty()) {
                llm_resp = ctx_.openrouter->chat_messages(model, messages);
            } else {
                llm_resp = ctx_.openrouter->chat(model, filtered);
            }

            // Track cost
            if (llm_resp.success) {
                ctx_.inference_gateway.record_cost(llm_resp.usage.cost_usd);
            }

            // Audit
            ctx_.audit_logger.log(AuditCategory::RESOURCE, "THINK",
                0, "", {
                    {"model", llm_resp.model_used},
                    {"tokens", llm_resp.usage.total_tokens},
                    {"cost_usd", llm_resp.usage.cost_usd},
                    {"prompt_len", filtered.size()},
                });

            json j;
            j["success"] = llm_resp.success;
            j["content"] = llm_resp.content;
            j["model"] = llm_resp.model_used;
            j["tokens"] = llm_resp.usage.total_tokens;
            j["prompt_tokens"] = llm_resp.usage.prompt_tokens;
            j["completion_tokens"] = llm_resp.usage.completion_tokens;
            j["cost_usd"] = llm_resp.usage.cost_usd;
            if (pii_count > 0) j["pii_redacted"] = pii_count;
            if (!llm_resp.success) j["error"] = llm_resp.error;
            res.set_content(j.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // OpenAI-compatible chat completions endpoint
    // POST /api/v1/chat/completions
    //
    // Drop-in replacement for OpenAI's API. Any tool that speaks OpenAI format
    // (OpenClaw, LangChain, Cursor, Continue, etc.) can point at this endpoint
    // and get cost tracking, PII filtering, and audit for free.
    // -----------------------------------------------------------------------
    svr.Post("/api/v1/chat/completions", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openrouter || !ctx_.openrouter->is_configured()) {
            res.status = 503;
            json err;
            err["error"] = json{{"message", "LLM not configured. Set OPENROUTER_API_KEY."}, {"type", "server_error"}};
            res.set_content(err.dump(), "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            std::string model = body.value("model", ctx_.config.llm_model);
            auto messages = body.value("messages", json::array());

            if (messages.empty()) {
                res.status = 400;
                json err;
                err["error"] = json{{"message", "messages is required"}, {"type", "invalid_request_error"}};
                res.set_content(err.dump(), "application/json");
                return;
            }

            // PII filter on ALL messages (user, system, assistant — OpenClaw embeds
            // user content across all roles in its conversation history)
            size_t pii_count = 0;
            if (ctx_.privacy_filter.is_enabled()) {
                for (auto& msg : messages) {
                    if (msg.contains("content") && msg["content"].is_string()) {
                        auto pii_result = ctx_.privacy_filter.redact(msg["content"].get<std::string>());
                        if (pii_result.matches.size() > 0) {
                            msg["content"] = pii_result.cleaned_text;
                            pii_count += pii_result.matches.size();
                        }
                    }
                }
            }

            // Strip "clove/" prefix from model name if present (e.g. "clove/anthropic/claude-sonnet-4" → "anthropic/claude-sonnet-4")
            if (model.substr(0, 6) == "clove/") {
                model = model.substr(6);
            }
            // Map "auto" to kernel's configured model
            if (model == "auto" || model.empty()) {
                model = ctx_.config.llm_model;
            }

            bool stream = body.value("stream", false);

            // Call LLM via OpenRouter
            auto llm_resp = ctx_.openrouter->chat_messages(model, messages);

            // Track cost
            if (llm_resp.success) {
                ctx_.inference_gateway.record_cost(llm_resp.usage.cost_usd);
            }

            // Audit
            ctx_.audit_logger.log(AuditCategory::RESOURCE, "CHAT_COMPLETION",
                0, "", {
                    {"model", llm_resp.model_used},
                    {"tokens", llm_resp.usage.total_tokens},
                    {"cost_usd", llm_resp.usage.cost_usd},
                    {"pii_redacted", pii_count},
                    {"stream", stream},
                });

            // Generate a unique ID
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            std::string id = "chatcmpl-clove-" + std::to_string(ms);

            if (stream) {
                // SSE streaming response — OpenClaw and other clients expect this
                res.set_header("Content-Type", "text/event-stream");
                res.set_header("Cache-Control", "no-cache");
                res.set_header("Connection", "keep-alive");

                // Chunk 1: the content
                json chunk;
                chunk["id"] = id;
                chunk["object"] = "chat.completion.chunk";
                chunk["created"] = ms / 1000;
                chunk["model"] = llm_resp.model_used;

                json delta;
                delta["role"] = "assistant";
                delta["content"] = llm_resp.content;

                json ch;
                ch["index"] = 0;
                ch["delta"] = delta;
                ch["finish_reason"] = nullptr;
                chunk["choices"] = json::array({ch});

                std::string sse = "data: " + chunk.dump() + "\n\n";

                // Chunk 2: finish
                json finish_chunk;
                finish_chunk["id"] = id;
                finish_chunk["object"] = "chat.completion.chunk";
                finish_chunk["created"] = ms / 1000;
                finish_chunk["model"] = llm_resp.model_used;

                json finish_delta;
                json fch;
                fch["index"] = 0;
                fch["delta"] = json::object();
                fch["finish_reason"] = "stop";
                finish_chunk["choices"] = json::array({fch});

                // Usage in final chunk (OpenAI spec)
                finish_chunk["usage"] = json{
                    {"prompt_tokens", llm_resp.usage.prompt_tokens},
                    {"completion_tokens", llm_resp.usage.completion_tokens},
                    {"total_tokens", llm_resp.usage.total_tokens},
                };

                sse += "data: " + finish_chunk.dump() + "\n\n";
                sse += "data: [DONE]\n\n";

                res.set_content(sse, "text/event-stream");
                return;
            }

            // Non-streaming response
            json resp;
            resp["id"] = id;
            resp["object"] = "chat.completion";
            resp["created"] = ms / 1000;
            resp["model"] = llm_resp.model_used;

            json choice;
            choice["index"] = 0;
            choice["message"] = json{{"role", "assistant"}, {"content", llm_resp.content}};
            choice["finish_reason"] = "stop";
            resp["choices"] = json::array({choice});

            json usage;
            usage["prompt_tokens"] = llm_resp.usage.prompt_tokens;
            usage["completion_tokens"] = llm_resp.usage.completion_tokens;
            usage["total_tokens"] = llm_resp.usage.total_tokens;
            resp["usage"] = usage;

            resp["clove"] = json{
                {"cost_usd", llm_resp.usage.cost_usd},
                {"pii_redacted", pii_count},
                {"audited", true},
            };

            res.set_content(resp.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            json err;
            err["error"] = json{{"message", std::string("invalid request: ") + e.what()}, {"type", "invalid_request_error"}};
            res.set_content(err.dump(), "application/json");
        }
    });

    // Also handle /v1/chat/completions (without /api prefix — matches OpenAI's URL structure)
    svr.Post("/v1/chat/completions", [this](const httplib::Request& req, httplib::Response& res) {
        // Forward to the /api/v1 handler by re-dispatching
        // For simplicity, duplicate the lambda reference — httplib doesn't support redirect
        // Instead, just tell clients to use /api/v1/chat/completions
        res.status = 308;
        res.set_header("Location", "/api/v1/chat/completions");
        res.set_content(R"({"error":"Use /api/v1/chat/completions"})", "application/json");
    });

    // -----------------------------------------------------------------------
    // Run — full agent tool-calling loop (synchronous)
    // POST /api/run  {"goal": "...", "budget": 0.50, "tools": [...], "model": "..."}
    // -----------------------------------------------------------------------
    svr.Post("/api/run", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openrouter || !ctx_.openrouter->is_configured()) {
            res.status = 503;
            res.set_content(R"({"error":"LLM not configured. Set OPENROUTER_API_KEY."})", "application/json");
            return;
        }

        try {
            auto body = json::parse(req.body);
            RunConfig cfg;
            cfg.goal = body.value("goal", "");
            cfg.model = body.value("model", ctx_.config.llm_model);
            cfg.budget_usd = body.value("budget", 1.0);
            cfg.max_steps = body.value("max_steps", 20);
            cfg.allowed_tools = body.value("tools", std::vector<std::string>{});
            cfg.agent_name = body.value("agent_name", "agent");

            if (cfg.goal.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"goal is required"})", "application/json");
                return;
            }

            RunEngine engine(*ctx_.openrouter, ctx_.inference_gateway, ctx_.privacy_filter,
                             ctx_.audit_logger, ctx_.state_store, ctx_.permissions_store,
                             ctx_.artifact_store, ctx_.chain_store,
                             ctx_.memory_blocks, ctx_.mcp_bridge, ctx_.assembler,
                             ctx_.config);

            auto result = engine.execute(cfg);

            json j;
            j["success"] = result.success;
            j["content"] = result.content;
            j["steps"] = result.steps;
            j["total_tokens"] = result.total_tokens;
            j["total_cost_usd"] = result.total_cost_usd;
            j["budget_usd"] = cfg.budget_usd;
            j["model"] = cfg.model.empty() ? ctx_.config.llm_model : cfg.model;
            j["chain_id"] = result.chain_id;
            j["step_log"] = result.step_log;
            if (!result.error.empty()) j["error"] = result.error;
            res.set_content(j.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Run with SSE streaming — real-time events
    // POST /api/run/stream  {"goal": "...", "budget": 0.50, ...}
    // Returns: text/event-stream with events as they happen
    // -----------------------------------------------------------------------
    svr.Post("/api/run/stream", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openrouter || !ctx_.openrouter->is_configured()) {
            res.status = 503;
            res.set_content(R"({"error":"LLM not configured. Set OPENROUTER_API_KEY."})", "application/json");
            return;
        }

        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid JSON"})", "application/json");
            return;
        }

        RunConfig cfg;
        cfg.goal = body.value("goal", "");
        cfg.model = body.value("model", ctx_.config.llm_model);
        cfg.budget_usd = body.value("budget", 1.0);
        cfg.max_steps = body.value("max_steps", 20);
        cfg.allowed_tools = body.value("tools", std::vector<std::string>{});
        cfg.agent_name = body.value("agent_name", "agent");

        if (cfg.goal.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"goal is required"})", "application/json");
            return;
        }

        // Capture what we need for the lambda
        auto* openrouter = ctx_.openrouter;
        auto* gateway = &ctx_.inference_gateway;
        auto* privacy = &ctx_.privacy_filter;
        auto* audit = &ctx_.audit_logger;
        auto* state = &ctx_.state_store;
        auto* artifacts = ctx_.artifact_store;
        auto* chains = ctx_.chain_store;
        auto* memory = ctx_.memory_blocks;
        auto* mcp = ctx_.mcp_bridge;
        auto* assembler = ctx_.assembler;
        auto* perms = &ctx_.permissions_store;
        auto* config = &ctx_.config;

        res.set_chunked_content_provider(
            "text/event-stream",
            [openrouter, gateway, privacy, audit, state, perms, artifacts, chains, memory, mcp, assembler, config, cfg]
            (size_t /*offset*/, httplib::DataSink& sink) -> bool {

                RunEngine engine(*openrouter, *gateway, *privacy, *audit, *state, *perms,
                                 artifacts, chains, memory, mcp, assembler, *config);

                auto result = engine.execute(cfg, [&sink](const RunEvent& ev) {
                    json event_data;
                    event_data["type"] = ev.type;
                    event_data["data"] = ev.data;
                    std::string line = "data: " + event_data.dump() + "\n\n";
                    sink.write(line.c_str(), line.size());
                });

                // Send final result
                json final_event;
                final_event["type"] = "result";
                final_event["data"] = {
                    {"success", result.success},
                    {"content", result.content},
                    {"steps", result.steps},
                    {"total_tokens", result.total_tokens},
                    {"total_cost_usd", result.total_cost_usd},
                    {"chain_id", result.chain_id},
                    {"step_log", result.step_log},
                };
                if (!result.error.empty()) final_event["data"]["error"] = result.error;
                std::string line = "data: " + final_event.dump() + "\n\n";
                sink.write(line.c_str(), line.size());

                sink.done();
                return true;
            },
            nullptr  // no resource releaser
        );
    });

    // -----------------------------------------------------------------------
    // Fleet — parallel agent runs with SSE streaming
    // POST /api/fleet  {"goal": "...", "agents": 5, "budget": 2.00, ...}
    // Returns: text/event-stream with events from all agents
    // -----------------------------------------------------------------------
    svr.Post("/api/fleet", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openrouter || !ctx_.openrouter->is_configured()) {
            res.status = 503;
            res.set_content(R"({"error":"LLM not configured. Set OPENROUTER_API_KEY."})", "application/json");
            return;
        }

        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid JSON"})", "application/json");
            return;
        }

        std::string goal = body.value("goal", "");
        int agent_count = body.value("agents", 3);
        double total_budget = body.value("budget", 2.0);
        std::string default_model = body.value("model", ctx_.config.llm_model);
        int max_steps = body.value("max_steps", 15);
        auto tools = body.value("tools", std::vector<std::string>{});
        std::string world_name = body.value("world", "fleet-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 100000));

        // Per-agent configs (optional — if not provided, all agents use defaults)
        json agent_configs = json::array();
        if (body.contains("agent_configs") && body["agent_configs"].is_array()) {
            agent_configs = body["agent_configs"];
            agent_count = static_cast<int>(agent_configs.size());
        }

        if (goal.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"goal is required"})", "application/json");
            return;
        }

        agent_count = std::min(agent_count, 20);  // Safety cap
        double per_agent_budget = total_budget / agent_count;

        auto* openrouter = ctx_.openrouter;
        auto* gateway = &ctx_.inference_gateway;
        auto* privacy = &ctx_.privacy_filter;
        auto* audit = &ctx_.audit_logger;
        auto* state = &ctx_.state_store;
        auto* artifacts = ctx_.artifact_store;
        auto* chains = ctx_.chain_store;
        auto* memory = ctx_.memory_blocks;
        auto* mcp = ctx_.mcp_bridge;
        auto* assembler = ctx_.assembler;
        auto* perms = &ctx_.permissions_store;
        auto* config = &ctx_.config;

        res.set_chunked_content_provider(
            "text/event-stream",
            [=](size_t /*offset*/, httplib::DataSink& sink) -> bool {

                std::mutex sink_mtx;
                auto safe_emit = [&sink, &sink_mtx](const json& event) {
                    std::lock_guard<std::mutex> lock(sink_mtx);
                    std::string line = "data: " + event.dump() + "\n\n";
                    sink.write(line.c_str(), line.size());
                };

                // Create a world for this fleet
                uint32_t world_id = 0;
                if (ctx_.world_engine) {
                    world_id = ctx_.world_engine->create(world_name, {{"goal", goal}, {"agent_count", agent_count}});
                }

                // Fleet start event
                safe_emit({
                    {"type", "fleet_start"},
                    {"data", {
                        {"goal", goal},
                        {"agent_count", agent_count},
                        {"total_budget_usd", total_budget},
                        {"per_agent_budget_usd", per_agent_budget},
                        {"world", world_name},
                        {"world_id", world_id},
                    }}
                });

                // Phase 1: Run N agents in parallel
                std::vector<std::thread> threads;
                std::vector<RunResult> results(agent_count);
                std::atomic<int> completed{0};

                for (int i = 0; i < agent_count; i++) {
                    threads.emplace_back([&, i]() {
                        // Build per-agent config from agent_configs or defaults
                        std::string agent_name = "agent-" + std::to_string(i + 1);
                        std::string agent_model = default_model;
                        std::string agent_role;
                        double agent_budget = per_agent_budget;
                        int agent_max_steps = max_steps;
                        std::vector<std::string> agent_tools = tools;

                        if (i < static_cast<int>(agent_configs.size())) {
                            auto& ac = agent_configs[i];
                            agent_name = ac.value("name", agent_name);
                            agent_model = ac.value("model", default_model);
                            agent_role = ac.value("role", "");
                            agent_budget = ac.value("budget", per_agent_budget);
                            agent_max_steps = ac.value("max_steps", max_steps);
                            if (ac.contains("tools") && ac["tools"].is_array()) {
                                agent_tools = ac["tools"].get<std::vector<std::string>>();
                            }
                        }

                        // Build agent goal with role context
                        std::string agent_goal = goal;
                        if (!agent_role.empty()) {
                            agent_goal = "YOUR ROLE: " + agent_role + "\n\nMISSION: " + goal;
                        }

                        RunConfig cfg;
                        cfg.goal = agent_goal;
                        cfg.model = agent_model;
                        cfg.budget_usd = agent_budget;
                        cfg.max_steps = agent_max_steps;
                        cfg.allowed_tools = agent_tools;
                        cfg.agent_name = agent_name;

                        RunEngine engine(*openrouter, *gateway, *privacy, *audit,
                                         *state, *perms, artifacts, chains, memory, mcp, assembler, *config);

                        results[i] = engine.execute(cfg, [&, i](const RunEvent& ev) {
                            json wrapped;
                            wrapped["type"] = "agent_event";
                            wrapped["data"] = ev.data;
                            wrapped["data"]["agent"] = "agent-" + std::to_string(i + 1);
                            wrapped["data"]["event_type"] = ev.type;
                            safe_emit(wrapped);
                        });

                        int done = ++completed;
                        safe_emit({
                            {"type", "agent_done"},
                            {"data", {
                                {"agent", "agent-" + std::to_string(i + 1)},
                                {"success", results[i].success},
                                {"steps", results[i].steps},
                                {"cost_usd", results[i].total_cost_usd},
                                {"tokens", results[i].total_tokens},
                                {"completed", done},
                                {"total", agent_count},
                            }}
                        });
                    });
                }

                // Wait for all agents
                for (auto& t : threads) {
                    t.join();
                }

                // Aggregate results
                double total_cost = 0;
                int total_tokens = 0;
                int total_steps = 0;
                json agent_outputs = json::array();
                bool all_success = true;

                for (int i = 0; i < agent_count; i++) {
                    total_cost += results[i].total_cost_usd;
                    total_tokens += results[i].total_tokens;
                    total_steps += results[i].steps;
                    if (!results[i].success) all_success = false;
                    agent_outputs.push_back({
                        {"agent", "agent-" + std::to_string(i + 1)},
                        {"success", results[i].success},
                        {"content_preview", results[i].content.substr(0, 300)},
                        {"steps", results[i].steps},
                        {"cost_usd", results[i].total_cost_usd},
                        {"chain_id", results[i].chain_id},
                    });
                }

                // Phase 2: Synthesize (if multiple agents produced results)
                std::string synthesis;
                double synth_cost = 0;
                if (agent_count > 1 && all_success) {
                    std::string synth_prompt = "Synthesize these " +
                        std::to_string(agent_count) + " research outputs into one coherent summary:\n\n";
                    for (int i = 0; i < agent_count; i++) {
                        synth_prompt += "--- Agent " + std::to_string(i+1) + " ---\n";
                        synth_prompt += results[i].content.substr(0, 2000) + "\n\n";
                    }

                    safe_emit({
                        {"type", "synthesizing"},
                        {"data", {{"message", "Synthesizing outputs from all agents..."}}}
                    });

                    auto synth_resp = openrouter->chat(default_model, synth_prompt);
                    synthesis = synth_resp.content;
                    synth_cost = synth_resp.usage.cost_usd;
                    total_cost += synth_cost;
                    total_tokens += synth_resp.usage.total_tokens;
                    gateway->record_cost(synth_cost);
                } else if (agent_count == 1) {
                    synthesis = results[0].content;
                }

                // Fleet done
                safe_emit({
                    {"type", "fleet_done"},
                    {"data", {
                        {"success", all_success},
                        {"synthesis", synthesis.substr(0, 500)},
                        {"agent_count", agent_count},
                        {"total_steps", total_steps},
                        {"total_tokens", total_tokens},
                        {"total_cost_usd", total_cost},
                        {"agents", agent_outputs},
                    }}
                });

                // Store synthesis as artifact
                if (artifacts && chains && !synthesis.empty()) {
                    auto chain = chains->create(0, "fleet", goal.substr(0, 200));
                    artifacts->create(0, ArtifactType::REPORT,
                        "Fleet Synthesis", synthesis, chain.id);
                }

                sink.done();
                return true;
            },
            nullptr
        );
    });


    // -----------------------------------------------------------------------
    // Run status — get result of a past run by chain ID
    // GET /api/run/:chain_id
    // -----------------------------------------------------------------------
    svr.Get(R"(/api/run/([a-z0-9_]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string chain_id = req.matches[1];
        if (!ctx_.chain_store) {
            res.status = 503;
            res.set_content(R"({"error":"chain store not available"})", "application/json");
            return;
        }
        auto chain_opt = ctx_.chain_store->get(chain_id);
        if (!chain_opt) {
            res.status = 404;
            res.set_content(R"({"error":"run not found"})", "application/json");
            return;
        }
        json j;
        j["chain_id"] = chain_opt->id;
        j["name"] = chain_opt->name;
        j["description"] = chain_opt->description;
        j["created_at_ms"] = chain_opt->created_at_ms;
        j["artifact_count"] = chain_opt->artifact_ids.size();

        // Fetch artifacts
        if (ctx_.artifact_store) {
            ArtifactFilter filter;
            filter.chain_id = chain_id;
            auto arts = ctx_.artifact_store->list(filter);
            json arts_arr = json::array();
            for (const auto& a : arts) {
                arts_arr.push_back({
                    {"id", a.id},
                    {"type", artifact_type_to_string(a.type)},
                    {"title", a.title},
                    {"content_preview", a.content.substr(0, 500)},
                    {"state", artifact_state_to_string(a.state)},
                });
            }
            j["artifacts"] = arts_arr;
        }
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // History — list past runs
    // GET /api/history?limit=50
    // -----------------------------------------------------------------------
    svr.Get("/api/history", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.chain_store) {
            res.set_content(R"({"runs":[]})", "application/json");
            return;
        }
        // List chains as run history
        auto chains = ctx_.chain_store->list(100);
        json j = json::array();
        for (const auto& c : chains) {
            j.push_back({
                {"chain_id", c.id},
                {"name", c.name},
                {"description", c.description},
                {"artifact_count", c.artifact_ids.size()},
                {"created_at_ms", c.created_at_ms},
            });
        }
        json resp;
        resp["runs"] = j;
        resp["count"] = j.size();
        res.set_content(resp.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Budget — get agent budget
    // GET /api/agents/:id/budget
    // -----------------------------------------------------------------------
    svr.Get(R"(/api/agents/(\d+)/budget)", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        auto& budget = ctx_.permissions_store.get_or_create_budget(id);
        res.set_content(budget.to_json().dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Budget — set agent budget
    // POST /api/agents/:id/budget  {"max_tokens": N, "max_cost_usd": N}
    // -----------------------------------------------------------------------
    svr.Post(R"(/api/agents/(\d+)/budget)", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        try {
            auto body = json::parse(req.body);
            auto& budget = ctx_.permissions_store.get_or_create_budget(id);
            if (body.contains("max_tokens")) budget.max_tokens = body["max_tokens"].get<uint64_t>();
            if (body.contains("max_cost_usd")) budget.max_cost_usd = body["max_cost_usd"].get<double>();
            if (body.contains("max_steps")) budget.max_steps = body["max_steps"].get<uint64_t>();
            if (body.contains("max_time_ms")) budget.max_time_ms = body["max_time_ms"].get<uint64_t>();
            if (body.contains("kill_on_exceeded")) budget.kill_on_exceeded = body["kill_on_exceeded"].get<bool>();
            res.set_content(budget.to_json().dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // KV Store — write
    // POST /api/store  {"key": "...", "value": "..."}
    // -----------------------------------------------------------------------
    svr.Post("/api/store", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string key = body.value("key", "");
            std::string value = body.value("value", "");
            if (key.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"key is required"})", "application/json");
                return;
            }
            ctx_.state_store.store(key, value, 0);
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // KV Store — read
    // GET /api/store/:key
    // -----------------------------------------------------------------------
    svr.Get(R"(/api/store/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1];
        auto val = ctx_.state_store.fetch(key, 0);
        if (val) {
            json j;
            j["key"] = key;
            j["value"] = *val;
            res.set_content(j.dump(), "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"key not found"})", "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // MCP — call a tool
    // POST /api/mcp/call  {"server": "...", "tool": "...", "arguments": {...}}
    // -----------------------------------------------------------------------
    svr.Post("/api/mcp/call", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.mcp_bridge) {
            res.status = 503;
            res.set_content(R"({"error":"MCP bridge not enabled. Start with --mcp"})", "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            std::string server = body.value("server", "");
            std::string tool = body.value("tool", "");
            auto args = body.value("arguments", json::object());

            if (server.empty() || tool.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"server and tool are required"})", "application/json");
                return;
            }

            auto result = ctx_.mcp_bridge->call_tool(0, server, tool, args);
            json j;
            j["success"] = result.success;
            j["content"] = result.content;
            j["duration_ms"] = result.duration_ms;
            if (!result.error.empty()) j["error"] = result.error;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid JSON: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // System cost — total spend
    // GET /api/cost
    // -----------------------------------------------------------------------
    svr.Get("/api/cost", [this](const httplib::Request&, httplib::Response& res) {
        auto gw = ctx_.inference_gateway.get_config();
        json j;
        j["total_cost_usd"] = gw.current_cost_usd;
        j["max_cost_usd"] = gw.max_cost_usd;
        j["total_requests"] = ctx_.llm_queue ? ctx_.llm_queue->total_requests() : 0;
        j["total_completed"] = ctx_.llm_queue ? ctx_.llm_queue->total_completed() : 0;
        res.set_content(j.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Memory — relevance search
    // GET /api/memory/search?q=query&limit=5
    // -----------------------------------------------------------------------
    svr.Get("/api/memory/search", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.memory_blocks) {
            res.set_content(R"({"results":[],"count":0})", "application/json");
            return;
        }
        std::string query = req.get_param_value("q");
        int limit = 5;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
        }

        auto scored = ctx_.memory_blocks->search(query, 0, static_cast<size_t>(limit));
        json arr = json::array();
        for (const auto& sb : scored) {
            json j = memory_block_to_json(sb.block);
            j["relevance_score"] = sb.score;
            arr.push_back(j);
        }
        json resp;
        resp["results"] = arr;
        resp["count"] = arr.size();
        resp["query"] = query;
        res.set_content(resp.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Memory — list blocks
    // GET /api/memory
    // -----------------------------------------------------------------------
    svr.Get("/api/memory", [this](const httplib::Request&, httplib::Response& res) {
        if (!ctx_.memory_blocks) {
            res.set_content(R"({"blocks":[]})", "application/json");
            return;
        }
        auto blocks = ctx_.memory_blocks->list(0, 100);
        json j = json::array();
        for (const auto& b : blocks) {
            j.push_back(memory_block_to_json(b));
        }
        json resp;
        resp["blocks"] = j;
        resp["count"] = j.size();
        res.set_content(resp.dump(), "application/json");
    });

    // -----------------------------------------------------------------------
    // Memory — create block
    // POST /api/memory  {"name": "...", "type": "core", "content": "..."}
    // -----------------------------------------------------------------------
    svr.Post("/api/memory", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.memory_blocks) {
            res.status = 503;
            res.set_content(R"({"error":"memory blocks not available"})", "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            std::string name = body.value("name", "");
            std::string type_str = body.value("type", "core");
            std::string content = body.value("content", "");
            std::string access_str = body.value("access", "private");

            if (name.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name is required"})", "application/json");
                return;
            }

            auto block = ctx_.memory_blocks->create(0, name,
                memory_block_type_from_string(type_str),
                memory_access_from_string(access_str),
                content, 0);
            res.status = 201;
            res.set_content(memory_block_to_json(block).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Memory — read block
    // GET /api/memory/:id
    // -----------------------------------------------------------------------
    svr.Get(R"(/api/memory/([a-z0-9_]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.memory_blocks) {
            res.status = 503;
            res.set_content(R"({"error":"memory blocks not available"})", "application/json");
            return;
        }
        std::string id = req.matches[1];
        auto opt = ctx_.memory_blocks->get(id, 0);
        if (!opt) {
            // Try by name
            opt = ctx_.memory_blocks->get_by_name(id, 0);
        }
        if (opt) {
            res.set_content(memory_block_to_json(*opt).dump(), "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"memory block not found"})", "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Memory — update block content
    // PUT /api/memory/:id  {"content": "..."}
    // -----------------------------------------------------------------------
    svr.Put(R"(/api/memory/([a-z0-9_]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.memory_blocks) {
            res.status = 503;
            res.set_content(R"({"error":"memory blocks not available"})", "application/json");
            return;
        }
        std::string id = req.matches[1];
        try {
            auto body = json::parse(req.body);
            std::string content = body.value("content", "");
            if (ctx_.memory_blocks->write(id, content, 0)) {
                auto opt = ctx_.memory_blocks->get(id, 0);
                if (opt) {
                    res.set_content(memory_block_to_json(*opt).dump(), "application/json");
                } else {
                    res.set_content(R"({"success":true})", "application/json");
                }
            } else {
                res.status = 404;
                res.set_content(R"({"error":"block not found or no access"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // -----------------------------------------------------------------------
    // Memory — delete block
    // DELETE /api/memory/:id
    // -----------------------------------------------------------------------
    svr.Delete(R"(/api/memory/([a-z0-9_]+))", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.memory_blocks) {
            res.status = 503;
            res.set_content(R"({"error":"memory blocks not available"})", "application/json");
            return;
        }
        std::string id = req.matches[1];
        if (ctx_.memory_blocks->remove(id, 0)) {
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"block not found or not owner"})", "application/json");
        }
    });

    // ===================================================================
    // ===================================================================
    // ===================================================================
    // Permissions — per-agent sandbox visualization
    // ===================================================================

    // GET /api/agents/:id/permissions
    svr.Get(R"(/api/agents/(\d+)/permissions)", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t agent_id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        auto& perms = ctx_.permissions_store.get_or_create(agent_id);
        auto& budget = ctx_.permissions_store.get_or_create_budget(agent_id);
        json j;
        j["agent_id"] = agent_id;
        j["permissions"] = perms.to_json();
        j["budget"] = budget.to_json();
        res.set_content(j.dump(), "application/json");
    });

    // PUT /api/agents/:id/permissions
    svr.Put(R"(/api/agents/(\d+)/permissions)", [this](const httplib::Request& req, httplib::Response& res) {
        uint32_t agent_id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        try {
            auto body = json::parse(req.body);
            if (body.contains("permissions")) {
                auto perms = AgentPermissions::from_json(body["permissions"]);
                ctx_.permissions_store.set_permissions(agent_id, perms);
            }
            if (body.contains("budget")) {
                auto budget = AgentBudget::from_json(body["budget"]);
                ctx_.permissions_store.set_budget(agent_id, budget);
            }
            ctx_.audit_logger.log(AuditCategory::SECURITY, "PERMISSIONS_UPDATED",
                agent_id, "", {{"body", body}});
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j; j["error"] = std::string("invalid: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // GET /api/sandbox/overview — all agents, permissions, budgets, activity
    svr.Get("/api/sandbox/overview", [this](const httplib::Request&, httplib::Response& res) {
        json agents_arr = json::array();

        // Get OpenClaw instances
        if (ctx_.openclaw) {
            auto instances = ctx_.openclaw->list();
            for (const auto* inst : instances) {
                auto& perms = ctx_.permissions_store.get_or_create(0);
                auto& budget = ctx_.permissions_store.get_or_create_budget(0);
                json a;
                a["id"] = inst->id;
                a["name"] = inst->name;
                a["type"] = "openclaw";
                a["state"] = inst->state;
                a["pid"] = inst->sandbox ? inst->sandbox->pid() : -1;
                a["budget_usd"] = inst->config.budget_usd;
                a["cost_usd"] = inst->cost_usd;
                a["channels"] = inst->config.channels;
                a["started_at_ms"] = inst->started_at_ms;
                a["permissions"] = perms.to_json();
                a["budget_tracking"] = budget.to_json();
                agents_arr.push_back(a);
            }
        }

        // Get recent audit for activity feed
        auto audit = ctx_.audit_logger.get_entries(nullptr, nullptr, 0, 20);
        json activity = json::array();
        for (const auto& e : audit) {
            activity.push_back(e.to_json());
        }

        // Cost
        json cost;
        cost["total_usd"] = ctx_.inference_gateway.get_config().current_cost_usd;

        json resp;
        resp["agents"] = agents_arr;
        resp["activity"] = activity;
        resp["cost"] = cost;
        resp["memory_blocks"] = ctx_.memory_blocks ? static_cast<int>(ctx_.memory_blocks->list(0, 1000).size()) : 0;
        res.set_content(resp.dump(), "application/json");
    });

    // ===================================================================
    // IPC — Agent-to-Agent Messaging
    // ===================================================================

    // POST /api/agents/:id/message  {"to": 1, "content": "..."}
    svr.Post(R"(/api/agents/(\d+)/message)", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.mailbox) {
            res.status = 503;
            res.set_content(R"({"error":"mailbox not available"})", "application/json");
            return;
        }
        try {
            uint32_t from_id = static_cast<uint32_t>(std::stoul(req.matches[1]));
            auto body = json::parse(req.body);
            std::string content = body.value("content", "");

            if (content.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"content is required"})", "application/json");
                return;
            }

            bool ok = false;
            if (body.contains("to")) {
                if (body["to"].is_number()) {
                    ok = ctx_.mailbox->send(from_id, body["to"].get<uint32_t>(), content);
                } else if (body["to"].is_string()) {
                    ok = ctx_.mailbox->send_by_name(from_id, body["to"].get<std::string>(), content);
                }
            }

            if (ok) {
                res.set_content(R"({"success":true})", "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"recipient not found"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            json j; j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // GET /api/agents/:id/messages
    svr.Get(R"(/api/agents/(\d+)/messages)", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.mailbox) {
            res.set_content(R"({"messages":[]})", "application/json");
            return;
        }
        uint32_t agent_id = static_cast<uint32_t>(std::stoul(req.matches[1]));
        auto msgs = ctx_.mailbox->receive(agent_id, 100);
        json arr = json::array();
        for (const auto& m : msgs) {
            arr.push_back(json{
                {"from", m.from_agent_id},
                {"to", m.to_agent_id},
                {"content", m.content},
                {"timestamp_ms", m.timestamp_ms},
            });
        }
        json resp;
        resp["messages"] = arr;
        resp["count"] = arr.size();
        res.set_content(resp.dump(), "application/json");
    });

    // POST /api/broadcast  {"from": 0, "content": "..."}
    svr.Post("/api/broadcast", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.mailbox) {
            res.status = 503;
            res.set_content(R"({"error":"mailbox not available"})", "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            uint32_t from = body.value("from", static_cast<uint32_t>(0));
            std::string content = body.value("content", "");
            if (content.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"content is required"})", "application/json");
                return;
            }
            ctx_.mailbox->broadcast(from, content);
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j; j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // ===================================================================
    // Scheduling — cron-based recurring runs
    // ===================================================================

    // In-memory schedule store (persisted via state store)
    // POST /api/schedules  {"name": "...", "cron": "...", "run": {...}}
    svr.Post("/api/schedules", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string name = body.value("name", "");
            std::string cron = body.value("cron", "");
            auto run_cfg = body.value("run", json::object());

            if (name.empty() || cron.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name and cron are required"})", "application/json");
                return;
            }

            // Store schedule in state store
            json schedule;
            schedule["name"] = name;
            schedule["cron"] = cron;
            schedule["run"] = run_cfg;
            schedule["enabled"] = true;
            schedule["created_at_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            ctx_.state_store.store("schedule:" + name, schedule, 0, "global");

            ctx_.audit_logger.log(AuditCategory::RESOURCE, "SCHEDULE_CREATED", 0, "", {
                {"name", name}, {"cron", cron},
            });

            res.status = 201;
            res.set_content(schedule.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j; j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // GET /api/schedules
    svr.Get("/api/schedules", [this](const httplib::Request&, httplib::Response& res) {
        auto all_keys = ctx_.state_store.keys("", 0);
        json arr = json::array();
        for (const auto& key : all_keys) {
            if (key.substr(0, 9) == "schedule:") {
                auto val = ctx_.state_store.fetch(key, 0);
                if (val) {
                    try { arr.push_back(*val); } catch (...) {}
                }
            }
        }
        json resp;
        resp["schedules"] = arr;
        resp["count"] = arr.size();
        res.set_content(resp.dump(), "application/json");
    });

    // DELETE /api/schedules/:name
    svr.Delete(R"(/api/schedules/([a-zA-Z0-9_-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        bool removed = ctx_.state_store.erase("schedule:" + name, 0);
        if (removed) {
            ctx_.audit_logger.log(AuditCategory::RESOURCE, "SCHEDULE_DELETED", 0, "", {{"name", name}});
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"schedule not found"})", "application/json");
        }
    });

    // ===================================================================
    // Webhooks — notify external services on events
    // ===================================================================

    // POST /api/webhooks  {"url": "...", "events": ["run_complete", ...]}
    svr.Post("/api/webhooks", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string url = body.value("url", "");
            auto events = body.value("events", json::array());

            if (url.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"url is required"})", "application/json");
                return;
            }

            json webhook;
            webhook["url"] = url;
            webhook["events"] = events;
            webhook["enabled"] = true;
            webhook["created_at_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // Generate webhook ID
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::string id = "wh_" + std::to_string(ms);
            webhook["id"] = id;

            ctx_.state_store.store("webhook:" + id, webhook, 0, "global");

            ctx_.audit_logger.log(AuditCategory::RESOURCE, "WEBHOOK_CREATED", 0, "", {
                {"id", id}, {"url", url}, {"events", events},
            });

            res.status = 201;
            res.set_content(webhook.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j; j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // GET /api/webhooks
    svr.Get("/api/webhooks", [this](const httplib::Request&, httplib::Response& res) {
        auto all_keys = ctx_.state_store.keys("", 0);
        json arr = json::array();
        for (const auto& key : all_keys) {
            if (key.substr(0, 8) == "webhook:") {
                auto val = ctx_.state_store.fetch(key, 0);
                if (val) {
                    try { arr.push_back(*val); } catch (...) {}
                }
            }
        }
        json resp;
        resp["webhooks"] = arr;
        resp["count"] = arr.size();
        res.set_content(resp.dump(), "application/json");
    });

    // DELETE /api/webhooks/:id
    svr.Delete(R"(/api/webhooks/(wh_[0-9]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        bool removed = ctx_.state_store.erase("webhook:" + id, 0);
        if (removed) {
            ctx_.audit_logger.log(AuditCategory::RESOURCE, "WEBHOOK_DELETED", 0, "", {{"id", id}});
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"webhook not found"})", "application/json");
        }
    });

    // ===================================================================
    // LLM Provider Management — store API keys per provider
    // ===================================================================

    // GET /api/providers — list configured providers (keys masked)
    svr.Get("/api/providers", [this](const httplib::Request&, httplib::Response& res) {
        auto keys = ctx_.state_store.keys("provider:", 0);
        json providers = json::array();
        for (const auto& key : keys) {
            auto val = ctx_.state_store.fetch(key, 0);
            if (!val.has_value()) continue;
            auto p = val.value();
            std::string name = key.substr(9); // strip "provider:"
            std::string api_key = p.value("api_key", "");
            std::string masked = api_key.size() > 8 ? api_key.substr(0, 4) + "..." + api_key.substr(api_key.size() - 4) : "****";
            providers.push_back({
                {"name", name},
                {"connected", !api_key.empty()},
                {"key_masked", masked},
                {"base_url", p.value("base_url", "")},
                {"models", p.value("models", json::array())},
            });
        }
        // Always include openrouter if configured via kernel config
        if (ctx_.openrouter && ctx_.openrouter->is_configured()) {
            bool found = false;
            for (const auto& p : providers) { if (p.value("name", "") == "openrouter") found = true; }
            if (!found) {
                providers.push_back({{"name", "openrouter"}, {"connected", true}, {"key_masked", "from config"}, {"base_url", "https://openrouter.ai/api/v1"}, {"models", json::array()}});
            }
        }
        res.set_content(json({{"providers", providers}, {"count", providers.size()}}).dump(), "application/json");
    });

    // POST /api/providers — save provider key
    svr.Post("/api/providers", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string name = body.value("name", "");
            std::string api_key = body.value("api_key", "");
            std::string base_url = body.value("base_url", "");
            if (name.empty() || api_key.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"name and api_key required"})", "application/json");
                return;
            }
            // Default base URLs per provider
            if (base_url.empty()) {
                if (name == "openai") base_url = "https://api.openai.com/v1";
                else if (name == "anthropic") base_url = "https://api.anthropic.com/v1";
                else if (name == "google") base_url = "https://generativelanguage.googleapis.com/v1";
                else if (name == "openrouter") base_url = "https://openrouter.ai/api/v1";
            }
            ctx_.state_store.store("provider:" + name, json({{"api_key", api_key}, {"base_url", base_url}, {"name", name}}), 0);
            ctx_.audit_logger.log(AuditCategory::RESOURCE, "PROVIDER_CONFIGURED", 0, "", {{"provider", name}});
            res.status = 201;
            res.set_content(json({{"name", name}, {"connected", true}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // DELETE /api/providers/:name
    svr.Delete(R"(/api/providers/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string name = req.matches[1];
        ctx_.state_store.erase("provider:" + name, 0);
        res.set_content(R"({"success":true})", "application/json");
    });

    // ===================================================================
    // Agent Runtimes — spawn Claude Code, Codex, OpenClaw as agent processes
    // ===================================================================

    // POST /api/runtimes/claude-code/run — spawn Claude Code on a task
    svr.Post("/api/runtimes/claude-code/run", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"invalid JSON"})", "application/json"); return;
        }

        std::string goal = body.value("goal", "");
        std::string model = body.value("model", "claude-sonnet-4");
        std::string working_dir = body.value("working_dir", ".");
        int timeout_s = body.value("timeout_s", 300);
        auto allowed_tools = body.value("allowed_tools", std::vector<std::string>{"Read", "Edit", "Bash"});

        if (goal.empty()) {
            res.status = 400; res.set_content(R"({"error":"goal required"})", "application/json"); return;
        }

        // Build command
        std::string tools_arg;
        for (const auto& t : allowed_tools) { if (!tools_arg.empty()) tools_arg += ","; tools_arg += t; }

        std::string cmd = "claude -p \"" + goal + "\" --output-format json";
        if (!tools_arg.empty()) cmd += " --allowedTools \"" + tools_arg + "\"";

        ctx_.audit_logger.log(AuditCategory::RESOURCE, "RUNTIME_SPAWN", 0, "", {{"runtime", "claude-code"}, {"goal", goal.substr(0, 100)}});

        // Execute via popen with timeout
        std::string output;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            res.status = 500;
            res.set_content(json({{"error","failed to spawn claude-code. Is it installed? npm i -g @anthropic-ai/claude-code"}}).dump(), "application/json");
            return;
        }
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) { output += buf; if (output.size() > 500000) break; }
        int exit_code = pclose(pipe);

        json result;
        result["runtime"] = "claude-code";
        result["success"] = (exit_code == 0);
        result["exit_code"] = exit_code;
        result["content"] = output.substr(0, 50000);
        result["goal"] = goal;
        result["model"] = model;
        res.set_content(result.dump(), "application/json");
    });

    // POST /api/runtimes/codex/run — spawn Codex on a task
    svr.Post("/api/runtimes/codex/run", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"invalid JSON"})", "application/json"); return;
        }

        std::string goal = body.value("goal", "");
        std::string model = body.value("model", "");
        int timeout_s = body.value("timeout_s", 300);

        if (goal.empty()) {
            res.status = 400; res.set_content(R"({"error":"goal required"})", "application/json"); return;
        }

        std::string cmd = "codex exec \"" + goal + "\" --json --approval-mode full-auto";
        if (!model.empty()) cmd += " --model " + model;

        ctx_.audit_logger.log(AuditCategory::RESOURCE, "RUNTIME_SPAWN", 0, "", {{"runtime", "codex"}, {"goal", goal.substr(0, 100)}});

        std::string output;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            res.status = 500;
            res.set_content(json({{"error","failed to spawn codex. Is it installed? npm i -g @openai/codex"}}).dump(), "application/json");
            return;
        }
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) { output += buf; if (output.size() > 500000) break; }
        int exit_code = pclose(pipe);

        json result;
        result["runtime"] = "codex";
        result["success"] = (exit_code == 0);
        result["exit_code"] = exit_code;
        result["content"] = output.substr(0, 50000);
        result["goal"] = goal;
        res.set_content(result.dump(), "application/json");
    });

    // GET /api/runtimes — list available runtimes
    svr.Get("/api/runtimes", [this](const httplib::Request&, httplib::Response& res) {
        json runtimes = json::array();

        // CLOVE (always available)
        runtimes.push_back({{"name", "clove"}, {"type", "built-in"}, {"status", "available"}, {"description", "CLOVE kernel RunEngine with 86 syscalls"}});

        // Claude Code (check if installed)
        int cc = system("which claude > /dev/null 2>&1");
        runtimes.push_back({{"name", "claude-code"}, {"type", "subprocess"}, {"status", cc == 0 ? "available" : "not-installed"},
            {"description", "Anthropic Claude Code — autonomous coding agent"}, {"install", "npm i -g @anthropic-ai/claude-code"}});

        // Codex (check if installed)
        int cx = system("which codex > /dev/null 2>&1");
        runtimes.push_back({{"name", "codex"}, {"type", "subprocess"}, {"status", cx == 0 ? "available" : "not-installed"},
            {"description", "OpenAI Codex — coding agent with sandboxed execution"}, {"install", "npm i -g @openai/codex"}});

        // OpenClaw
        bool oc = ctx_.openclaw != nullptr;
        runtimes.push_back({{"name", "openclaw"}, {"type", "subprocess"}, {"status", oc ? "available" : "not-configured"},
            {"description", "OpenClaw — conversational agent with channel integrations"}});

        res.set_content(json({{"runtimes", runtimes}}).dump(), "application/json");
    });

    // ===================================================================
    // World Launch — run a multi-agent world from inline config
    // POST /api/worlds/launch  {"goal": "...", "world": "name", "agents": [{name, role, tools, budget}]}
    // ===================================================================
    svr.Post("/api/worlds/launch", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openrouter || !ctx_.openrouter->is_configured()) {
            res.status = 503;
            res.set_content(R"({"error":"LLM not configured"})", "application/json");
            return;
        }

        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"invalid JSON"})", "application/json"); return;
        }

        std::string goal = body.value("goal", "");
        std::string world_name = body.value("world", "world-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 100000));
        json agent_configs = body.value("agents", json::array());
        double total_budget = body.value("budget", 2.0);

        if (goal.empty() || agent_configs.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"goal and agents array are required"})", "application/json");
            return;
        }

        // Create world
        uint32_t world_id = 0;
        if (ctx_.world_engine) {
            world_id = ctx_.world_engine->create(world_name, {{"goal", goal}, {"agent_count", agent_configs.size()}});
        }

        // Run each agent synchronously and collect results
        int agent_count = static_cast<int>(agent_configs.size());
        double per_budget = total_budget / std::max(agent_count, 1);
        json agent_results = json::array();
        double total_cost = 0;
        int total_tokens = 0;
        int total_steps = 0;
        bool all_success = true;

        for (int i = 0; i < agent_count; i++) {
            auto& ac = agent_configs[i];
            std::string agent_name = ac.value("name", "agent-" + std::to_string(i + 1));
            std::string role = ac.value("role", "");
            double budget = ac.value("budget", per_budget);
            int max_steps = ac.value("max_steps", 10);
            auto tools = ac.value("tools", std::vector<std::string>{});
            std::string model = ac.value("model", ctx_.config.llm_model);

            std::string agent_goal = goal;
            if (!role.empty()) agent_goal = "YOUR ROLE: " + role + "\n\nMISSION: " + goal;

            RunConfig cfg;
            cfg.goal = agent_goal;
            cfg.model = model;
            cfg.budget_usd = budget;
            cfg.max_steps = max_steps;
            cfg.allowed_tools = tools;
            cfg.agent_name = agent_name;

            RunEngine engine(*ctx_.openrouter, ctx_.inference_gateway, ctx_.privacy_filter,
                             ctx_.audit_logger, ctx_.state_store, ctx_.permissions_store,
                             ctx_.artifact_store, ctx_.chain_store, ctx_.memory_blocks,
                             ctx_.mcp_bridge, ctx_.assembler, ctx_.config);

            auto result = engine.execute(cfg);

            total_cost += result.total_cost_usd;
            total_tokens += result.total_tokens;
            total_steps += result.steps;
            if (!result.success) all_success = false;

            agent_results.push_back({
                {"agent", agent_name},
                {"role", role},
                {"success", result.success},
                {"content", result.content.substr(0, 2000)},
                {"cost_usd", result.total_cost_usd},
                {"steps", result.steps},
            });
        }

        // Synthesize if multiple agents
        std::string synthesis;
        if (agent_count > 1) {
            std::string synth_prompt = "Synthesize these " + std::to_string(agent_count) + " agent outputs into one coherent result:\n\n";
            for (const auto& ar : agent_results) {
                synth_prompt += "--- " + ar.value("agent", "") + " (" + ar.value("role", "") + ") ---\n";
                synth_prompt += ar.value("content", "") + "\n\n";
            }
            auto synth_resp = ctx_.openrouter->chat(ctx_.config.llm_model, synth_prompt);
            synthesis = synth_resp.content;
            total_cost += synth_resp.usage.cost_usd;
            total_tokens += synth_resp.usage.total_tokens;
            ctx_.inference_gateway.record_cost(synth_resp.usage.cost_usd);
        } else if (agent_count == 1) {
            synthesis = agent_results[0].value("content", "");
        }

        // Destroy world if auto_destroy
        if (ctx_.world_engine && body.value("auto_destroy", true)) {
            ctx_.world_engine->destroy(world_id);
        }

        json resp;
        resp["success"] = all_success;
        resp["world"] = world_name;
        resp["world_id"] = world_id;
        resp["agent_count"] = agent_count;
        resp["total_cost_usd"] = total_cost;
        resp["total_tokens"] = total_tokens;
        resp["total_steps"] = total_steps;
        resp["synthesis"] = synthesis.substr(0, 3000);
        resp["agents"] = agent_results;
        res.set_content(resp.dump(), "application/json");
    });

    // ===================================================================
    // Inbound Webhook — receive events from GitHub, Slack, PagerDuty, etc.
    // Matches events to agent definitions with webhook triggers and runs them.
    // ===================================================================
    svr.Post("/api/events/ingest", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); } catch (...) { body = json::object(); }

        std::string source = req.get_header_value("X-GitHub-Event").empty()
            ? (req.get_header_value("X-Event-Source").empty() ? body.value("source", "unknown") : req.get_header_value("X-Event-Source"))
            : "github";
        std::string event_type = req.get_header_value("X-GitHub-Event").empty()
            ? body.value("type", "generic")
            : req.get_header_value("X-GitHub-Event");

        // Log the inbound event
        ctx_.audit_logger.log(AuditCategory::RESOURCE, "WEBHOOK_RECEIVED", 0, "",
            {{"source", source}, {"event_type", event_type}});

        // Find matching agent definitions
        auto keys = ctx_.state_store.keys("agent-def:", 0);
        json triggered = json::array();

        for (const auto& key : keys) {
            auto val = ctx_.state_store.fetch(key, 0);
            if (!val.has_value()) continue;
            auto def = val.value();
            if (!def.value("enabled", false)) continue;

            auto triggers = def.value("triggers", json::array());
            for (const auto& trigger : triggers) {
                if (trigger.value("type", "") != "webhook") continue;
                std::string trig_source = trigger.value("source", "");
                if (!trig_source.empty() && trig_source != source) continue;

                // Match — run the agent
                auto action = def.value("action", json::object());
                std::string goal = action.value("goal", "");
                // Replace {{event}} with the webhook payload
                auto event_str = body.dump();
                size_t pos;
                while ((pos = goal.find("{{event}}")) != std::string::npos) {
                    goal.replace(pos, 9, event_str.substr(0, 2000));
                }
                while ((pos = goal.find("{{event.source}}")) != std::string::npos) {
                    goal.replace(pos, 16, source);
                }
                while ((pos = goal.find("{{event.type}}")) != std::string::npos) {
                    goal.replace(pos, 14, event_type);
                }

                auto budget_obj = def.value("budget", json::object());
                double budget = budget_obj.value("per_run", 0.5);
                int max_steps = action.value("max_steps", 10);
                auto tools = action.value("tools", std::vector<std::string>{});

                RunConfig cfg;
                cfg.goal = goal;
                cfg.budget_usd = budget;
                cfg.max_steps = max_steps;
                cfg.allowed_tools = tools;
                cfg.agent_name = def.value("name", "webhook-agent");
                cfg.model = action.value("model", ctx_.config.llm_model);

                // Run asynchronously (don't block the webhook response)
                auto* openrouter = ctx_.openrouter;
                auto* gateway = &ctx_.inference_gateway;
                auto* privacy = &ctx_.privacy_filter;
                auto* audit = &ctx_.audit_logger;
                auto* state = &ctx_.state_store;
                auto* perms = &ctx_.permissions_store;
                auto* artifacts = ctx_.artifact_store;
                auto* chains = ctx_.chain_store;
                auto* memory = ctx_.memory_blocks;
                auto* mcp = ctx_.mcp_bridge;
                auto* assembler = ctx_.assembler;
                auto* config = &ctx_.config;

                std::thread([=]() {
                    RunEngine engine(*openrouter, *gateway, *privacy, *audit,
                                     *state, *perms, artifacts, chains, memory, mcp, assembler, *config);
                    engine.execute(cfg);
                }).detach();

                triggered.push_back({
                    {"agent", def.value("name", "")},
                    {"trigger_source", source},
                    {"trigger_type", event_type},
                });
                break; // Only trigger once per agent
            }
        }

        json resp;
        resp["received"] = true;
        resp["source"] = source;
        resp["event_type"] = event_type;
        resp["agents_triggered"] = triggered.size();
        resp["triggered"] = triggered;
        res.set_content(resp.dump(), "application/json");
    });

    // ===================================================================
    // OpenClaw Module — spawn and manage OpenClaw instances
    // ===================================================================

    // POST /api/openclaw/spawn
    svr.Post("/api/openclaw/spawn", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openclaw) {
            res.status = 503;
            res.set_content(R"({"error":"OpenClaw module not available"})", "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            OpenClawInstanceConfig cfg;
            cfg.name = body.value("name", "openclaw");
            cfg.soul = body.value("soul", "");
            cfg.model = body.value("model", "");
            cfg.budget_usd = body.value("budget_usd", 10.0);
            cfg.sandbox_memory_mb = body.value("memory_mb", 512);
            cfg.sandbox_cpu_percent = body.value("cpu_percent", 50);

            if (body.contains("channels")) {
                for (const auto& ch : body["channels"]) cfg.channels.push_back(ch.get<std::string>());
            }
            if (body.contains("skills")) {
                for (const auto& s : body["skills"]) cfg.skills.push_back(s.get<std::string>());
            }
            if (body.contains("allowed_domains")) {
                for (const auto& d : body["allowed_domains"]) cfg.allowed_domains.push_back(d.get<std::string>());
            }
            if (body.contains("allowed_read_paths")) {
                for (const auto& p : body["allowed_read_paths"]) cfg.allowed_read_paths.push_back(p.get<std::string>());
            }
            if (body.contains("allowed_write_paths")) {
                for (const auto& p : body["allowed_write_paths"]) cfg.allowed_write_paths.push_back(p.get<std::string>());
            }

            std::string id = ctx_.openclaw->spawn(cfg);
            if (id.empty()) {
                res.status = 500;
                res.set_content(R"({"error":"Failed to spawn OpenClaw instance. Check logs."})", "application/json");
                return;
            }

            const auto* inst = ctx_.openclaw->get(id);
            json j;
            j["id"] = id;
            j["name"] = cfg.name;
            j["state"] = inst ? inst->state : "unknown";
            j["port"] = inst ? inst->api_port : 0;
            j["budget_usd"] = cfg.budget_usd;
            j["sandbox"] = "seatbelt";
            res.status = 201;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // GET /api/openclaw/status
    svr.Get("/api/openclaw/status", [this](const httplib::Request&, httplib::Response& res) {
        if (!ctx_.openclaw) {
            res.set_content(R"({"instances":[],"count":0})", "application/json");
            return;
        }
        auto instances = ctx_.openclaw->list();
        json arr = json::array();
        for (const auto* inst : instances) {
            json j;
            j["id"] = inst->id;
            j["name"] = inst->name;
            j["state"] = inst->state;
            j["port"] = inst->api_port;
            j["budget_usd"] = inst->config.budget_usd;
            j["cost_usd"] = inst->cost_usd;
            j["pid"] = inst->sandbox ? inst->sandbox->pid() : -1;
            j["started_at_ms"] = inst->started_at_ms;
            j["channels"] = inst->config.channels;
            arr.push_back(j);
        }
        json resp;
        resp["instances"] = arr;
        resp["count"] = instances.size();
        res.set_content(resp.dump(), "application/json");
    });

    // POST /api/openclaw/:id/stop
    svr.Post(R"(/api/openclaw/([a-z0-9_]+)/stop)", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openclaw) {
            res.status = 503;
            res.set_content(R"({"error":"OpenClaw module not available"})", "application/json");
            return;
        }
        std::string id = req.matches[1];
        if (ctx_.openclaw->stop(id)) {
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"instance not found"})", "application/json");
        }
    });

    // POST /api/openclaw/fleet — spawn multiple instances
    svr.Post("/api/openclaw/fleet", [this](const httplib::Request& req, httplib::Response& res) {
        if (!ctx_.openclaw) {
            res.status = 503;
            res.set_content(R"({"error":"OpenClaw module not available"})", "application/json");
            return;
        }
        try {
            auto body = json::parse(req.body);
            auto agents_arr = body.value("agents", json::array());

            std::vector<OpenClawInstanceConfig> configs;
            for (const auto& a : agents_arr) {
                OpenClawInstanceConfig cfg;
                cfg.name = a.value("name", "agent-" + std::to_string(configs.size() + 1));
                cfg.soul = a.value("soul", "");
                cfg.model = a.value("model", "");
                cfg.budget_usd = a.value("budget_usd", 5.0);
                if (a.contains("channels")) {
                    for (const auto& ch : a["channels"]) cfg.channels.push_back(ch.get<std::string>());
                }
                if (a.contains("skills")) {
                    for (const auto& s : a["skills"]) cfg.skills.push_back(s.get<std::string>());
                }
                if (a.contains("can")) {
                    for (const auto& c : a["can"]) {
                        std::string perm = c.get<std::string>();
                        if (perm == "http") cfg.allowed_domains = {"*"};
                        if (perm == "read") cfg.allowed_read_paths = {"/tmp", "/Users"};
                        if (perm == "write") cfg.allowed_write_paths = {"/tmp"};
                    }
                }
                configs.push_back(cfg);
            }

            auto ids = ctx_.openclaw->spawn_fleet(configs);

            json resp;
            resp["count"] = ids.size();
            json arr = json::array();
            for (size_t i = 0; i < ids.size(); ++i) {
                json j;
                j["id"] = ids[i];
                j["name"] = configs[i].name;
                j["success"] = !ids[i].empty();
                arr.push_back(j);
            }
            resp["instances"] = arr;
            res.status = 201;
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            json j;
            j["error"] = std::string("invalid request: ") + e.what();
            res.set_content(j.dump(), "application/json");
        }
    });

    // POST /api/openclaw/stop-all
    svr.Post("/api/openclaw/stop-all", [this](const httplib::Request&, httplib::Response& res) {
        if (!ctx_.openclaw) {
            res.status = 503;
            res.set_content(R"({"error":"OpenClaw module not available"})", "application/json");
            return;
        }
        ctx_.openclaw->stop_all();
        res.set_content(R"({"success":true})", "application/json");
    });
}

} // namespace clove
