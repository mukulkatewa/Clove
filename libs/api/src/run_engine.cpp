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
            if (val) results += val->get<std::string>() + "\n";
        }
        return results.empty() ? "(no memories found)" : results;
    }

    // Use relevance-scored search (keyword overlap + recency + type priority)
    auto scored = memory_->search(query, agent_id_, 10);
    if (scored.empty()) {
        // Fallback: return all blocks if search returns nothing
        auto all = memory_->list(agent_id_, 100);
        if (all.empty()) return "(no memories found)";
        std::string result;
        for (const auto& b : all) {
            result += "[" + std::string(memory_block_type_to_string(b.type)) + "] " +
                      b.name + ":\n" + b.content + "\n\n";
        }
        return result;
    }

    std::string result;
    for (const auto& sb : scored) {
        result += "[" + std::string(memory_block_type_to_string(sb.block.type)) +
                  " | score:" + std::to_string(static_cast<int>(sb.score * 100)) + "%] " +
                  sb.block.name + ":\n" + sb.block.content + "\n\n";
    }
    return result;
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

    // ── Delegate tool (spawn sub-agent) ──
    add("delegate",
        "Delegate a sub-task to a new agent. The sub-agent runs independently with its own context, "
        "completes the goal, and returns the result. Use this when a task is complex enough to "
        "benefit from a dedicated agent with its own plan. Budget is deducted from your remaining budget.",
        {{"type", "object"}, {"properties", {
            {"goal", {{"type", "string"}, {"description", "What the sub-agent should accomplish"}}},
            {"tools", {{"type", "array"}, {"items", {{"type", "string"}}},
                {"description", "Tools the sub-agent can use (default: same as parent)"}}},
            {"budget", {{"type", "number"},
                {"description", "Max budget in USD for the sub-agent (default: 20% of parent's remaining)"}}},
            {"name", {{"type", "string"}, {"description", "Name for the sub-agent (default: sub-agent)"}}}
        }}, {"required", json::array({"goal"})}});

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

    } else if (name == "delegate") {
        return tool_delegate(args, model, cost, tokens);
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

// ── delegate: spawn a sub-agent to handle a sub-task ──
std::string RunEngine::tool_delegate(const json& args, const std::string& model, double& cost, int& tokens) {
    if (!active_cfg_) {
        return "[error] delegate not available outside a run";
    }

    // Depth check
    if (active_cfg_->current_depth >= active_cfg_->max_depth) {
        audit_.log(AuditCategory::SECURITY, "DELEGATE_DEPTH_EXCEEDED", agent_id_, "",
            {{"depth", active_cfg_->current_depth}, {"max", active_cfg_->max_depth}}, false);
        return "[error] max delegation depth (" + std::to_string(active_cfg_->max_depth) + ") reached";
    }

    std::string goal = args.value("goal", "");
    if (goal.empty()) {
        return "[error] goal is required";
    }

    // Budget: default to 20% of parent's remaining budget
    double parent_remaining = active_cfg_->budget_usd - cost;
    double sub_budget = args.value("budget", parent_remaining * 0.2);

    // Can't exceed parent's remaining budget
    if (sub_budget > parent_remaining) {
        sub_budget = parent_remaining;
    }
    if (sub_budget <= 0.0001) {
        return "[error] insufficient budget for sub-agent";
    }

    // Tools: inherit parent's or use specified
    std::vector<std::string> sub_tools;
    if (args.contains("tools") && args["tools"].is_array()) {
        for (const auto& t : args["tools"]) {
            sub_tools.push_back(t.get<std::string>());
        }
    } else {
        sub_tools = active_cfg_->allowed_tools;
    }

    // Can't give sub-agent tools the parent doesn't have
    if (!active_cfg_->allowed_tools.empty()) {
        std::vector<std::string> filtered;
        for (const auto& t : sub_tools) {
            if (std::find(active_cfg_->allowed_tools.begin(),
                          active_cfg_->allowed_tools.end(), t) != active_cfg_->allowed_tools.end()) {
                filtered.push_back(t);
            }
        }
        sub_tools = filtered;
    }

    std::string sub_name = args.value("name", "sub-" + std::to_string(active_cfg_->current_depth + 1));

    // Build sub-agent config
    RunConfig sub_cfg;
    sub_cfg.goal = goal;
    sub_cfg.model = model;
    sub_cfg.budget_usd = sub_budget;
    sub_cfg.max_steps = std::max(active_cfg_->max_steps / 2, 5); // half parent's steps
    sub_cfg.allowed_tools = sub_tools;
    sub_cfg.agent_name = sub_name;
    sub_cfg.max_depth = active_cfg_->max_depth;
    sub_cfg.current_depth = active_cfg_->current_depth + 1;

    audit_.log(AuditCategory::RESOURCE, "DELEGATE_START", agent_id_, active_cfg_->agent_name, {
        {"sub_agent", sub_name},
        {"goal", goal.substr(0, 200)},
        {"budget_usd", sub_budget},
        {"depth", sub_cfg.current_depth},
        {"tools", sub_tools},
    });

    // Execute sub-agent (synchronous — blocks until complete)
    auto sub_result = execute(sub_cfg, nullptr);

    // Deduct sub-agent cost from parent
    cost += sub_result.total_cost_usd;
    tokens += sub_result.total_tokens;

    audit_.log(AuditCategory::RESOURCE, "DELEGATE_COMPLETE", agent_id_, active_cfg_->agent_name, {
        {"sub_agent", sub_name},
        {"success", sub_result.success},
        {"cost_usd", sub_result.total_cost_usd},
        {"steps", sub_result.steps},
        {"depth", sub_cfg.current_depth},
    });

    if (sub_result.success) {
        return "[sub-agent '" + sub_name + "' completed (" +
               std::to_string(sub_result.steps) + " steps, $" +
               std::to_string(sub_result.total_cost_usd) + ")]\n\n" +
               sub_result.content;
    } else {
        return "[sub-agent '" + sub_name + "' failed: " + sub_result.error + "]\n" +
               sub_result.content;
    }
}

RunResult RunEngine::execute(const RunConfig& cfg, EventCallback on_event) {
    RunResult result;
    result.step_log = json::array();

    // Assign agent ID for permission checks (0 = API caller, no restrictions by default)
    agent_id_ = 0;
    active_cfg_ = &cfg;

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

    // Position-aware prompt structure (Lost in the Middle, Liu et al. 2023):
    // HIGH attention zone (beginning) → agent identity, rules, capabilities
    // LOW attention zone (middle)     → assembled context, memories (already added above)
    // HIGH attention zone (end)       → the actual goal (added as user message)
    system_prompt +=
        "You are '" + cfg.agent_name + "', an autonomous agent operating inside the CLOVE kernel.\n\n"
        "YOU ARE AN OPERATOR, NOT AN ASSISTANT.\n"
        "You don't just answer questions — you take action. You have a shell, a filesystem, "
        "HTTP access, persistent memory, and a key-value store. Use them.\n\n"
        "TOOLS:\n"
        "- exec: Run ANY shell command. Use it to explore (ls, find, cat, grep), install packages (pip, npm), "
        "run scripts, compile code, check processes, inspect logs. If a command fails, read the error and fix it.\n"
        "- read_file: Read file contents. Use it to understand code, configs, data files.\n"
        "- write_file: Write or overwrite files. Use it to produce output, save results, create scripts.\n"
        "- http: Make HTTP requests (GET/POST/PUT/DELETE). Use it to call APIs, fetch web pages, check endpoints.\n"
        "- search: Search the web for information.\n"
        "- store/fetch: Persistent key-value storage. Store intermediate results, share data with other agents.\n"
        "- remember/recall: Long-term memory. Remember important findings. Recall relevant past knowledge.\n"
        "- mcp_call: Call external tool servers (GitHub, Slack, databases) via MCP protocol.\n"
        "- delegate: Spawn a sub-agent for parallel or specialized subtasks.\n\n"
        "HOW TO WORK:\n"
        "1. EXPLORE first. Read files, list directories, check what exists before making assumptions.\n"
        "2. PLAN briefly (2-5 steps). State what you'll do.\n"
        "3. EXECUTE step by step. Use tool calls — don't describe what you would do, actually do it.\n"
        "4. VERIFY your work. After making changes, check they worked (read the file back, run tests, check output).\n"
        "5. RECOVER from errors. If a tool call fails, read the error message, diagnose the issue, and try a different approach. "
        "Don't give up after one failure.\n"
        "6. When done, provide your final answer as plain text. Be concise and factual.\n\n"
        "CONSTRAINTS:\n"
        "- Every action is audited. Every tool call is permission-gated.\n"
        "- You have a budget. Work efficiently — don't waste steps on unnecessary exploration.\n"
        "- If you need information, go get it. Don't ask the user — use your tools.";

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

        // Observation masking (JetBrains "Complexity Trap", NeurIPS 2025):
        // Compress tool outputs older than 2 steps. Don't summarize — just truncate.
        // Research shows this halves cost with equal or better quality.
        if (result.steps > 2) {
            int tool_msg_count = 0;
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                if ((*it).value("role", "") == "tool") {
                    tool_msg_count++;
                    if (tool_msg_count > 2) {
                        // Compress old tool outputs
                        std::string content = (*it).value("content", "");
                        if (content.size() > 200) {
                            std::string first_line = content.substr(0, content.find('\n'));
                            if (first_line.size() > 100) first_line = first_line.substr(0, 100);
                            (*it)["content"] = "[" + first_line + "... " +
                                std::to_string(content.size()) + " chars truncated]";
                        }
                    }
                }
            }
        }

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

    // ── Reflexion (Shinn et al. 2023): retry on failure with self-reflection ──
    // If the run failed AND we have budget remaining AND haven't been cancelled,
    // generate a reflection and retry once. This gives +11-20% success rate.
    if (!result.success && !cancelled_ &&
        result.total_cost_usd < cfg.budget_usd * 0.8 &&  // need 20% budget headroom
        !result.error.empty() && result.error != "budget exceeded")
    {
        emit(on_event, "reflecting", {
            {"attempt", 1},
            {"error", result.error},
        });

        // Generate reflection
        std::string reflection_prompt =
            "You attempted this task and encountered an error.\n\n"
            "Goal: " + cfg.goal + "\n"
            "Error: " + result.error + "\n"
            "Steps taken: " + std::to_string(result.steps) + "\n\n"
            "What went wrong and what should you do differently on your next attempt? "
            "Be specific and actionable.";

        auto reflection_resp = openrouter_.chat(active_model, reflection_prompt);
        result.total_cost_usd += reflection_resp.usage.cost_usd;
        result.total_tokens += reflection_resp.usage.total_tokens;

        if (reflection_resp.success && !reflection_resp.content.empty()) {
            audit_.log(AuditCategory::RESOURCE, "REFLEXION",
                agent_id_, cfg.agent_name, {
                    {"attempt", 1},
                    {"reflection", reflection_resp.content.substr(0, 500)},
                });

            // Retry: add reflection to messages and continue the loop
            messages.push_back({{"role", "user"}, {"content",
                "Your previous attempt failed. Here is your reflection on what went wrong:\n\n" +
                reflection_resp.content + "\n\nPlease try again with a different approach."}});

            result.error.clear();

            // Mini retry loop (up to max_steps/2 additional steps)
            int retry_steps = 0;
            int max_retry_steps = std::max(cfg.max_steps / 2, 3);

            while (retry_steps < max_retry_steps &&
                   result.total_cost_usd < cfg.budget_usd &&
                   !cancelled_)
            {
                retry_steps++;
                result.steps++;

                emit(on_event, "thinking", {
                    {"step", result.steps},
                    {"cost_usd", result.total_cost_usd},
                    {"retry", true},
                });

                // Observation masking on retry too
                if (result.steps > 2) {
                    int tool_msg_count = 0;
                    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                        if ((*it).value("role", "") == "tool") {
                            tool_msg_count++;
                            if (tool_msg_count > 2) {
                                std::string content = (*it).value("content", "");
                                if (content.size() > 200) {
                                    std::string first_line = content.substr(0, content.find('\n'));
                                    if (first_line.size() > 100) first_line = first_line.substr(0, 100);
                                    (*it)["content"] = "[" + first_line + "... " +
                                        std::to_string(content.size()) + " chars truncated]";
                                }
                            }
                        }
                    }
                }

                json options;
                if (!tools_desc.empty()) options["tools"] = tools_desc;

                auto llm_resp = openrouter_.chat_messages(active_model, messages, options);
                result.total_cost_usd += llm_resp.usage.cost_usd;
                result.total_tokens += llm_resp.usage.total_tokens;
                gateway_.record_cost(llm_resp.usage.cost_usd);

                if (!llm_resp.success) {
                    result.error = llm_resp.error;
                    break;
                }

                json raw;
                try { raw = json::parse(llm_resp.raw_json); } catch (...) {
                    result.content = llm_resp.content;
                    result.success = true;
                    break;
                }

                auto choices = raw.value("choices", json::array());
                if (choices.empty()) { result.content = llm_resp.content; result.success = true; break; }

                auto choice = choices[0];
                auto message = choice.value("message", json::object());
                messages.push_back(message);

                auto tool_calls = message.value("tool_calls", json::array());
                if (tool_calls.empty() || choice.value("finish_reason", "") == "stop") {
                    if (message.contains("content") && message["content"].is_string()) {
                        result.content = message["content"].get<std::string>();
                    } else {
                        result.content = llm_resp.content;
                    }
                    result.success = true;
                    break;
                }

                for (const auto& tc : tool_calls) {
                    if (cancelled_) break;
                    std::string tool_id = tc.value("id", "");
                    auto fn = tc.value("function", json::object());
                    std::string tool_name = fn.value("name", "");
                    json tool_args;
                    try { tool_args = json::parse(fn.value("arguments", "{}")); } catch (...) { tool_args = json::object(); }

                    std::string tool_result = execute_tool(tool_name, tool_args, active_model,
                        result.total_cost_usd, result.total_tokens);

                    result.step_log.push_back({
                        {"step", result.steps}, {"tool", tool_name},
                        {"args", tool_args}, {"result_preview", tool_result.substr(0, 200)},
                        {"retry", true},
                    });

                    messages.push_back({
                        {"role", "tool"}, {"tool_call_id", tool_id}, {"content", tool_result}
                    });
                }
            }
        }
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
