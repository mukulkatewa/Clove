# CLOVE MCP Server

Connect Claude Code — or any MCP-compatible AI tool — directly to a CLOVE kernel. Run agents, manage memory, schedule jobs, and orchestrate fleets without leaving your editor.

**60+ tools** covering agent execution, memory, async jobs, daemons, workspaces, governance, search, and more.

---

## Quickstart

### Use the hosted server

The fastest way to get started. No setup required — connect to a running CLOVE instance:

```json
{
  "mcpServers": {
    "clove": {
      "url": "https://your-clove.up.railway.app/mcp",
      "headers": {
        "Authorization": "Bearer YOUR_API_KEY"
      }
    }
  }
}
```

Add this to your Claude Code settings (`~/.claude/settings.json`) and you're done.

---

### Run locally

If you're running a CLOVE kernel on your own machine:

**1. Start the kernel**
```bash
clove start
```

**2. Add to Claude Code**
```json
{
  "mcpServers": {
    "clove": {
      "command": "npx",
      "args": ["@cloveos/mcp-server"],
      "env": {
        "CLOVE_API_URL": "http://localhost:8080"
      }
    }
  }
}
```

That's it. Claude Code will start the MCP server automatically when needed.

---

## Self-hosting

Deploy CLOVE on Railway, Fly.io, or any VPS and share access with your team.

### Deploy on Railway

[![Deploy on Railway](https://railway.app/button.svg)](https://railway.app)

Set these environment variables on your Railway service:

| Variable | Value |
|----------|-------|
| `PORT` | `3001` |
| `CLOVE_API_URL` | Internal URL of your kernel service |
| `CLOVE_MCP_KEY` | A strong secret key you choose |

Railway automatically provisions HTTPS. Your team connects via:
```
https://your-mcp.up.railway.app/mcp
```

### Docker

```bash
docker run -p 3001:3001 \
  -e PORT=3001 \
  -e CLOVE_API_URL=http://your-kernel:8080 \
  -e CLOVE_MCP_KEY=your-secret-key \
  ghcr.io/cloveos/mcp-server
```

---

## Authentication

All connections to a hosted CLOVE MCP server require an API key. Pass it as a bearer token:

```
Authorization: Bearer YOUR_CLOVE_MCP_KEY
```

Every major MCP client — Claude Code, Cursor, Zed — supports bearer token auth in their settings. If you're self-hosting, set `CLOVE_MCP_KEY` to any secret string you choose. If it's not set, the server accepts all connections (fine for local use, not for production).

---

## Available Tools

### Agent Execution
| Tool | Description |
|------|-------------|
| `clove_run` | Run an agent on a goal with budget, tools, and model |
| `clove_run_stream` | Same, with streaming output |
| `clove_fleet` | Run multiple agents in parallel and synthesize results |
| `clove_think` | Single LLM call with PII filtering |

### Async Jobs
| Tool | Description |
|------|-------------|
| `clove_submit_job` | Submit a job and get an ID back immediately |
| `clove_get_job` | Check job status and step log |
| `clove_list_jobs` | List jobs, filter by status or workspace |
| `clove_cancel_job` | Cancel a queued job |
| `clove_retry_job` | Re-queue a failed job |

### Live Agents
| Tool | Description |
|------|-------------|
| `clove_list_agents` | List all running agents |
| `clove_spawn_agent` | Spawn a new agent process |
| `clove_kill_agent` | Stop a running agent |
| `clove_message_agent` | Send a message to an agent |
| `clove_broadcast` | Broadcast a message to all agents |

### Agent Definitions
| Tool | Description |
|------|-------------|
| `clove_define_agent` | Create or update an agent definition |
| `clove_list_agent_defs` | List all agent definitions |
| `clove_update_agent_def` | Update an existing definition |

### Daemon Agents
| Tool | Description |
|------|-------------|
| `clove_list_daemons` | List running daemons with uptime and tick count |
| `clove_start_daemon` | Start an agent as an always-on daemon |
| `clove_stop_daemon` | Stop a daemon |
| `clove_daemon_dream` | Trigger a memory consolidation cycle |

### Memory
| Tool | Description |
|------|-------------|
| `remember` | Store a named memory block |
| `recall` | Retrieve memory blocks, optionally filtered by query |
| `clove_write_memory` | Create or update a memory block by ID |
| `clove_delete_memory` | Delete a memory block |
| `share` | Share a memory block with another agent |
| `artifact` | Create a typed artifact (research, analysis, report) |

### Governance & Audit
| Tool | Description |
|------|-------------|
| `clove_audit` | Query the audit log |
| `clove_set_policy` | Update inference policy — model allowlist, cost limits |
| `clove_get_policy` | Get the current policy |
| `clove_privacy_scan` | Scan text for PII |

### Workspaces
| Tool | Description |
|------|-------------|
| `clove_create_world` | Create an isolated workspace |
| `clove_list_worlds` | List all workspaces |
| `clove_launch_world` | Launch a workspace from a template |

### Schedules & Webhooks
| Tool | Description |
|------|-------------|
| `clove_list_schedules` | List cron schedules |
| `clove_list_webhooks` | List registered webhooks |
| `clove_create_webhook` | Register a webhook |
| `clove_delete_webhook` | Remove a webhook |

### Search
| Tool | Description |
|------|-------------|
| `clove_web_search` | Web search |
| `clove_google_scholar` | Academic paper search |

### Status & Metrics
| Tool | Description |
|------|-------------|
| `clove_status` | Kernel health, uptime, version |
| `clove_cost` | Total spend and per-agent cost breakdown |
| `clove_metrics` | System metrics — CPU, memory, agent counts |

### MCP Passthrough
| Tool | Description |
|------|-------------|
| `clove_mcp_servers` | List MCP servers connected to the kernel |
| `clove_mcp_tools` | List tools available via the kernel's MCP connections |
| `clove_mcp_call` | Call any MCP tool through the kernel |

---

## Resources

| URI | Returns |
|-----|---------|
| `clove://memory/<id>` | Memory block contents |
| `clove://trace/<chain_id>` | Full execution trace |

---

## Requirements

- Node.js 22+
- A running CLOVE kernel (local or hosted)

```bash
# Install and run locally
npm install -g @cloveos/mcp-server
npx @cloveos/mcp-server
```
