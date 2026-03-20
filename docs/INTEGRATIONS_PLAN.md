# CLOVE v2 — Integrations & Next-Level Features Plan

> **Status:** Planning (pre-implementation)
> **Goal:** Turn CLOVE from "a kernel that boots" into "the universal agent control plane"

---

## The Big Picture

Right now CLOVE is a closed system — agents talk to the kernel via binary IPC, and that's it. To be the control plane for ALL agents, we need to connect to the outside world:

```
                    EXTERNAL ECOSYSTEM
    ┌──────────────────────────────────────────────┐
    │                                               │
    │  ┌──────────┐  ┌─────┐  ┌─────┐  ┌───────┐ │
    │  │OpenRouter │  │ MCP │  │ A2A │  │ OTel  │ │
    │  │300+ LLMs  │  │Tools│  │Proto│  │Traces │ │
    │  └─────┬─────┘  └──┬──┘  └──┬──┘  └───┬───┘ │
    │        │            │        │          │     │
    └────────┼────────────┼────────┼──────────┼─────┘
             │            │        │          │
    ╔════════╧════════════╧════════╧══════════╧═════╗
    ║                 CLOVE KERNEL                   ║
    ║                                                ║
    ║  Agents ←→ IPC ←→ Router ←→ Integrations      ║
    ╚════════════════════════════════════════════════╝
             │            │        │          │
    ┌────────┼────────────┼────────┼──────────┼─────┐
    │        │            │        │          │     │
    │  ┌─────▼─────┐  ┌──▼───┐  ┌▼────┐  ┌──▼──┐ │
    │  │Langfuse   │  │SQLite│  │Redis│  │Prom │ │
    │  │Dashboard  │  │State │  │Cache│  │     │ │
    │  └───────────┘  └──────┘  └─────┘  └─────┘ │
    │                                               │
    │                    STORAGE / MONITORING        │
    └──────────────────────────────────────────────┘
```

Each integration unlocks a multiplier:
- **OpenRouter** → 300+ models instantly (vs our 1 hardcoded Gemini)
- **MCP** → entire tool ecosystem (databases, APIs, browsers, file systems)
- **A2A** → agents on other platforms can talk to ours
- **OTel** → visible in every enterprise monitoring dashboard
- **Persistence** → data survives restarts (audit, state, replay)

---

## 1. OpenRouter Integration (PRIORITY: DO FIRST)

### What It Gives Us
- 300+ LLM models through one API key
- Automatic provider failover (if OpenAI is down, routes to Anthropic)
- Zero Data Retention option (prompts never logged by provider)
- EU routing (data stays in EU for GDPR)
- Per-key spend limits (stacks with our per-agent limits)
- Cost tracking per request (we get exact $ amounts back)

### How It Connects

```
Agent calls SYS_THINK
     │
     ▼
┌─────────────────────────────────────────────────────────┐
│ CLOVE KERNEL                                             │
│                                                          │
│  1. PermissionsStore → can_think? quota ok?              │
│  2. InferenceGateway → model allowed? cost limit ok?     │
│  3. PrivacyFilter → scan for PII, redact if configured   │
│  4. OpenRouterClient → POST /v1/chat/completions         │
│  5. Response → record cost, update quota, audit log       │
│  6. Return to agent                                       │
└─────────────────────────────────────────────────────────┘
     │
     ▼ HTTPS
┌─────────────────────────────────────────────────────────┐
│ OpenRouter API (openrouter.ai/api/v1)                    │
│                                                          │
│  • Routes to best provider for requested model           │
│  • Handles failover if provider is down                  │
│  • Returns usage: {prompt_tokens, completion_tokens,      │
│    total_cost}                                           │
│  • ZDR: prompt never stored if enabled                   │
└─────────────────────────────────────────────────────────┘
     │
     ▼
  OpenAI / Anthropic / Google / Groq / Mistral / etc.
```

### What We Build

**`libs/integrations/include/clove/openrouter.hpp`**
```cpp
struct OpenRouterConfig {
    std::string api_key;
    std::string base_url = "https://openrouter.ai/api/v1";
    bool zero_data_retention = false;
    std::string default_model = "openai/gpt-4o";
    int timeout_ms = 30000;
};

struct OpenRouterResponse {
    bool success;
    std::string content;
    int prompt_tokens;
    int completion_tokens;
    double cost_usd;
    std::string model_used;
    std::string error;
};

class OpenRouterClient {
public:
    void configure(const OpenRouterConfig& config);

    // Synchronous call (used by LLM queue workers)
    OpenRouterResponse chat(const std::string& model,
                            const std::string& prompt,
                            const nlohmann::json& options = {});

    // Streaming call (future)
    // void chat_stream(model, prompt, callback);
};
```

**How it plugs in:**
- LlmQueue gets an OpenRouterClient as its backend
- When worker thread calls `backend_(agent_id, prompt)`, it actually calls `openrouter.chat(model, prompt)`
- The response includes `cost_usd` which we feed to InferenceGateway's cost tracking
- Agent never knows — they send SYS_THINK, get back a response

**Config:**
```bash
clove_kernel --openrouter --openrouter-key sk-or-xxx \
             --llm-proxy --llm-allowed-models "gpt-4o,claude-sonnet-4"
```

**Dependencies:** libcurl (for HTTPS requests)

### What It Unlocks
- Every agent on CLOVE can use any model from any provider
- Cost tracking is REAL (not estimated — OpenRouter returns exact costs)
- ZDR compliance for sensitive industries (healthcare, finance)
- No vendor lock-in — switch models with a config change, not code change

---

## 2. MCP Integration (PRIORITY: HIGH — THIS IS THE GAME CHANGER)

### What MCP Is
Model Context Protocol — Anthropic's open standard (now under Linux Foundation). It's the USB-C for AI agents. Agents get tools (read files, query databases, call APIs, browse the web) through a standard protocol.

97M+ monthly SDK downloads. Every tool provider is building MCP servers. If CLOVE supports MCP, agents on CLOVE get access to the entire tool ecosystem for free.

### How MCP Works (The Protocol)

```
MCP Client (agent)          MCP Server (tool provider)
       │                              │
       │─── initialize ──────────────►│
       │◄── capabilities + tools ─────│
       │                              │
       │─── tools/call ──────────────►│
       │    {name: "query_db",        │
       │     args: {sql: "SELECT..."}}│
       │◄── result ──────────────────│
       │    {content: [{text: "..."}]}│
```

MCP servers run as subprocesses (stdio transport) or HTTP servers (SSE transport). The client sends JSON-RPC messages, the server responds with tool results.

### How CLOVE Mediates MCP

**The key insight:** Agents should NOT connect to MCP servers directly. The KERNEL should own the MCP connections and mediate every tool call. This gives us:
1. **Permission control** — agent A can use the GitHub MCP server, agent B cannot
2. **Audit trail** — every tool call logged with agent ID, tool name, args, result
3. **Rate limiting** — prevent agents from hammering expensive tools
4. **Shared sessions** — multiple agents can share one MCP server instance (e.g., one database connection)

```
Agent 1 ─── SYS_MCP_CALL ──►┐
Agent 2 ─── SYS_MCP_CALL ──►┤
Agent 3 ─── SYS_MCP_CALL ──►┤
                              │
                    ┌─────────▼──────────────────────────┐
                    │ CLOVE KERNEL — MCP Bridge            │
                    │                                      │
                    │ 1. Permission check (can agent use   │
                    │    this MCP server?)                  │
                    │ 2. Rate limit check                  │
                    │ 3. PII scan on args (if enabled)     │
                    │ 4. Forward to MCP server             │
                    │ 5. PII scan on response (if enabled) │
                    │ 6. Audit log the call                │
                    │ 7. Return to agent                   │
                    └─────────┬──────────────────────────┘
                              │
              ┌───────────────┼────────────────┐
              ▼               ▼                ▼
    ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
    │ MCP Server:  │ │ MCP Server:  │ │ MCP Server:  │
    │ filesystem   │ │ github       │ │ postgres     │
    │ (subprocess) │ │ (subprocess) │ │ (HTTP/SSE)   │
    └──────────────┘ └──────────────┘ └──────────────┘
```

### What We Build

**`libs/integrations/include/clove/mcp_bridge.hpp`**
```cpp
struct McpServerConfig {
    std::string name;           // "filesystem", "github", "postgres"
    std::string command;        // "npx @modelcontextprotocol/server-filesystem"
    std::vector<std::string> args;  // ["/tmp"]
    std::string transport;      // "stdio" or "sse"
    std::string url;            // For SSE transport: "http://localhost:3000"
    std::vector<std::string> allowed_agents;  // Which agents can use this
    int max_concurrent = 10;    // Max concurrent calls
};

struct McpTool {
    std::string name;
    std::string description;
    nlohmann::json input_schema;  // JSON Schema
};

struct McpCallResult {
    bool success;
    nlohmann::json content;     // Array of {type, text} or {type, data}
    std::string error;
};

class McpBridge {
public:
    // Register an MCP server
    void add_server(const McpServerConfig& config);
    void remove_server(const std::string& name);

    // Start all registered servers
    bool start_all();
    void stop_all();

    // List available tools (across all servers)
    std::vector<McpTool> list_tools(uint32_t agent_id) const;
    std::vector<McpTool> list_tools_for_server(const std::string& server) const;

    // Call a tool (kernel-mediated)
    McpCallResult call_tool(uint32_t agent_id,
                            const std::string& server_name,
                            const std::string& tool_name,
                            const nlohmann::json& args);

    // Get status of all servers
    nlohmann::json status() const;

private:
    struct McpSession {
        McpServerConfig config;
        pid_t pid = -1;
        int stdin_fd = -1;
        int stdout_fd = -1;
        std::vector<McpTool> tools;
        bool ready = false;
        std::mutex mutex;   // Per-server lock for sequential calls
        uint64_t next_id = 1;
    };
    std::unordered_map<std::string, std::unique_ptr<McpSession>> servers_;
    std::mutex registry_mutex_;

    bool spawn_server(McpSession& session);
    nlohmann::json send_jsonrpc(McpSession& session,
                                const std::string& method,
                                const nlohmann::json& params);
    bool can_agent_access(uint32_t agent_id, const McpSession& session) const;
};
```

**MCP stdio protocol flow:**
```
CLOVE                          MCP Server (subprocess)
  │                                │
  │ spawn(command, args)           │
  │ stdin_fd ──────────────────────│ reads from stdin
  │ stdout_fd ◄────────────────────│ writes to stdout
  │                                │
  │ {"jsonrpc":"2.0",              │
  │  "method":"initialize",        │
  │  "id":1,                       │
  │  "params":{                    │
  │    "protocolVersion":"2025-11",│
  │    "capabilities":{},          │
  │    "clientInfo":{              │
  │      "name":"clove",           │
  │      "version":"2.0.0"        │
  │    }                           │
  │  }}                            │
  │ ─────────────────────────────► │
  │ ◄───────────────────────────── │
  │ {"result":{                    │
  │   "protocolVersion":"2025-11", │
  │   "capabilities":{            │
  │     "tools":{}                 │
  │   },                           │
  │   "serverInfo":{...}           │
  │ }}                             │
  │                                │
  │ {"method":"initialized"}       │
  │ ─────────────────────────────► │ (notification, no response)
  │                                │
  │ {"method":"tools/list","id":2} │
  │ ─────────────────────────────► │
  │ ◄───────────────────────────── │
  │ {"result":{"tools":[           │
  │   {"name":"read_file",         │
  │    "description":"Read a file",│
  │    "inputSchema":{...}}        │
  │ ]}}                            │
  │                                │
  │ ... ready for tool calls ...   │
  │                                │
  │ {"method":"tools/call",        │
  │  "id":3,                       │
  │  "params":{                    │
  │    "name":"read_file",         │
  │    "arguments":{               │
  │      "path":"/tmp/data.csv"    │
  │    }                           │
  │  }}                            │
  │ ─────────────────────────────► │
  │ ◄───────────────────────────── │
  │ {"result":{"content":[         │
  │   {"type":"text",              │
  │    "text":"col1,col2\n..."}    │
  │ ]}}                            │
```

**New syscalls:**
- `SYS_MCP_LIST (0xD4)` — list available tools for this agent
- `SYS_MCP_CALL (0xD3)` — call a tool (server, tool_name, args)

**Config:**
```bash
clove_kernel --mcp \
  --mcp-server "filesystem:npx @modelcontextprotocol/server-filesystem /tmp" \
  --mcp-server "github:npx @modelcontextprotocol/server-github"
```

Or in manifest:
```json
{
  "mcp_servers": [
    {
      "name": "filesystem",
      "command": "npx",
      "args": ["@modelcontextprotocol/server-filesystem", "/tmp"],
      "allowed_agents": ["researcher", "analyst"]
    },
    {
      "name": "postgres",
      "transport": "sse",
      "url": "http://localhost:3000/mcp",
      "allowed_agents": ["data-agent"]
    }
  ]
}
```

### What It Unlocks
- Agents get file access, database queries, web browsing, GitHub, Slack, Jira — anything with an MCP server
- Kernel mediates every tool call → audit, permissions, PII filtering
- Shared MCP sessions → efficient resource usage
- Agents don't need to know about MCP — they call `SYS_MCP_CALL` and the kernel handles the rest

---

## 3. A2A Protocol Bridge (PRIORITY: MEDIUM)

### What A2A Is
Google's Agent-to-Agent protocol. HTTP + SSE + JSON-RPC. Lets agents on different platforms communicate. 50+ partners (Salesforce, SAP, Atlassian).

### How CLOVE Uses It

**Inbound:** External agents can talk to CLOVE agents.
**Outbound:** CLOVE agents can talk to external agents.

```
External Agent                   CLOVE Kernel                    CLOVE Agent
(Salesforce)                        │                               │
    │                               │                               │
    │── POST /a2a/message ────────►│                               │
    │   {to: "researcher",          │                               │
    │    content: "analyze Q1"}     │── SYS_SEND ────────────────►│
    │                               │   (delivered to mailbox)      │
    │                               │                               │
    │                               │◄── SYS_A2A_SEND ────────────│
    │◄── HTTP response ────────────│   (to external agent)         │
    │   {content: "analysis..."}    │                               │
```

**What we build:**
- HTTP server listening on `--a2a-port 8081`
- A2A Agent Card endpoint (`GET /.well-known/agent.json`) describing our agents
- Message routing: A2A message → find CLOVE agent by name → deliver via IPC mailbox
- Outbound: `SYS_A2A_SEND` → HTTP POST to external A2A endpoint

### What It Unlocks
- CLOVE agents can participate in cross-platform workflows
- Enterprise agents on Salesforce/SAP can delegate tasks to CLOVE agents
- Our fast IPC (0.02ms) for co-located agents, A2A (HTTP) for remote — best of both

---

## 4. OpenTelemetry Export (PRIORITY: MEDIUM)

### What It Gives Us
Every enterprise uses Datadog, Grafana, New Relic, or Splunk. If CLOVE emits OTel traces, our agents appear in their existing dashboards. Zero integration effort for the customer.

### How It Works

```
Agent calls SYS_THINK
     │
CLOVE Kernel:
     │
     ├── Creates OTel span: {
     │     name: "SYS_THINK",
     │     attributes: {
     │       agent_id: 42,
     │       agent_name: "researcher",
     │       model: "gpt-4o",
     │       prompt_tokens: 150,
     │       completion_tokens: 500,
     │       cost_usd: 0.0035,
     │       duration_ms: 1200,
     │       success: true
     │     }
     │   }
     │
     ├── Multi-agent trace propagation:
     │     SYS_SEND creates child span linked to receiver's trace
     │     Crew pipeline = one trace with 5 agent spans
     │
     └── Export via OTLP (gRPC or HTTP)
              │
              ▼
         Jaeger / Grafana Tempo / Datadog / New Relic
```

**What we build:**
- `OtelExporter` class that batches spans and sends via OTLP HTTP
- Hook into every syscall: create span with opcode, agent_id, duration, success
- LLM-specific attributes: model, tokens, cost (from OpenRouter response)
- Trace context propagation across agent IPC (SYS_SEND carries trace_id)

**Config:**
```bash
clove_kernel --otel --otel-endpoint http://localhost:4318/v1/traces
```

### What It Unlocks
- Enterprise adoption: "CLOVE agents show up in our Datadog dashboard"
- Multi-agent debugging: one trace shows the entire crew pipeline
- Performance monitoring: latency, cost, error rate per agent
- Alerting: "Agent X cost > $100 in the last hour" triggers PagerDuty

---

## 5. SQLite Persistence (PRIORITY: HIGH)

### Why
Everything is in-memory. Restart the kernel → lose all audit logs, state, permissions, execution recordings. No enterprise will accept this.

### What Gets Persisted

| Data | Current | With SQLite |
|------|---------|-------------|
| Audit log | In-memory ring buffer (10K entries) | Append-only table, unlimited history |
| State store | In-memory KV | Persistent KV with TTL enforcement |
| Execution recordings | In-memory vector | Stored per-session, queryable |
| Permissions | In-memory map | Persist across restarts |
| Agent registry | In-memory | Track historical agents |
| Cost tracking | In-memory counter | Running totals per agent/day/week |

### Schema

```sql
-- Audit log (append-only)
CREATE TABLE audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,         -- ISO 8601
    category TEXT NOT NULL,          -- SECURITY, AGENT_LIFECYCLE, etc.
    event_type TEXT NOT NULL,
    agent_id INTEGER,
    agent_name TEXT,
    details TEXT,                    -- JSON
    success INTEGER DEFAULT 1
);
CREATE INDEX idx_audit_category ON audit_log(category);
CREATE INDEX idx_audit_agent ON audit_log(agent_id);
CREATE INDEX idx_audit_time ON audit_log(timestamp);

-- State store (KV with TTL)
CREATE TABLE state_store (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,             -- JSON
    owner_agent_id INTEGER,
    scope TEXT DEFAULT 'global',
    expires_at TEXT,                 -- ISO 8601, NULL = no expiry
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

-- Execution recordings
CREATE TABLE execution_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,        -- Recording session ID
    sequence_id INTEGER NOT NULL,
    timestamp TEXT NOT NULL,
    agent_id INTEGER,
    opcode INTEGER,
    payload TEXT,
    response TEXT,
    duration_us INTEGER,
    success INTEGER
);
CREATE INDEX idx_exec_session ON execution_log(session_id);

-- Permissions (persist across restarts)
CREATE TABLE permissions (
    agent_id INTEGER PRIMARY KEY,
    permissions TEXT NOT NULL         -- JSON blob
);

-- Cost tracking
CREATE TABLE cost_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    agent_id INTEGER,
    model TEXT,
    prompt_tokens INTEGER,
    completion_tokens INTEGER,
    cost_usd REAL,
    provider TEXT
);
CREATE INDEX idx_cost_agent ON cost_log(agent_id);
CREATE INDEX idx_cost_time ON cost_log(timestamp);
```

### Architecture

```
In-memory (fast path)           SQLite (durable path)
┌──────────────────┐           ┌──────────────────┐
│ StateStore       │──write──►│ state_store table │
│ (KV map)         │◄──load───│                   │
├──────────────────┤           ├──────────────────┤
│ AuditLogger      │──append─►│ audit_log table   │
│ (ring buffer)    │           │ (append-only)     │
├──────────────────┤           ├──────────────────┤
│ ExecutionLogger  │──flush──►│ execution_log     │
│ (vector)         │           │                   │
├──────────────────┤           ├──────────────────┤
│ PermissionsStore │──save───►│ permissions table │
│ (map)            │◄──load───│                   │
├──────────────────┤           ├──────────────────┤
│ InferenceGateway │──log────►│ cost_log table    │
│ (cost counter)   │           │                   │
└──────────────────┘           └──────────────────┘
```

**Strategy:** Write-ahead. In-memory stores are the source of truth during runtime (fast). SQLite is the durable backing store. On startup, load from SQLite. During runtime, write-through (or periodic flush) to SQLite.

---

## 6. REST API + Dashboard (PRIORITY: MEDIUM-HIGH)

### Why
Operators need visibility. CI/CD needs programmatic access. The kernel is great for agents, but humans need HTTP.

### API Design

```
GET  /api/health                    → {"status": "ok", "version": "2.0.0", "uptime_s": 3600}
GET  /api/agents                    → [{id, name, state, pid, uptime, llm_tokens}]
POST /api/agents                    → spawn agent from config
DELETE /api/agents/:id              → kill agent

GET  /api/agents/:id/metrics       → {memory, cpu, llm_calls, llm_tokens}
GET  /api/agents/:id/permissions   → {can_exec, can_read, ...}
PUT  /api/agents/:id/permissions   → update permissions (delta merge)

GET  /api/audit                    → [{id, timestamp, category, event, ...}]
GET  /api/audit?category=SECURITY  → filtered audit entries
GET  /api/audit/export             → JSONL download

GET  /api/replay                   → list recording sessions
POST /api/replay/start             → start recording
POST /api/replay/stop              → stop recording

GET  /api/inference                → inference gateway config
PUT  /api/inference                → update model/provider/cost config

GET  /api/privacy/scan             → scan text for PII (POST body)
GET  /api/policy/recommendations   → get suggested policy changes

GET  /api/mcp/servers              → list MCP servers
GET  /api/mcp/tools                → list available tools
POST /api/mcp/call                 → call a tool (for testing)

GET  /api/metrics                  → system metrics (CPU, memory, agent count)
GET  /api/metrics/costs            → cost breakdown per agent/model/day

WebSocket:
  /ws/logs                         → real-time log streaming
  /ws/events                       → real-time event streaming
  /ws/agents                       → agent state changes
```

### Dashboard (HTMX — no React, no build step)

```
┌─────────────────────────────────────────────────────────┐
│ CLOVE Dashboard                          [refresh: 5s]  │
├──────────────────────────┬──────────────────────────────┤
│                          │                               │
│  AGENTS (8 running)      │  COST TRACKER                │
│  ┌────┬───────┬──────┐  │  Today:    $12.45             │
│  │ ID │ Name  │ State│  │  This week: $67.30            │
│  ├────┼───────┼──────┤  │  Limit:     $500.00           │
│  │ 1  │ rsrch │ ✅   │  │  ████████░░░░░░ 13%           │
│  │ 2  │ write │ ✅   │  │                               │
│  │ 3  │ revw  │ ⏸   │  │  TOP MODELS:                  │
│  │ 4  │ cs-01 │ ✅   │  │  gpt-4o:     $8.20 (65%)     │
│  │ 5  │ cs-02 │ ✅   │  │  claude:     $3.10 (25%)     │
│  │ ...│       │      │  │  gemini:     $1.15 (10%)     │
│  └────┴───────┴──────┘  │                               │
│                          ├──────────────────────────────┤
│  RECENT AUDIT            │  PII DETECTIONS              │
│  [SECURITY] Agent 3      │  Last hour: 7 redacted       │
│    blocked HTTP to        │  Types: 3 email, 2 SSN,     │
│    api.openai.com         │         1 phone, 1 CC       │
│  [LIFECYCLE] Agent 4      │                               │
│    restarted (attempt 2)  │  POLICY RECOMMENDATIONS     │
│  [SECURITY] PII detected  │  • Add domain: arxiv.org    │
│    SSN redacted in prompt │    (denied 15 times)        │
│                          │  • Add cmd: pip install      │
│                          │    (denied 8 times)          │
│                          │  [Approve] [Reject]          │
└──────────────────────────┴──────────────────────────────┘
```

**Implementation:** cpp-httplib (header-only HTTP server) + HTMX for reactive updates. No JavaScript build system. HTML templates served from embedded strings or static files.

---

## 7. Langfuse Integration (PRIORITY: LOW — nice to have)

### What It Gives Us
Detailed LLM call tracing — prompt/response pairs, token counts, latency, cost, eval scores. Developers use Langfuse for debugging and prompt engineering.

### How It Works
- On every SYS_THINK, send a trace event to Langfuse
- Includes: prompt, response, model, tokens, cost, agent_id, latency
- Multi-agent traces: crew pipeline as one Langfuse trace with agent spans

**Implementation:** HTTP POST to Langfuse API after each LLM call. Async (fire-and-forget, don't block the agent).

---

## 8. Credential Vault (PRIORITY: MEDIUM)

### The Problem
Right now agents manage their own API keys. That's insecure — keys in env vars, visible in /proc, copied between agents.

### The Solution
Kernel-managed credentials. Agents request credentials by name, kernel injects them.

```
Agent: SYS_CREDS_GET("openai_key")
  → Kernel looks up in credential store
  → Permission check (is this agent allowed this credential?)
  → Returns the key
  → Audit log: "Agent 42 accessed credential 'openai_key'"
```

**Storage:** Encrypted at rest in SQLite (AES-256-GCM with a master key derived from a passphrase or hardware key).

---

## 9. SSRF Protection (PRIORITY: HIGH — security)

### The Problem
Agents making HTTP requests could be tricked into calling internal services (127.0.0.1, 10.x.x.x, 169.254.x.x metadata endpoints). This is Server-Side Request Forgery.

### The Solution
Before every SYS_HTTP, resolve the hostname and check if the IP is private:

```cpp
bool SsrfGuard::is_safe(const std::string& url) {
    auto ip = resolve(extract_host(url));
    if (is_loopback(ip)) return false;      // 127.0.0.0/8
    if (is_private(ip)) return false;        // 10.0.0.0/8, 172.16-31.x, 192.168.x
    if (is_link_local(ip)) return false;     // 169.254.0.0/16
    if (is_metadata(ip)) return false;       // 169.254.169.254 (cloud metadata)
    return true;
}
```

This is 20 lines of code but prevents a real attack vector that OpenShell implements and we don't.

---

## 10. Agent Templates / Marketplace (PRIORITY: FUTURE)

### Concept
Pre-built agents that deploy with one command:

```bash
clove deploy template://researcher     # Web researcher agent
clove deploy template://code-reviewer  # Code review agent
clove deploy template://data-analyst   # Data analysis agent
clove deploy template://customer-service # CS agent with routing
```

Templates include: agent code + manifest (permissions, resources, MCP servers needed) + suggested policies.

**This is a network effect play.** More templates → more users → more templates.

---

## Implementation Priority & Order

```
WEEK 1: Foundation
├── OpenRouter client (libcurl HTTP, sync)
├── SQLite persistence (audit + state + permissions)
├── SSRF guard (20 lines, blocks private IPs)
└── Wire remaining syscall handlers (SYS_SPAWN, THINK, SEND, etc.)

WEEK 2: MCP
├── MCP Bridge (stdio transport — subprocess management)
├── MCP JSON-RPC protocol implementation
├── SYS_MCP_LIST + SYS_MCP_CALL handlers
├── Per-agent MCP server access control
└── MCP tool call audit logging

WEEK 3: API + Dashboard
├── REST API (cpp-httplib)
├── HTMX dashboard (agents, audit, costs, PII, recommendations)
├── WebSocket for real-time log/event streaming
└── API key authentication

WEEK 4: Observability
├── OTel exporter (OTLP HTTP)
├── Trace context propagation across agent IPC
├── Langfuse integration (async fire-and-forget)
└── Credential vault (encrypted SQLite)

WEEK 5: Cross-System
├── A2A bridge (HTTP server + client)
├── A2A Agent Card endpoint
├── SYS_A2A_SEND + SYS_A2A_RECV handlers
└── Cross-system trace propagation

WEEK 6: Distribution
├── Docker image (Alpine + kernel, ~15MB)
├── curl installer (get.cloveos.com)
├── GitHub Action (cloveos/setup-clove@v1)
├── systemd service file
└── Agent template system (template://)
```

---

## What This Makes CLOVE

After all integrations:

```
CLOVE = Agent Control Plane
├── Run any agent (LangChain, CrewAI, OpenClaw, OpenAI Agents, custom)
├── Any model (300+ via OpenRouter, or direct, or local)
├── Any tool (entire MCP ecosystem — files, DBs, APIs, browsers)
├── Talk to any agent (A2A protocol — Salesforce, SAP, etc.)
├── Visible everywhere (OTel → Datadog, Grafana, New Relic)
├── Debuggable (Langfuse traces + execution replay)
├── Compliant (audit + PII filter + execution replay + cost controls)
├── Durable (SQLite persistence, survives restarts)
├── Secure (namespaces + cgroups + Landlock + seccomp + SSRF + egress proxy)
└── Fast (0.02ms IPC, 54K ops/sec, 27ms cold start, 2.1MB binary)
```

**The pitch in one sentence:**
*"CLOVE is the control plane that lets you run any agent, with any model, using any tool, talking to any other agent, visible in your monitoring, compliant with your regulations, and fast enough to coordinate a fleet of thousands."*

That's what takes it to the next level.
