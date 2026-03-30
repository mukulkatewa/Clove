# CLOVE MCP Server

Exposes CLOVE's orchestration primitives as MCP tools. Connect Claude Code, Cursor, or any MCP host to your CLOVE kernel.

## Setup

### Claude Code

Add to `~/.claude/settings.json`:

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

### Cursor / Other MCP Hosts

Same config pattern — point at the MCP server binary with stdio transport.

## Available Tools

| Tool | What it does |
|------|-------------|
| `clove_run_agent` | Run a single agent on a goal with budget/tool control |
| `clove_launch_fleet` | Run N agents in parallel on the same goal |
| `clove_launch_world` | Launch a multi-agent world from a template |
| `clove_list_agents` | See all running agents |
| `clove_list_worlds` | See all active worlds |
| `clove_create_world` | Create an isolated environment |
| `clove_get_cost` | Check total spend and budget |
| `clove_get_status` | Kernel health and uptime |
| `clove_query_audit` | Query the audit log |
| `clove_list_memory` | Browse agent memory blocks |
| `clove_mcp_tools` | List external MCP tools available |

## Requirements

CLOVE kernel must be running (`clove start` or manual launch on port 8080).
