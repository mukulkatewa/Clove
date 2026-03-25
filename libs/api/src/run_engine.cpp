#include <clove/run_engine.hpp>
#include <clove/openrouter.hpp>
#include <clove/inference_gateway.hpp>
#include <clove/privacy_filter.hpp>
#include <clove/audit_log.hpp>
#include <clove/state_store.hpp>
#include <clove/artifact_store.hpp>
#include <clove/chain_store.hpp>
#include <clove/memory_block_store.hpp>
#include <clove/memory_block.hpp>
#include <clove/mcp_bridge.hpp>
#include <clove/context_assembler.hpp>
#include <clove/permissions_store.hpp>
#include <clove/permissions.hpp>
#include <curl/curl.h>

#include <fstream>
#include <array>
#include <cstdio>
#include <sys/wait.h>
#include <algorithm>
#include <sstream>

namespace clove {

using json = nlohmann::json;

// ── CURL write callback ─────────────────────────────────────────
static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// ── Constructor ─────────────────────────────────────────────────

RunEngine::RunEngine(
    OpenRouterClient& openrouter,
    InferenceGateway& gateway,
    PrivacyFilter& privacy,
    AuditLogger& audit,
    StateStore& state,
    PermissionsStore& permissions,
    ArtifactStore* artifacts,
    ChainStore* chains,
    MemoryBlockStore* memory,
    McpBridge* mcp,
    ContextAssembler* assembler,
    const KernelConfig& config)
    : openrouter_(openrouter)
    , gateway_(gateway)
    , privacy_(privacy)
    , audit_(audit)
    , state_(state)
    , permissions_(permissions)
    , artifacts_(artifacts)
    , chains_(chains)
    , memory_(memory)
    , mcp_(mcp)
    , assembler_(assembler)
    , config_(config)
{}

void RunEngine::emit(EventCallback& cb, const std::string& type, const json& data) {
    if (cb) {
        RunEvent ev;
        ev.type = type;
        ev.data = data;
        cb(ev);
    }
}

// ── Real tool implementations ───────────────────────────────────

std::string RunEngine::tool_read_file(const std::string& path) {
    // Permission check
    auto& perms = permissions_.get_or_create(agent_id_);
    if (!perms.can_read) {
        audit_.log(AuditCategory::SECURITY, "READ_DENIED", agent_id_, "", {{"path", path}}, false);
        return "[error] read permission denied";
    }
    if (!perms.can_read_path(path)) {
        audit_.log(AuditCategory::SECURITY, "READ_PATH_DENIED", agent_id_, "", {{"path", path}}, false);
        return "[error] path not allowed: " + path;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "[error] file not found: " + path;
    }
    std::string content(1024 * 1024, '\0');  // 1MB max
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<size_t>(file.gcount()));

    audit_.log(AuditCategory::SYSCALL, "FILE_READ", agent_id_, "", {{"path", path}, {"bytes", content.size()}});
    return content;
}

std::string RunEngine::tool_write_file(const std::string& path, const std::string& content) {
    // Permission check
    auto& perms = permissions_.get_or_create(agent_id_);
    if (!perms.can_write) {
        audit_.log(AuditCategory::SECURITY, "WRITE_DENIED", agent_id_, "", {{"path", path}}, false);
        return "[error] write permission denied";
    }
    if (!perms.can_write_path(path)) {
        audit_.log(AuditCategory::SECURITY, "WRITE_PATH_DENIED", agent_id_, "", {{"path", path}}, false);
        return "[error] path not allowed: " + path;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return "[error] cannot write: " + path;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.close();

    audit_.log(AuditCategory::SYSCALL, "FILE_WRITE", agent_id_, "", {{"path", path}, {"bytes", content.size()}});
    return "wrote " + std::to_string(content.size()) + " bytes to " + path;
}

std::string RunEngine::tool_exec(const std::string& command) {
    // Permission check
    auto& perms = permissions_.get_or_create(agent_id_);
    if (!perms.can_exec) {
        audit_.log(AuditCategory::SECURITY, "EXEC_DENIED", agent_id_, "", {{"command", command}}, false);
        return "[error] exec permission denied";
    }
    if (!perms.can_execute_command(command)) {
        audit_.log(AuditCategory::SECURITY, "EXEC_CMD_DENIED", agent_id_, "", {{"command", command}}, false);
        return "[error] command not allowed";
    }

    audit_.log(AuditCategory::SYSCALL, "EXEC_START", agent_id_, "", {{"command", command}});

    std::string cmd_redirect = command + " 2>&1";
    FILE* pipe = popen(cmd_redirect.c_str(), "r");
    if (!pipe) {
        return "[error] failed to execute: " + command;
    }

    std::string output;
    std::array<char, 4096> buf;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        output += buf.data();
        if (output.size() > 512 * 1024) break;  // 512KB cap
    }
    int raw_status = pclose(pipe);
    int exit_code = WEXITSTATUS(raw_status);

    audit_.log(AuditCategory::SYSCALL, "EXEC_DONE", 0, "",
        {{"command", command}, {"exit_code", exit_code}, {"output_bytes", output.size()}});

    if (exit_code != 0) {
        return "[exit " + std::to_string(exit_code) + "] " + output;
    }
    return output;
}

std::string RunEngine::tool_http(const std::string& url, const std::string& method, const std::string& body) {
    // Permission check
    auto& perms = permissions_.get_or_create(agent_id_);
    if (!perms.can_http) {
        audit_.log(AuditCategory::NETWORK, "HTTP_DENIED", agent_id_, "", {{"url", url}}, false);
        return "[error] HTTP permission denied";
    }
    // Extract domain for allowlist check
    std::string domain;
    size_t start = url.find("://");
    if (start != std::string::npos) start += 3; else start = 0;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    domain = url.substr(start, end - start);
    size_t colon = domain.find(':');
    if (colon != std::string::npos) domain = domain.substr(0, colon);

    if (!perms.can_access_domain(domain)) {
        audit_.log(AuditCategory::NETWORK, "HTTP_DOMAIN_DENIED", agent_id_, "", {{"url", url}, {"domain", domain}}, false);
        return "[error] domain not allowed: " + domain;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return "[error] failed to init HTTP client";
    }

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    // User-Agent for web requests
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "CLOVE/2.0");

    if (method == "POST" || method == "PUT" || method == "PATCH") {
        if (method == "POST") curl_easy_setopt(curl, CURLOPT_POST, 1L);
        else curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_easy_cleanup(curl);

    audit_.log(AuditCategory::NETWORK, "HTTP_REQUEST", 0, "",
        {{"url", url}, {"method", method}, {"status", status}, {"bytes", response_body.size()}});

    if (res != CURLE_OK) {
        return "[error] HTTP failed: " + std::string(curl_easy_strerror(res));
    }

    // Cap response for LLM context
    if (response_body.size() > 8000) {
        response_body.resize(8000);
        response_body += "\n[truncated at 8KB]";
    }
    return response_body;
}

std::string RunEngine::tool_search(
    const std::string& query, const std::string& model,
    double& cost, int& tokens)
{
    // Real web search via DuckDuckGo
    std::string encoded_query = query;
    for (auto& c : encoded_query) {
        if (c == ' ') c = '+';
    }
    std::string url = "https://html.duckduckgo.com/html/?q=" + encoded_query;

    std::string raw_html = tool_http(url, "GET", "");

    // Extract useful info via LLM
    // Trim HTML to save tokens
    if (raw_html.size() > 6000) raw_html.resize(6000);

    auto extract_resp = openrouter_.chat(model,
        "Extract the key search results and information from this HTML for the query: " + query +
        "\n\nRaw HTML:\n" + raw_html +
        "\n\nProvide a concise summary of the top 5 results.");

    cost += extract_resp.usage.cost_usd;
    tokens += extract_resp.usage.total_tokens;
    gateway_.record_cost(extract_resp.usage.cost_usd);

    return extract_resp.content;
}

std::string RunEngine::tool_mcp_call(
    const std::string& server, const std::string& tool, const json& args)
{
    if (!mcp_) {
        return "[error] MCP bridge not enabled. Start kernel with --mcp";
    }
    auto result = mcp_->call_tool(0, server, tool, args);
    if (!result.success) {
        return "[error] MCP call failed: " + result.error;
    }
    return result.content.dump();
}

std::string RunEngine::tool_remember(const std::string& fact) {
    if (!memory_) {
        // Fallback to state store
        state_.store("memory:" + fact.substr(0, 50), fact, 0);
        return "remembered (via state store)";
    }
    // Create or append to agent memory block
    auto blocks = memory_->list(0, 100);
    std::string block_id;
    for (const auto& b : blocks) {
        if (b.name == "agent-memory" && b.type == MemoryBlockType::CORE) {
            block_id = b.id;
            break;
        }
    }
    if (block_id.empty()) {
        auto block = memory_->create(0, "agent-memory", MemoryBlockType::CORE,
                                      MemoryAccess::PRIVATE, "", 0);
        block_id = block.id;
    }
    memory_->append(block_id, "\n" + fact, 0);
    return "remembered: " + fact.substr(0, 100);
}

std::string RunEngine::tool_recall(const std::string& query) {
    if (!memory_) {
        // Fallback: search state store
        auto keys = state_.keys("memory:", 0);
        std::string results;
        for (const auto& k : keys) {
            auto val = state_.fetch(k, 0);
            if (val) results += *val + "\n";
        }
        return results.empty() ? "(no memories found)" : results;
    }
    // Read from memory block
    auto blocks = memory_->list(0, 100);
    for (const auto& b : blocks) {
        if (b.name == "agent-memory") {
            auto opt = memory_->get(b.id, 0);
            if (opt) return opt->content;
        }
    }
    return "(no memories found)";
}

// ── Tool dispatch ───────────────────────────────────────────────

json RunEngine::build_tools(const std::vector<std::string>& allowed) {
    json tools = json::array();

    auto add = [&](const std::string& name, const std::string& desc, const json& params) {
        if (!allowed.empty() &&
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            return;
        }
        tools.push_back({
            {"type", "function"},
            {"function", {
                {"name", name},
                {"description", desc},
                {"parameters", params}
            }}
        });
    };

    // ── Core tools (kernel syscalls) ──
    add("read_file", "Read a file from disk. Returns the file content.",
        {{"type", "object"}, {"properties", {
            {"path", {{"type", "string"}, {"description", "Absolute or relative file path"}}}
        }}, {"required", json::array({"path"})}});

    add("write_file", "Write content to a file on disk.",
        {{"type", "object"}, {"properties", {
            {"path", {{"type", "string"}, {"description", "File path to write to"}}},
            {"content", {{"type", "string"}, {"description", "Content to write"}}}
        }}, {"required", json::array({"path", "content"})}});

    add("exec", "Execute a shell command and return stdout/stderr.",
        {{"type", "object"}, {"properties", {
            {"command", {{"type", "string"}, {"description", "Shell command to execute"}}}
        }}, {"required", json::array({"command"})}});

    add("search", "Search the web for information. Returns summarized results.",
        {{"type", "object"}, {"properties", {
            {"query", {{"type", "string"}, {"description", "Search query"}}}
        }}, {"required", json::array({"query"})}});

    add("http", "Make an HTTP request to any URL.",
        {{"type", "object"}, {"properties", {
            {"url", {{"type", "string"}, {"description", "URL"}}},
            {"method", {{"type", "string"}, {"description", "HTTP method: GET, POST, PUT, DELETE (default GET)"}}},
            {"body", {{"type", "string"}, {"description", "Request body (for POST/PUT)"}}}
        }}, {"required", json::array({"url"})}});

    add("store", "Store a key-value pair for later retrieval.",
        {{"type", "object"}, {"properties", {
            {"key", {{"type", "string"}, {"description", "Storage key"}}},
            {"value", {{"type", "string"}, {"description", "Value to store"}}}
        }}, {"required", json::array({"key", "value"})}});

    add("fetch", "Retrieve a previously stored value by key.",
        {{"type", "object"}, {"properties", {
            {"key", {{"type", "string"}, {"description", "Storage key"}}}
        }}, {"required", json::array({"key"})}});

    add("remember", "Store a fact in persistent memory for future runs.",
        {{"type", "object"}, {"properties", {
            {"fact", {{"type", "string"}, {"description", "Fact to remember"}}}
        }}, {"required", json::array({"fact"})}});

    add("recall", "Retrieve relevant facts from persistent memory.",
        {{"type", "object"}, {"properties", {
            {"query", {{"type", "string"}, {"description", "What to recall"}}}
        }}, {"required", json::array({"query"})}});

    // ── MCP tools (dynamically discovered) ──
    if (mcp_) {
        auto mcp_tools = mcp_->list_tools();
        for (const auto& t : mcp_tools) {
            std::string full_name = "mcp_" + t.server_name + "_" + t.name;
            // Only add if allowed (or no filter)
            if (!allowed.empty() &&
                std::find(allowed.begin(), allowed.end(), full_name) == allowed.end() &&
                std::find(allowed.begin(), allowed.end(), "mcp") == allowed.end()) {
                continue;
            }
            tools.push_back({
                {"type", "function"},
                {"function", {
                    {"name", full_name},
                    {"description", t.description.empty() ? (t.server_name + "/" + t.name) : t.description},
                    {"parameters", t.input_schema.empty() ?
                        json({{"type", "object"}, {"properties", json::object()}}) : t.input_schema}
                }}
            });
        }
    }

    return tools;
}

std::string RunEngine::execute_tool(
    const std::string& name, const json& args,
    const std::string& model, double& cost, int& tokens)
{
    // ── Core tools ──
    if (name == "read_file") {
        return tool_read_file(args.value("path", ""));

    } else if (name == "write_file") {
        return tool_write_file(args.value("path", ""), args.value("content", ""));

    } else if (name == "exec") {
        return tool_exec(args.value("command", ""));

    } else if (name == "search") {
        return tool_search(args.value("query", ""), model, cost, tokens);

    } else if (name == "http") {
        return tool_http(args.value("url", ""), args.value("method", "GET"), args.value("body", ""));

    } else if (name == "store") {
        state_.store(args.value("key", ""), args.value("value", ""), 0);
        return "stored";

    } else if (name == "fetch") {
        auto val = state_.fetch(args.value("key", ""), 0);
        return val.value_or("(not found)");

    } else if (name == "remember") {
        return tool_remember(args.value("fact", ""));

    } else if (name == "recall") {
        return tool_recall(args.value("query", ""));
    }

    // ── MCP tools (mcp_<server>_<tool>) ──
    if (name.substr(0, 4) == "mcp_" && mcp_) {
        // Parse server and tool from name: mcp_github_create_issue
        auto rest = name.substr(4);
        auto sep = rest.find('_');
        if (sep != std::string::npos) {
            std::string server = rest.substr(0, sep);
            std::string tool = rest.substr(sep + 1);
            return tool_mcp_call(server, tool, args);
        }
    }

    return "[error] unknown tool: " + name;
}

// ── Main execution loop ─────────────────────────────────────────

RunResult RunEngine::execute(const RunConfig& cfg, EventCallback on_event) {
    RunResult result;
    result.step_log = json::array();

    // Assign agent ID for permission checks (0 = API caller, no restrictions by default)
    agent_id_ = 0;

    // Create chain
    std::string chain_id = cfg.chain_id;
    if (chain_id.empty() && chains_) {
        auto chain = chains_->create(agent_id_, "run-" + cfg.agent_name, cfg.goal.substr(0, 200));
        chain_id = chain.id;
    }
    result.chain_id = chain_id;

    // Build tools (filtered by what's allowed)
    json tools_desc = build_tools(cfg.allowed_tools);

    // System prompt — optionally assembled from context layer
    std::string system_prompt;
    if (assembler_ && !chain_id.empty()) {
        // Use the research-backed context assembler:
        // - SYSTEM memory blocks pinned at top
        // - CORE memory blocks always included
        // - Chain artifacts included
        // - Observation masking compresses tool outputs
        // - Position-aware: critical info at boundaries
        AssemblyConfig ac;
        ac.max_tokens = 128000;
        auto assembled = assembler_->assemble(agent_id_, chain_id, ac);
        if (!assembled.context.empty()) {
            system_prompt = assembled.context + "\n\n---\n\n";
        }
    }

    system_prompt +=
        "You are an AI agent named '" + cfg.agent_name + "'. "
        "Complete the user's goal using the tools available to you. "
        "Call tools when you need to interact with the real world — read files, search the web, "
        "execute commands, query databases, etc. "
        "When you have the final answer, respond with text (no tool call). "
        "Be concise and thorough.";

    // Build messages
    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt}});

    // PII filter on goal
    std::string filtered_goal = cfg.goal;
    if (privacy_.is_enabled()) {
        auto pii = privacy_.redact(cfg.goal);
        filtered_goal = pii.cleaned_text;
    }
    messages.push_back({{"role", "user"}, {"content", filtered_goal}});

    emit(on_event, "start", {
        {"goal", cfg.goal},
        {"agent", cfg.agent_name},
        {"model", cfg.model.empty() ? config_.llm_model : cfg.model},
        {"budget_usd", cfg.budget_usd},
        {"chain_id", chain_id},
        {"tools", tools_desc.size()},
    });

    // ── Tool-calling loop ──
    std::string active_model = cfg.model.empty() ? config_.llm_model : cfg.model;

    while (result.steps < cfg.max_steps &&
           result.total_cost_usd < cfg.budget_usd &&
           !cancelled_)
    {
        result.steps++;

        emit(on_event, "thinking", {
            {"step", result.steps},
            {"cost_usd", result.total_cost_usd},
        });

        // Call LLM with tools
        json options;
        if (!tools_desc.empty()) {
            options["tools"] = tools_desc;
        }

        auto llm_resp = openrouter_.chat_messages(active_model, messages, options);
        result.total_cost_usd += llm_resp.usage.cost_usd;
        result.total_tokens += llm_resp.usage.total_tokens;
        gateway_.record_cost(llm_resp.usage.cost_usd);

        if (!llm_resp.success) {
            result.success = false;
            result.error = llm_resp.error;
            emit(on_event, "error", {{"error", llm_resp.error}, {"step", result.steps}});
            break;
        }

        // Parse response
        json raw;
        try {
            raw = json::parse(llm_resp.raw_json);
        } catch (...) {
            result.content = llm_resp.content;
            result.success = true;
            break;
        }

        auto choices = raw.value("choices", json::array());
        if (choices.empty()) {
            result.content = llm_resp.content;
            result.success = true;
            break;
        }

        auto choice = choices[0];
        auto message = choice.value("message", json::object());
        std::string finish_reason = choice.value("finish_reason", "");

        messages.push_back(message);

        auto tool_calls = message.value("tool_calls", json::array());

        if (tool_calls.empty() || finish_reason == "stop") {
            std::string content;
            if (message.contains("content") && message["content"].is_string()) {
                content = message["content"].get<std::string>();
            } else {
                content = llm_resp.content;
            }
            result.content = content;
            result.success = true;
            break;
        }

        // Execute tool calls
        for (const auto& tc : tool_calls) {
            if (cancelled_) break;

            std::string tool_id = tc.value("id", "");
            auto fn = tc.value("function", json::object());
            std::string tool_name = fn.value("name", "");
            json tool_args;
            try {
                tool_args = json::parse(fn.value("arguments", "{}"));
            } catch (...) {
                tool_args = json::object();
            }

            emit(on_event, "tool_call", {
                {"step", result.steps},
                {"tool", tool_name},
                {"args", tool_args},
                {"cost_usd", result.total_cost_usd},
            });

            std::string tool_result = execute_tool(
                tool_name, tool_args, active_model,
                result.total_cost_usd, result.total_tokens);

            // Store file write artifacts
            if (tool_name == "write_file" && artifacts_ && !chain_id.empty()) {
                artifacts_->create(0, ArtifactType::NOTE,
                    tool_args.value("path", "output"),
                    tool_args.value("content", ""),
                    chain_id);
            }

            result.step_log.push_back({
                {"step", result.steps},
                {"tool", tool_name},
                {"args", tool_args},
                {"result_preview", tool_result.substr(0, 200)},
                {"cost_usd", result.total_cost_usd},
            });

            emit(on_event, "tool_result", {
                {"step", result.steps},
                {"tool", tool_name},
                {"result_preview", tool_result.substr(0, 200)},
                {"cost_usd", result.total_cost_usd},
                {"tokens", result.total_tokens},
            });

            audit_.log(AuditCategory::SYSCALL, tool_name,
                0, cfg.agent_name, {{"args", tool_args}, {"step", result.steps}});

            messages.push_back({
                {"role", "tool"},
                {"tool_call_id", tool_id},
                {"content", tool_result}
            });
        }
    }

    // Budget check
    if (result.total_cost_usd >= cfg.budget_usd && result.error.empty()) {
        result.error = "budget exceeded";
    }
    if (cancelled_ && result.error.empty()) {
        result.error = "cancelled";
        result.success = false;
    }

    // Store final result as artifact
    if (artifacts_ && !chain_id.empty() && !result.content.empty()) {
        artifacts_->create(0, ArtifactType::REPORT,
            "Run Result: " + cfg.agent_name, result.content, chain_id);
    }

    // Audit
    audit_.log(AuditCategory::RESOURCE, "RUN_COMPLETE",
        0, cfg.agent_name, {
            {"goal", cfg.goal.substr(0, 200)},
            {"steps", result.steps},
            {"tokens", result.total_tokens},
            {"cost_usd", result.total_cost_usd},
            {"chain_id", chain_id},
            {"success", result.success},
        });

    emit(on_event, "done", {
        {"success", result.success},
        {"steps", result.steps},
        {"tokens", result.total_tokens},
        {"cost_usd", result.total_cost_usd},
        {"content_preview", result.content.substr(0, 200)},
        {"chain_id", chain_id},
    });

    return result;
}

} // namespace clove
