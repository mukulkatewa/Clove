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

#include "dashboard_html.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>

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
    return running_;
}

void ApiServer::stop() {
    if (!running_) return;
    impl_->stop();
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    running_ = false;
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
        j["syscall_count"] = 66;
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
}

} // namespace clove
