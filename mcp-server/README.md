# CLOVE MCP Server

Exposes CLOVE's full orchestration API as MCP tools. Connect Claude Code, Cursor, or any MCP host to a running CLOVE kernel — local or remote.

**60+ tools** covering agent execution, memory, jobs, daemons, worlds, governance, and more.

---

## Modes

| Mode | When | Transport |
|------|------|-----------|
| **stdio** | Local Claude Code / any local MCP host | stdin/stdout |
| **HTTP** | Remote deployment (Railway, Fly, VPS) | StreamableHTTP on `PORT` |

---

## Local Setup (stdio)

### 1. Start the kernel

```bash
clove start
# or: ./build/kernel/clove_kernel --api --api-port 8080
```

### 2. Add to Claude Code (`~/.claude/settings.json`)

```json
{
  "mcpServers": {
    "clove": {
      "command": "node",
      "args": ["/path/to/clove-v2/mcp-server/dist/index.js"],
      "env": {
        "CLOVE_API_URL": "http://localhost:8080"
      }
    }
  }
}
```

### 3. Or run via npx

```bash
npx @cloveos/mcp-server
```

---

## Remote Setup (HTTP mode — Railway / self-hosted)

Set `PORT` to enable HTTP transport. The server listens at `/mcp`.

```bash
PORT=3001 CLOVE_API_URL=http://kernel:8080 CLOVE_MCP_KEY=my-secret node dist/index.js
```

### Add to Claude Code (remote)

```json
{
  "mcpServers": {
    "clove": {
      "url": "https://your-mcp.up.railway.app/mcp",
      "headers": {
        "Authorization": "Bearer YOUR_CLOVE_MCP_KEY"
      }
    }
  }
}
```

### Authentication (HTTP mode)

Send one of:
- `Authorization: Bearer <CLOVE_MCP_KEY>` header
- `x-api-key: <CLOVE_MCP_KEY>` header

If `CLOVE_MCP_KEY` is unset, auth is disabled (not recommended in production).

The `/health` endpoint is always public (used by Railway health checks).

---

## Deploy with Docker

```bash
# Build
docker build -t clove-mcp ./mcp-server

# Run (pointing at a running kernel)
docker run -p 3001:3001 \
  -e PORT=3001 \
  -e CLOVE_API_URL=http://host.docker.internal:8080 \
  -e CLOVE_MCP_KEY=my-secret \
  clove-mcp
```

Or use the full stack:

```bash
cd deploy && docker-compose up   # starts kernel + mcp together
```

---

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `CLOVE_API_URL` | `http://localhost:8080` | URL of the CLOVE kernel |
| `PORT` | (unset) | Set to enable HTTP mode. Leave unset for stdio. |
| `CLOVE_MCP_KEY` | (unset) | Auth token for HTTP mode |

See `mcp-server/.env.example` for a template.

---

## Available Tools

### Status & Metrics
| Tool | What it does |
|------|-------------|
| `clove_status` | Kernel health, uptime, version |
| `clove_cost` | Total spend, per-agent cost breakdown |
| `clove_metrics` | System metrics (CPU, memory, agent counts) |

### Agent Execution
| Tool | What it does |
|------|-------------|
| `clove_run` | Run an agent on a goal (goal, budget, tools, model) |
| `clove_run_stream` | Same with SSE streaming |
| `clove_fleet` | Run N agents in parallel, kernel synthesizes results |
| `clove_think` | Single LLM call with PII filtering |

### Async Jobs
| Tool | What it does |
|------|-------------|
| `clove_submit_job` | Submit async job (returns job_id immediately) |
| `clove_get_job` | Get job status + step log |
| `clove_list_jobs` | List jobs (filter by status, workspace) |
| `clove_cancel_job` | Cancel queued job |
| `clove_retry_job` | Re-queue failed job |

### Runtime Agents (live processes)
| Tool | What it does |
|------|-------------|
| `clove_list_agents` | List all running agents |
| `clove_spawn_agent` | Spawn a new agent process |
| `clove_kill_agent` | Kill a running agent |
| `clove_message_agent` | Send IPC message to agent |
| `clove_broadcast` | Broadcast to all agents |

### Agent Definitions
| Tool | What it does |
|------|-------------|
| `clove_define_agent` | Create/update agent definition |
| `clove_list_agent_defs` | List agent definitions |
| `clove_update_agent_def` | Update agent definition |

### Daemon Agents (always-on)
| Tool | What it does |
|------|-------------|
| `clove_list_daemons` | List running daemons with uptime + ticks |
| `clove_start_daemon` | Start agent as always-on daemon |
| `clove_stop_daemon` | Stop daemon |
| `clove_daemon_dream` | Trigger memory consolidation cycle |

### Memory
| Tool | What it does |
|------|-------------|
| `remember` | Store a named memory block (type: system/core/recall, access: private/shared_read/shared_readwrite) |
| `recall` | Retrieve memory blocks, optionally filtered by query |
| `clove_write_memory` | Create or update memory block by ID |
| `clove_delete_memory` | Delete a memory block |
| `share` | Share a memory block with another agent |
| `artifact` | Create a typed artifact (QUERY/RESEARCH/ANALYSIS/REPORT) |

### Governance & Audit
| Tool | What it does |
|------|-------------|
| `clove_audit` | Query audit log (filter by category, agent, limit) |
| `clove_set_policy` | Update inference policy (model allowlist, cost limit) |
| `clove_get_policy` | Get current policy |
| `clove_privacy_scan` | Scan text for PII |

### Worlds (Multi-tenant isolation)
| Tool | What it does |
|------|-------------|
| `clove_create_world` | Create isolated world (own state + events) |
| `clove_list_worlds` | List all worlds |
| `clove_launch_world` | Launch world from template |

### Schedules & Webhooks
| Tool | What it does |
|------|-------------|
| `clove_list_schedules` | List cron schedules |
| `clove_list_webhooks` | List registered webhooks |
| `clove_create_webhook` | Register webhook |
| `clove_delete_webhook` | Remove webhook |

### Search
| Tool | What it does |
|------|-------------|
| `clove_web_search` | Web search |
| `clove_google_scholar` | Academic paper search |

### MCP Integration
| Tool | What it does |
|------|-------------|
| `clove_mcp_servers` | List connected MCP servers |
| `clove_mcp_tools` | List available MCP tools |
| `clove_mcp_call` | Call an MCP tool via kernel |

---

## MCP Resources

| URI | What it returns |
|-----|----------------|
| `clove://memory/<id>` | Memory block contents |
| `clove://trace/<chain_id>` | Full execution trace |

---

## Requirements

- Node.js 22+
- CLOVE kernel running and reachable at `CLOVE_API_URL`

```bash
# Build from source
cd mcp-server && npm install && npm run build

# Run (stdio, local)
node dist/index.js

# Run (HTTP, remote)
PORT=3001 node dist/index.js
```
