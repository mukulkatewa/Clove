# CLOVE MCP Server

Connect Claude Code — or any MCP-compatible AI tool — to a full AI agent OS. Run persistent agents, chain async jobs, manage memory, and orchestrate multi-agent pipelines without leaving your editor.

**60+ tools.** Persistent agents · async job pipelines · sandboxed execution · A2A · multi-model routing.

---

## Quickstart (30 seconds)

Add this to your Claude Code MCP settings (`~/.claude/mcp.json`):

```json
{
  "mcpServers": {
    "clove": {
      "command": "npx",
      "args": ["-y", "@cloveos/mcp-server"],
      "env": {
        "CLOVE_KERNEL_URL": "https://kernel-production-96de.up.railway.app",
        "CLOVE_API_KEY": "clove-prod-70f36e07a895a89c1fd82b84ec35a4d7"
      }
    }
  }
}
```

Restart Claude Code. Done — you now have a full agent OS connected.

---

## Hosted Endpoints

| Service | URL |
|---------|-----|
| Kernel API | `https://kernel-production-96de.up.railway.app` |
| MCP (HTTP) | `https://mcp-production-07a6.up.railway.app/mcp` |
| Health | `https://kernel-production-96de.up.railway.app/api/health` |

---

## Using the hosted MCP directly (Cursor, Zed, etc.)

```json
{
  "mcpServers": {
    "clove": {
      "url": "https://mcp-production-07a6.up.railway.app/mcp"
    }
  }
}
```

---

## Self-hosting

Deploy your own kernel on Railway, Fly.io, or any VPS.

**Kernel env vars:**
| Variable | Purpose |
|----------|---------|
| `CLOVE_API_KEY` | REST API auth key |
| `OPENROUTER_API_KEY` | 300+ models via OpenRouter |
| `ANTHROPIC_API_KEY` | Native Anthropic API |

**MCP server env vars:**
| Variable | Purpose |
|----------|---------|
| `CLOVE_KERNEL_URL` | URL of your kernel (internal or public) |
| `CLOVE_API_KEY` | Must match kernel's API key |
| `CLOVE_MCP_KEY` | Optional: guards the MCP endpoint itself |

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
| `clove_get_policy` | Get current policy |
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
| `clove_create_webhook` | Register a webhook |
| `clove_delete_webhook` | Remove a webhook |

### Status & Metrics
| Tool | Description |
|------|-------------|
| `clove_status` | Kernel health, uptime, version |
| `clove_cost` | Total spend and per-agent cost breakdown |
| `clove_metrics` | System metrics |

### MCP Passthrough
| Tool | Description |
|------|-------------|
| `clove_mcp_servers` | List MCP servers connected to the kernel |
| `clove_mcp_call` | Call any MCP tool through the kernel |

---

## Requirements

- Node.js 22+
- A running CLOVE kernel (use the hosted one above, or self-host)
