# CLOVE v2 REST API Specification

**Version:** 2.0.0
**Base URL:** `http://localhost:<port>/api`
**Default port:** 8080 (configurable via `--api-port`)
**Content-Type:** All request and response bodies are `application/json` unless otherwise noted.

---

## 1. Authentication

All endpoints (except `/api/health`, `/dashboard/*`, and `/api/fragments/*`) require a Bearer token when the kernel is started with `--api-key`.

```
Authorization: Bearer <your-api-key>
```

**Unauthenticated request response:**

```json
{ "error": "unauthorized" }
```

Status: `401`

If the kernel was started without `--api-key`, all endpoints are open (no auth header required).

---

## 2. Error Format

All errors follow the same shape:

```json
{ "error": "<human-readable message>" }
```

Common error status codes:
| Code | Meaning |
|------|---------|
| 400  | Bad request (invalid JSON, missing required field) |
| 401  | Unauthorized (missing or invalid Bearer token) |
| 404  | Resource not found |
| 503  | Service unavailable (subsystem not enabled, e.g. LLM not configured) |
| 500  | Internal server error |

---

## 3. Endpoint Reference

### 3.1 Health

#### `GET /api/health`

No auth required. Use this for liveness/readiness probes.

**Response** `200`
```json
{
  "status": "ok",
  "version": "2.0.0",
  "uptime_s": 3621,
  "syscall_count": 66
}
```

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Always `"ok"` |
| `version` | string | Kernel version |
| `uptime_s` | integer | Seconds since API server started |
| `syscall_count` | integer | Number of registered kernel syscall opcodes |

---

### 3.2 Think (One-Shot LLM)

#### `POST /api/think`

Send a prompt to an LLM and get a single completion back. Supports both simple prompt strings and OpenAI-format message arrays. PII is automatically redacted before the prompt is sent to the provider.

**Request Body**
```json
{
  "prompt": "Explain quantum entanglement in one paragraph.",
  "model": "anthropic/claude-sonnet-4",
  "messages": []
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `prompt` | string | One of `prompt` or `messages` required | `""` | Plain text prompt |
| `messages` | array | One of `prompt` or `messages` required | `[]` | OpenAI-format messages array (`[{"role":"user","content":"..."}]`) |
| `model` | string | No | Kernel default model | OpenRouter model ID (e.g. `"openai/gpt-4o"`, `"anthropic/claude-sonnet-4"`) |

**Response** `200`
```json
{
  "success": true,
  "content": "Quantum entanglement is a phenomenon...",
  "model": "anthropic/claude-sonnet-4",
  "tokens": 187,
  "prompt_tokens": 42,
  "completion_tokens": 145,
  "cost_usd": 0.000891,
  "pii_redacted": 0
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | boolean | Whether the LLM call succeeded |
| `content` | string | The LLM's response text |
| `model` | string | Actual model used (may differ from requested if aliased) |
| `tokens` | integer | Total tokens (prompt + completion) |
| `prompt_tokens` | integer | Tokens in the prompt |
| `completion_tokens` | integer | Tokens in the completion |
| `cost_usd` | number | Cost of this call in USD |
| `pii_redacted` | integer | (Only present if > 0) Number of PII items redacted before sending to LLM |
| `error` | string | (Only present on failure) Error message |

**Errors**
- `400` — Neither `prompt` nor `messages` provided
- `503` — `LLM not configured. Set OPENROUTER_API_KEY.`

---

### 3.3 Run (Agent with Tools — Synchronous)

#### `POST /api/run`

Execute a full agent tool-calling loop. The agent receives a goal, plans, and iteratively calls tools (file I/O, HTTP, exec, MCP, memory, search) until the goal is met or budget is exhausted. This is a blocking call that returns the full result when done.

**Request Body**
```json
{
  "goal": "Find the top 3 trending AI papers this week and summarize them",
  "model": "anthropic/claude-sonnet-4",
  "budget": 0.50,
  "max_steps": 20,
  "tools": ["read_file", "write_file", "exec", "http", "search", "mcp_call", "remember", "recall"],
  "agent_name": "researcher"
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `goal` | string | **Yes** | | The task for the agent to accomplish |
| `model` | string | No | Kernel default | OpenRouter model ID |
| `budget` | number | No | `1.0` | Maximum spend in USD for this run |
| `max_steps` | integer | No | `20` | Maximum tool-calling iterations |
| `tools` | string[] | No | `[]` (all tools) | Whitelist of allowed tool names. Empty = all tools allowed |
| `agent_name` | string | No | `"agent"` | Name for this agent (appears in logs and events) |

**Available tool names:** `read_file`, `write_file`, `exec`, `http`, `search`, `mcp_call`, `remember`, `recall`

**Response** `200`
```json
{
  "success": true,
  "content": "Here are the top 3 trending AI papers this week:\n\n1. ...",
  "steps": 7,
  "total_tokens": 4231,
  "total_cost_usd": 0.0342,
  "budget_usd": 0.50,
  "model": "anthropic/claude-sonnet-4",
  "chain_id": "chain_a1b2c3d4",
  "step_log": [
    {
      "step": 1,
      "tool": "search",
      "args": { "query": "trending AI papers this week" },
      "result": "Found 10 results...",
      "tokens": 512,
      "cost_usd": 0.004
    }
  ],
  "error": ""
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | boolean | Whether the agent completed its goal |
| `content` | string | Final output/answer from the agent |
| `steps` | integer | Number of tool-calling steps taken |
| `total_tokens` | integer | Total tokens consumed across all steps |
| `total_cost_usd` | number | Total cost in USD for this run |
| `budget_usd` | number | The budget that was set for this run |
| `model` | string | Model used |
| `chain_id` | string | Chain ID for retrieving this run later via `GET /api/run/:chain_id` |
| `step_log` | array | Detailed log of each tool call (see below) |
| `error` | string | (Only present on failure) Error message |

**Step log entry schema:**

| Field | Type | Description |
|-------|------|-------------|
| `step` | integer | 1-indexed step number |
| `tool` | string | Tool that was called |
| `args` | object | Arguments passed to the tool |
| `result` | string | Tool output |
| `tokens` | integer | Tokens used in this step |
| `cost_usd` | number | Cost of this step |

**Errors**
- `400` — `goal is required` or invalid JSON
- `503` — LLM not configured

---

### 3.4 Run Stream (Agent with Tools — SSE)

#### `POST /api/run/stream`

Same as `/api/run` but returns results as a Server-Sent Events stream. Use this for real-time UI updates showing each tool call as it happens.

**Request Body** — Identical to `POST /api/run`.

**Response** — `200` with `Content-Type: text/event-stream`

Each event is a single `data:` line containing a JSON object. Events are emitted in real-time as the agent works.

**Event types during execution:**

```
data: {"type":"start","data":{"goal":"...","model":"..."}}

data: {"type":"thinking","data":{"content":"I need to search for..."}}

data: {"type":"tool_call","data":{"tool":"search","args":{"query":"trending AI papers"}}}

data: {"type":"tool_result","data":{"tool":"search","result":"Found 10 results..."}}

data: {"type":"done","data":{"content":"Here is my final answer..."}}

data: {"type":"error","data":{"message":"Budget exceeded"}}
```

**Final result event (always last):**

```
data: {"type":"result","data":{"success":true,"content":"...","steps":7,"total_tokens":4231,"total_cost_usd":0.0342,"chain_id":"chain_a1b2c3d4","step_log":[...]}}
```

**Event Type Reference:**

| Event `type` | When emitted | `data` fields |
|-------------|--------------|---------------|
| `start` | Agent begins execution | `goal`, `model` |
| `thinking` | Agent is reasoning | `content` (the LLM's chain-of-thought) |
| `tool_call` | Agent invokes a tool | `tool`, `args` |
| `tool_result` | Tool returns | `tool`, `result` |
| `done` | Agent finishes successfully | `content` (final answer) |
| `error` | An error occurred | `message` |
| `result` | Final summary (always last) | `success`, `content`, `steps`, `total_tokens`, `total_cost_usd`, `chain_id`, `step_log`, optionally `error` |

**Errors** — Same as `/api/run`. If errors occur before streaming begins, a normal JSON error response is returned instead of SSE.

---

### 3.5 Fleet (Parallel Agents — SSE)

#### `POST /api/fleet`

Run multiple agents in parallel on the same goal, then synthesize their outputs into one coherent result. Returns an SSE stream with events from all agents.

The fleet operates in two phases:
1. **Research phase** — N agents run in parallel, each with `total_budget / N` budget
2. **Synthesis phase** — One final LLM call merges all agent outputs

**Request Body**
```json
{
  "goal": "Research the current state of quantum computing startups",
  "agents": 5,
  "budget": 2.00,
  "model": "anthropic/claude-sonnet-4",
  "max_steps": 15,
  "tools": ["search", "http"]
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `goal` | string | **Yes** | | Task for all agents |
| `agents` | integer | No | `3` | Number of parallel agents (max 20) |
| `budget` | number | No | `2.0` | Total budget in USD (split equally among agents) |
| `model` | string | No | Kernel default | OpenRouter model ID |
| `max_steps` | integer | No | `15` | Max steps per agent |
| `tools` | string[] | No | `[]` (all) | Allowed tool names |

**Response** — `200` with `Content-Type: text/event-stream`

**Fleet SSE Event Sequence:**

```
data: {"type":"fleet_start","data":{"goal":"...","agent_count":5,"total_budget_usd":2.0,"per_agent_budget_usd":0.4}}

data: {"type":"agent_event","data":{"agent":"agent-1","event_type":"thinking","content":"..."}}

data: {"type":"agent_event","data":{"agent":"agent-3","event_type":"tool_call","tool":"search","args":{...}}}

data: {"type":"agent_done","data":{"agent":"agent-1","success":true,"steps":8,"cost_usd":0.032,"tokens":2100,"completed":1,"total":5}}

data: {"type":"agent_done","data":{"agent":"agent-3","success":true,"steps":6,"cost_usd":0.028,"tokens":1800,"completed":2,"total":5}}

... (more agent_done events) ...

data: {"type":"synthesizing","data":{"message":"Synthesizing outputs from all agents..."}}

data: {"type":"fleet_done","data":{"success":true,"synthesis":"Here is the merged research...","agent_count":5,"total_steps":35,"total_tokens":12400,"total_cost_usd":0.187,"agents":[{"agent":"agent-1","success":true,"content_preview":"...","steps":8,"cost_usd":0.032,"chain_id":"chain_abc123"},...]}}
```

**Fleet Event Type Reference:**

| Event `type` | When emitted | `data` fields |
|-------------|--------------|---------------|
| `fleet_start` | Fleet begins | `goal`, `agent_count`, `total_budget_usd`, `per_agent_budget_usd` |
| `agent_event` | An individual agent emits an event | `agent` (name), `event_type` (one of: `thinking`, `tool_call`, `tool_result`, `done`, `error`), plus the inner event's fields |
| `agent_done` | One agent finishes | `agent`, `success`, `steps`, `cost_usd`, `tokens`, `completed` (how many agents done so far), `total` |
| `synthesizing` | Synthesis LLM call starts | `message` |
| `fleet_done` | All work complete (always last) | `success`, `synthesis` (merged text, first 500 chars), `agent_count`, `total_steps`, `total_tokens`, `total_cost_usd`, `agents` (array of per-agent summaries) |

**Per-agent summary in `fleet_done.data.agents`:**

| Field | Type | Description |
|-------|------|-------------|
| `agent` | string | Agent name (e.g. `"agent-1"`) |
| `success` | boolean | Whether this agent succeeded |
| `content_preview` | string | First 300 chars of agent output |
| `steps` | integer | Steps taken |
| `cost_usd` | number | Cost for this agent |
| `chain_id` | string | Chain ID for this agent's run |

---

### 3.6 Run Status (Past Run by Chain ID)

#### `GET /api/run/:chain_id`

Retrieve details of a past run, including its artifacts.

**Path Parameters**
| Param | Type | Description |
|-------|------|-------------|
| `chain_id` | string | Chain ID returned from a run (e.g. `chain_a1b2c3d4`) |

**Response** `200`
```json
{
  "chain_id": "chain_a1b2c3d4",
  "name": "agent",
  "description": "Find the top 3 trending AI papers this week and summarize them",
  "created_at_ms": 1711324800000,
  "artifact_count": 3,
  "artifacts": [
    {
      "id": "art_x9y8z7",
      "type": "REPORT",
      "title": "Research Summary",
      "content_preview": "Here are the top 3 trending AI papers...",
      "state": "FINAL"
    }
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `chain_id` | string | Chain identifier |
| `name` | string | Chain/agent name |
| `description` | string | Goal or description |
| `created_at_ms` | integer | Unix timestamp in milliseconds |
| `artifact_count` | integer | Number of artifacts in this chain |
| `artifacts` | array | List of artifacts (see below) |

**Artifact schema:**

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Artifact ID |
| `type` | string | One of: `REPORT`, `CODE`, `DATA`, `NOTE`, `PLAN` |
| `title` | string | Human-readable title |
| `content_preview` | string | First 500 characters of content |
| `state` | string | One of: `DRAFT`, `FINAL`, `ARCHIVED` |

**Errors**
- `404` — `run not found`
- `503` — `chain store not available`

---

### 3.7 History (Past Runs)

#### `GET /api/history`

List past runs (chains). Returns the most recent 100 runs.

**Query Parameters**
| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `limit` | integer | `100` | Max number of runs to return |

**Response** `200`
```json
{
  "runs": [
    {
      "chain_id": "chain_a1b2c3d4",
      "name": "researcher",
      "description": "Find trending AI papers",
      "artifact_count": 2,
      "created_at_ms": 1711324800000
    }
  ],
  "count": 1
}
```

| Field | Type | Description |
|-------|------|-------------|
| `runs` | array | Array of run summaries |
| `runs[].chain_id` | string | Chain ID (use with `GET /api/run/:chain_id`) |
| `runs[].name` | string | Agent/chain name |
| `runs[].description` | string | Goal or description |
| `runs[].artifact_count` | integer | Number of artifacts |
| `runs[].created_at_ms` | integer | Unix timestamp ms |
| `count` | integer | Total number of runs returned |

---

### 3.8 Agents

#### `GET /api/agents`

List all agents (running, paused, stopped).

**Response** `200`
```json
[
  {
    "id": 1,
    "name": "web-crawler",
    "state": "RUNNING",
    "pid": 48721,
    "uptime_s": 127.5
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | Unique agent ID (monotonic) |
| `name` | string | Agent name |
| `state` | string | One of: `RUNNING`, `STOPPED`, `PAUSED`, `STARTING`, `RESTARTING` |
| `pid` | integer | OS process ID |
| `uptime_s` | number | Seconds since agent was created |

---

#### `POST /api/agents`

Spawn a new agent process.

**Request Body**
```json
{
  "name": "web-crawler",
  "command": "python3 agent.py"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | **Yes** | Agent name |
| `command` | string | **Yes** | Command/script to execute |

**Response** `201`
```json
{
  "id": 2,
  "name": "web-crawler",
  "state": "RUNNING",
  "pid": 48732
}
```

**Errors**
- `400` — `name and command are required`
- `500` — `failed to spawn agent`

---

#### `DELETE /api/agents/:id`

Kill an agent (sends SIGTERM, then SIGKILL after timeout).

**Path Parameters**
| Param | Type | Description |
|-------|------|-------------|
| `id` | integer | Agent ID |

**Response** `200`
```json
{ "success": true }
```

**Errors**
- `404` — `agent not found`

---

#### `GET /api/agents/:id/metrics`

Get detailed resource metrics for an agent.

**Response** `200`
```json
{
  "id": 1,
  "name": "web-crawler",
  "pid": 48721,
  "state": "RUNNING",
  "memory_bytes": 67108864,
  "cpu_percent": 12.5,
  "uptime_seconds": 3621.4,
  "llm_request_count": 42,
  "llm_tokens_used": 18400,
  "parent_id": 0,
  "child_ids": [3, 4],
  "created_at_ms": 1711324800000
}
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | Agent ID |
| `name` | string | Agent name |
| `pid` | integer | OS process ID |
| `state` | string | Agent state |
| `memory_bytes` | integer | Resident memory in bytes (RSS) |
| `cpu_percent` | number | CPU usage percentage |
| `uptime_seconds` | number | Seconds since creation |
| `llm_request_count` | integer | Number of LLM calls made |
| `llm_tokens_used` | integer | Total LLM tokens consumed |
| `parent_id` | integer | Parent agent ID (0 = top-level) |
| `child_ids` | integer[] | IDs of child agents |
| `created_at_ms` | integer | Unix timestamp ms of creation |

---

#### `GET /api/agents/:id/permissions`

Get an agent's capability flags and ACLs.

**Response** `200`
```json
{
  "can_exec": false,
  "can_read": true,
  "can_write": true,
  "can_think": true,
  "can_spawn": false,
  "can_http": false,
  "allowed_read_paths": [],
  "allowed_write_paths": [],
  "blocked_paths": [],
  "allowed_commands": [],
  "blocked_commands": [],
  "allowed_domains": [],
  "allowed_http_methods": [],
  "max_exec_time_ms": 30000
}
```

| Field | Type | Description |
|-------|------|-------------|
| `can_exec` | boolean | Can execute shell commands |
| `can_read` | boolean | Can read files |
| `can_write` | boolean | Can write files |
| `can_think` | boolean | Can make LLM calls |
| `can_spawn` | boolean | Can spawn child agents |
| `can_http` | boolean | Can make HTTP requests |
| `allowed_read_paths` | string[] | Glob patterns for allowed read paths |
| `allowed_write_paths` | string[] | Glob patterns for allowed write paths |
| `blocked_paths` | string[] | Glob patterns for blocked paths |
| `allowed_commands` | string[] | Command prefixes allowed for exec |
| `blocked_commands` | string[] | Blocked command prefixes |
| `allowed_domains` | string[] | Allowed HTTP domains (glob patterns) |
| `allowed_http_methods` | string[] | Allowed HTTP methods (e.g. `["GET","POST"]`) |
| `max_exec_time_ms` | integer | Max execution time for shell commands in ms |

**Errors**
- `404` — `agent not found`

---

#### `PUT /api/agents/:id/permissions`

Update agent permissions (delta merge -- only fields you send are changed).

**Request Body** — Any subset of the permissions fields:
```json
{
  "can_exec": true,
  "can_http": true,
  "allowed_domains": ["api.github.com", "*.openai.com"]
}
```

**Response** `200` — Returns the full updated permissions object (same schema as GET).

---

#### `GET /api/agents/:id/budget`

Get an agent's budget limits and current usage.

**Response** `200`
```json
{
  "max_tokens": 100000,
  "tokens_used": 18400,
  "max_steps": 500,
  "steps_taken": 42,
  "max_time_ms": 3600000,
  "elapsed_ms": 127500,
  "max_cost_usd": 5.00,
  "cost_usd": 0.87,
  "kill_on_exceeded": false
}
```

| Field | Type | Description |
|-------|------|-------------|
| `max_tokens` | integer | Token limit (0 = unlimited) |
| `tokens_used` | integer | Tokens consumed so far |
| `max_steps` | integer | Step limit (0 = unlimited) |
| `steps_taken` | integer | Steps taken so far |
| `max_time_ms` | integer | Time limit in ms (0 = unlimited) |
| `elapsed_ms` | integer | Time elapsed since agent started |
| `max_cost_usd` | number | Cost limit in USD (0 = unlimited) |
| `cost_usd` | number | Cost spent so far |
| `kill_on_exceeded` | boolean | Whether to SIGKILL agent when budget is exceeded |

---

#### `POST /api/agents/:id/budget`

Set or update an agent's budget (delta merge).

**Request Body** — Any subset of budget limit fields:
```json
{
  "max_tokens": 50000,
  "max_cost_usd": 2.50,
  "max_steps": 200,
  "max_time_ms": 1800000,
  "kill_on_exceeded": true
}
```

**Response** `200` — Returns the full updated budget object (same schema as GET).

---

### 3.9 Audit

#### `GET /api/audit`

Query the audit log with optional filters.

**Query Parameters**
| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `category` | string | (all) | Filter by category. One of: `SECURITY`, `AGENT_LIFECYCLE`, `IPC`, `STATE_STORE`, `RESOURCE`, `SYSCALL`, `NETWORK`, `WORLD` |
| `agent_id` | integer | (all) | Filter by agent ID |
| `since_id` | integer | `0` | Only return entries with ID greater than this (for polling) |
| `limit` | integer | `100` | Max entries to return |

**Response** `200`
```json
[
  {
    "id": 1042,
    "timestamp": "2026-03-25T14:30:00Z",
    "category": "RESOURCE",
    "event_type": "THINK",
    "agent_id": 0,
    "agent_name": "",
    "success": true,
    "details": {
      "model": "anthropic/claude-sonnet-4",
      "tokens": 187,
      "cost_usd": 0.000891,
      "prompt_len": 42
    }
  }
]
```

Each audit entry has:

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | Monotonically increasing entry ID |
| `timestamp` | string | ISO 8601 timestamp |
| `category` | string | Audit category |
| `event_type` | string | Event name (e.g. `THINK`, `SPAWN`, `KILL`, `HTTP`, `EXEC`) |
| `agent_id` | integer | Agent that triggered this (0 = kernel) |
| `agent_name` | string | Agent name (empty if kernel) |
| `success` | boolean | Whether the operation succeeded |
| `details` | object | Event-specific metadata |

---

#### `GET /api/audit/export`

Export the full audit log as JSONL (newline-delimited JSON). Useful for compliance and archival.

**Query Parameters**
| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `limit` | integer | `0` (all) | Max entries to export |

**Response** `200` with `Content-Type: application/x-ndjson`

```
{"id":1,"timestamp":"...","category":"SECURITY","event_type":"SPAWN",...}
{"id":2,"timestamp":"...","category":"RESOURCE","event_type":"THINK",...}
```

---

### 3.10 Cost / Metrics

#### `GET /api/cost`

Get system-wide LLM cost tracking.

**Response** `200`
```json
{
  "total_cost_usd": 1.2345,
  "max_cost_usd": 50.00,
  "total_requests": 142,
  "total_completed": 140
}
```

| Field | Type | Description |
|-------|------|-------------|
| `total_cost_usd` | number | Total USD spent on LLM calls since kernel start |
| `max_cost_usd` | number | System-wide cost limit (hard cap) |
| `total_requests` | integer | Total LLM requests submitted to queue |
| `total_completed` | integer | Total LLM requests completed |

---

#### `GET /api/metrics`

Get a system overview with agent counts, state store size, and LLM status.

**Response** `200`
```json
{
  "state_store_size": 247,
  "audit_entries": 1042,
  "agent_count": 3,
  "llm": {
    "enabled": true,
    "current_cost_usd": 1.2345,
    "max_cost_usd": 50.00
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `state_store_size` | integer | Number of keys in the KV store |
| `audit_entries` | integer | Total audit log entries |
| `agent_count` | integer | Number of agents (all states) |
| `llm.enabled` | boolean | Whether LLM inference is enabled |
| `llm.current_cost_usd` | number | Total LLM cost so far |
| `llm.max_cost_usd` | number | System cost limit |

---

### 3.11 KV Store

#### `POST /api/store`

Write a key-value pair to the global state store.

**Request Body**
```json
{
  "key": "config:theme",
  "value": "dark"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `key` | string | **Yes** | Key name (any string) |
| `value` | string | No | Value to store |

**Response** `200`
```json
{ "success": true }
```

---

#### `GET /api/store/:key`

Read a value from the state store. The key is the URL path segment after `/api/store/`.

**Path Parameters**
| Param | Type | Description |
|-------|------|-------------|
| `key` | string | Key to look up (supports `/` in key names via URL path) |

**Response** `200`
```json
{
  "key": "config:theme",
  "value": "dark"
}
```

**Errors**
- `404` — `key not found`

---

### 3.12 Inference Config

#### `GET /api/inference`

Get the current LLM inference gateway configuration (model allowlists, provider lists, cost limits, etc.).

**Response** `200` — Returns the full gateway config object. Fields vary by configuration.

---

#### `PUT /api/inference`

Update inference gateway configuration (delta merge).

**Request Body** — Any subset of inference config fields (model allowlists, budget limits, enabled flag, etc.)

**Response** `200` — Returns the full updated config.

---

### 3.13 Privacy

#### `POST /api/privacy/scan`

Scan text for PII (Personally Identifiable Information). Does not redact -- only detects.

**Request Body**
```json
{
  "text": "Contact john@example.com or call 555-123-4567. SSN: 123-45-6789"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `text` | string | **Yes** | Text to scan for PII |

**Response** `200`
```json
{
  "contains_pii": true,
  "match_count": 3,
  "matches": [
    { "type": "email", "start": 8, "end": 24 },
    { "type": "phone", "start": 33, "end": 45 },
    { "type": "ssn", "start": 52, "end": 63 }
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `contains_pii` | boolean | Whether any PII was detected |
| `match_count` | integer | Number of PII matches |
| `matches` | array | Array of match objects |
| `matches[].type` | string | PII type: `email`, `phone`, `ssn`, `credit_card`, `ip_address` |
| `matches[].start` | integer | Start character offset |
| `matches[].end` | integer | End character offset |

---

### 3.14 Policy Recommendations

#### `GET /api/policy/recommendations`

Get auto-generated security policy recommendations based on observed agent behavior.

**Response** `200`
```json
[
  {
    "category": "NETWORK",
    "action": "HTTP",
    "resource": "api.github.com",
    "occurrence_count": 47,
    "suggested_change": "Add api.github.com to allowed_domains"
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `category` | string | Policy category |
| `action` | string | Action type observed |
| `resource` | string | Resource being accessed |
| `occurrence_count` | integer | How many times this was observed |
| `suggested_change` | string | Human-readable policy suggestion |

---

### 3.15 MCP (Model Context Protocol)

#### `GET /api/mcp/servers`

List connected MCP servers and their status.

**Response** `200`
```json
{
  "enabled": true,
  "servers": [
    {
      "name": "filesystem",
      "connected": true,
      "pid": 49001,
      "tool_count": 5,
      "total_calls": 23
    }
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Whether MCP bridge is active |
| `servers` | array | Connected MCP servers |
| `servers[].name` | string | Server name |
| `servers[].connected` | boolean | Connection status |
| `servers[].pid` | integer | Server process ID |
| `servers[].tool_count` | integer | Number of tools exposed by this server |
| `servers[].total_calls` | integer | Total tool invocations on this server |

If MCP is not enabled: `{"enabled":false,"servers":[]}`

---

#### `GET /api/mcp/tools`

List all tools available across all connected MCP servers.

**Response** `200`
```json
{
  "enabled": true,
  "tools": [
    {
      "server": "filesystem",
      "name": "read_file",
      "description": "Read the contents of a file"
    }
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Whether MCP bridge is active |
| `tools` | array | All available MCP tools |
| `tools[].server` | string | MCP server that provides this tool |
| `tools[].name` | string | Tool name |
| `tools[].description` | string | Tool description |

---

#### `POST /api/mcp/call`

Call a specific tool on an MCP server.

**Request Body**
```json
{
  "server": "filesystem",
  "tool": "read_file",
  "arguments": { "path": "/tmp/data.csv" }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `server` | string | **Yes** | MCP server name |
| `tool` | string | **Yes** | Tool name |
| `arguments` | object | No | Tool arguments (defaults to `{}`) |

**Response** `200`
```json
{
  "success": true,
  "content": "id,name,value\n1,foo,42\n2,bar,99\n",
  "duration_ms": 12,
  "error": ""
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | boolean | Whether the tool call succeeded |
| `content` | string | Tool output |
| `duration_ms` | integer | Execution time in milliseconds |
| `error` | string | (Only present on failure) Error message |

**Errors**
- `400` — `server and tool are required`
- `503` — `MCP bridge not enabled. Start with --mcp`

---

### 3.16 Worlds

#### `GET /api/worlds`

List all active worlds (shared environments for multi-agent collaboration).

**Response** `200`
```json
[
  {
    "id": 1,
    "name": "research-lab",
    "member_count": 3,
    "metadata": { "topic": "quantum computing" }
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | World ID |
| `name` | string | World name |
| `member_count` | integer | Number of agents in this world |
| `metadata` | object | Arbitrary metadata |

If world engine is not enabled: `{"enabled":false,"worlds":[]}`

---

#### `POST /api/worlds`

Create a new world.

**Request Body**
```json
{
  "name": "research-lab",
  "metadata": { "topic": "quantum computing" }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | **Yes** | World name |
| `metadata` | object | No | Arbitrary metadata (defaults to `{}`) |

**Response** `201`
```json
{
  "id": 1,
  "name": "research-lab"
}
```

**Errors**
- `400` — `name is required`
- `503` — `world engine not enabled`

---

#### `DELETE /api/worlds/:id`

Destroy a world.

**Path Parameters**
| Param | Type | Description |
|-------|------|-------------|
| `id` | integer | World ID |

**Response** `200`
```json
{ "success": true }
```

**Errors**
- `404` — `world not found`
- `503` — `world engine not enabled`

---

### 3.17 Replay (Execution Recording)

#### `GET /api/replay`

Get the current recording status.

**Response** `200`
```json
{
  "recording": false,
  "state": "idle",
  "entry_count": 0
}
```

| Field | Type | Description |
|-------|------|-------------|
| `recording` | boolean | Whether recording is active |
| `state` | string | One of: `recording`, `paused`, `idle` |
| `entry_count` | integer | Number of recorded entries |

---

#### `POST /api/replay/start`

Start recording all kernel operations (for later replay/debugging).

**Response** `200`
```json
{
  "success": true,
  "state": "recording"
}
```

If already recording: `{"success":false,"state":"already_recording"}`

---

#### `POST /api/replay/stop`

Stop recording.

**Response** `200`
```json
{
  "success": true,
  "entry_count": 247
}
```

---

### 3.18 Dashboard Fragments (HTMX)

These endpoints return **HTML fragments** (not JSON). They are used by the built-in HTMX dashboard for live updates. No auth required.

All responses have `Content-Type: text/html`.

| Endpoint | Description | Refresh use |
|----------|-------------|-------------|
| `GET /api/fragments/uptime` | Formatted uptime string (e.g. `Uptime: 01:02:15`) | Poll every second |
| `GET /api/fragments/agent-count` | Agent count text (e.g. `3 total`) | Poll every 2s |
| `GET /api/fragments/agents` | Agent sidebar list with name, ID, PID, state badges | Poll every 2s |
| `GET /api/fragments/agent-table` | Full agents table with metrics and kill buttons | Poll every 2s |
| `GET /api/fragments/metrics` | Metrics grid with agent count, running count, audit entries, state keys, LLM cost, uptime | Poll every 5s |
| `GET /api/fragments/inference` | Inference gateway status card with cost bar | Poll every 5s |
| `GET /api/fragments/audit` | Audit log table with timestamps, categories, and status badges | Poll on demand |

`GET /api/fragments/audit` accepts query parameters:
| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `category` | string | (all) | Filter by audit category |
| `limit` | integer | `100` | Max entries |

---

## 4. Dashboard Pages

These serve full HTML pages (not API responses). No auth required.

| Path | Description |
|------|-------------|
| `/` | Redirects to `/dashboard` |
| `/dashboard` | Main dashboard overview |
| `/dashboard/agents` | Agent management page |
| `/dashboard/audit` | Audit log viewer |
| `/dashboard/style.css` | Dashboard CSS stylesheet |

---

## 5. SSE Streaming Guide for Frontend Developers

### Connecting to a Run Stream

```javascript
const response = await fetch('/api/run/stream', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
    'Authorization': 'Bearer YOUR_KEY'
  },
  body: JSON.stringify({
    goal: 'Summarize the latest AI news',
    budget: 0.50,
    max_steps: 10
  })
});

const reader = response.body.getReader();
const decoder = new TextDecoder();
let buffer = '';

while (true) {
  const { done, value } = await reader.read();
  if (done) break;

  buffer += decoder.decode(value, { stream: true });
  const lines = buffer.split('\n\n');
  buffer = lines.pop(); // Keep incomplete chunk

  for (const line of lines) {
    if (line.startsWith('data: ')) {
      const event = JSON.parse(line.slice(6));

      switch (event.type) {
        case 'start':
          console.log('Agent started:', event.data);
          break;
        case 'thinking':
          // Show chain-of-thought in UI
          appendThinking(event.data.content);
          break;
        case 'tool_call':
          // Show tool being called
          appendToolCall(event.data.tool, event.data.args);
          break;
        case 'tool_result':
          // Show tool output
          appendToolResult(event.data.tool, event.data.result);
          break;
        case 'result':
          // Final result — update summary panel
          showResult(event.data);
          break;
      }
    }
  }
}
```

### Connecting to a Fleet Stream

```javascript
const response = await fetch('/api/fleet', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
    'Authorization': 'Bearer YOUR_KEY'
  },
  body: JSON.stringify({
    goal: 'Research quantum computing startups',
    agents: 5,
    budget: 2.00
  })
});

// Same reader pattern as above, but handle fleet-specific events:
// event.type === 'fleet_start'   -> Initialize progress bars for N agents
// event.type === 'agent_event'   -> Update specific agent's activity panel
// event.type === 'agent_done'    -> Mark agent complete, update progress (completed/total)
// event.type === 'synthesizing'  -> Show synthesis spinner
// event.type === 'fleet_done'    -> Show final merged result + cost breakdown
```

---

## 6. Example Flows

### 6.1 Submit a Run and Stream Results

```
1. POST /api/run/stream  { "goal": "...", "budget": 0.50 }
2. Read SSE events as they arrive:
   - "start" -> show spinner
   - "thinking" -> update thought panel
   - "tool_call" -> show tool name + args
   - "tool_result" -> show output
   - "result" -> show final answer, cost, token count
3. GET /api/run/{chain_id}  -> retrieve artifacts later
```

### 6.2 Show a Fleet Working in Real-Time

```
1. POST /api/fleet  { "goal": "...", "agents": 5, "budget": 2.00 }
2. On "fleet_start": render 5 agent cards with progress bars
3. On "agent_event": update the specific agent's card with live activity
4. On "agent_done": mark agent card as complete, show cost
5. On "synthesizing": show synthesis spinner
6. On "fleet_done": display merged synthesis, total cost, per-agent breakdown
```

### 6.3 Display Cost Dashboard

```
1. GET /api/cost -> total_cost_usd, max_cost_usd, total_requests
2. GET /api/metrics -> agent_count, state_store_size, llm status
3. GET /api/agents -> list all agents
4. For each agent: GET /api/agents/{id}/budget -> per-agent spend
5. Poll GET /api/cost every 5 seconds to update cost bar
```

### 6.4 Browse Run History

```
1. GET /api/history?limit=50 -> list of past runs
2. Render a table with chain_id, name, description, artifact_count, date
3. On row click: GET /api/run/{chain_id} -> show artifacts and details
```

### 6.5 Agent Management

```
1. GET /api/agents -> list all agents
2. POST /api/agents { "name": "worker", "command": "python3 worker.py" } -> spawn
3. GET /api/agents/{id}/metrics -> show resource usage
4. GET /api/agents/{id}/permissions -> show current permissions
5. PUT /api/agents/{id}/permissions { "can_http": true } -> grant HTTP access
6. POST /api/agents/{id}/budget { "max_cost_usd": 1.00 } -> set budget
7. DELETE /api/agents/{id} -> kill agent
```

### 6.6 Audit Log Viewer

```
1. GET /api/audit?limit=100 -> initial load
2. Note the highest entry ID
3. Poll: GET /api/audit?since_id={last_id}&limit=50 -> get new entries
4. Filter: GET /api/audit?category=SECURITY&limit=100
5. Export: GET /api/audit/export -> download JSONL file
```

---

## 7. Complete Endpoint Summary

| Method | Path | Auth | Content-Type | Description |
|--------|------|------|-------------|-------------|
| GET | `/api/health` | No | JSON | Health check |
| POST | `/api/think` | Yes | JSON | One-shot LLM call |
| POST | `/api/run` | Yes | JSON | Agent run (sync) |
| POST | `/api/run/stream` | Yes | SSE | Agent run (streaming) |
| GET | `/api/run/:chain_id` | Yes | JSON | Get past run details |
| POST | `/api/fleet` | Yes | SSE | Parallel fleet run (streaming) |
| GET | `/api/history` | Yes | JSON | List past runs |
| GET | `/api/agents` | Yes | JSON | List agents |
| POST | `/api/agents` | Yes | JSON | Spawn agent |
| DELETE | `/api/agents/:id` | Yes | JSON | Kill agent |
| GET | `/api/agents/:id/metrics` | Yes | JSON | Agent resource metrics |
| GET | `/api/agents/:id/permissions` | Yes | JSON | Get agent permissions |
| PUT | `/api/agents/:id/permissions` | Yes | JSON | Update agent permissions |
| GET | `/api/agents/:id/budget` | Yes | JSON | Get agent budget |
| POST | `/api/agents/:id/budget` | Yes | JSON | Set agent budget |
| GET | `/api/audit` | Yes | JSON | Query audit log |
| GET | `/api/audit/export` | Yes | NDJSON | Export audit log |
| GET | `/api/cost` | Yes | JSON | System cost summary |
| GET | `/api/metrics` | Yes | JSON | System metrics overview |
| POST | `/api/store` | Yes | JSON | Write KV store |
| GET | `/api/store/:key` | Yes | JSON | Read KV store |
| GET | `/api/inference` | Yes | JSON | Get inference config |
| PUT | `/api/inference` | Yes | JSON | Update inference config |
| POST | `/api/privacy/scan` | Yes | JSON | Scan text for PII |
| GET | `/api/policy/recommendations` | Yes | JSON | Policy suggestions |
| GET | `/api/mcp/servers` | Yes | JSON | List MCP servers |
| GET | `/api/mcp/tools` | Yes | JSON | List MCP tools |
| POST | `/api/mcp/call` | Yes | JSON | Call MCP tool |
| GET | `/api/worlds` | Yes | JSON | List worlds |
| POST | `/api/worlds` | Yes | JSON | Create world |
| DELETE | `/api/worlds/:id` | Yes | JSON | Destroy world |
| GET | `/api/replay` | Yes | JSON | Recording status |
| POST | `/api/replay/start` | Yes | JSON | Start recording |
| POST | `/api/replay/stop` | Yes | JSON | Stop recording |
| GET | `/api/fragments/uptime` | No | HTML | Uptime string |
| GET | `/api/fragments/agent-count` | No | HTML | Agent count |
| GET | `/api/fragments/agents` | No | HTML | Agent sidebar list |
| GET | `/api/fragments/agent-table` | No | HTML | Agent table |
| GET | `/api/fragments/metrics` | No | HTML | Metrics grid |
| GET | `/api/fragments/inference` | No | HTML | Inference status card |
| GET | `/api/fragments/audit` | No | HTML | Audit log table |
