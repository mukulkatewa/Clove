# Creating Agents in CLOVE

This guide walks through creating a working agent from scratch — from connecting external services to deploying a persistent, triggered agent.

## Prerequisites

```bash
npm install -g @cloveos/cli
clove start
```

Verify the kernel is running:
```bash
clove status
```

## Step 1: Connect Services

Agents need to talk to external systems. CLOVE uses MCP (Model Context Protocol) to connect to services like GitHub, Slack, databases, and more.

```bash
# Connect GitHub (needs a Personal Access Token)
clove connect github ghp_your_token_here

# Connect Slack (needs a Bot Token)
clove connect slack xoxb-your-token-here

# Connect a filesystem path
clove connect filesystem

# Connect PostgreSQL
clove connect postgres postgresql://user:pass@host:5432/db

# See all connections
clove connect list
```

Each connection configures an MCP server that agents can use. Connections are stored in `~/.clove/connections.json` and `~/.clove/mcp.yaml`.

**Available services:** github, slack, filesystem, postgres, notion, google_drive

After connecting, restart the kernel to activate MCP servers:
```bash
clove stop && clove start
```

## Step 2: Create an Agent

```bash
clove agent create pr-reviewer
```

This creates `~/.clove/agents/pr-reviewer/agent.json`. Edit it:

```json
{
  "name": "pr-reviewer",
  "description": "Reviews new PRs for security and code quality",
  "enabled": false,
  "connections": ["github"],
  "triggers": [
    {
      "type": "webhook",
      "source": "github",
      "event_type": "pull_request.opened"
    }
  ],
  "action": {
    "goal": "A new PR was opened: {{event}}. Read the changed files via GitHub MCP. Review for security issues, code quality, and correctness. Post a review comment with findings.",
    "tools": ["read_file", "exec", "mcp_call"],
    "max_steps": 15
  },
  "permissions": {
    "can_exec": true,
    "can_read": true,
    "can_write": false,
    "can_http": true,
    "allowed_domains": ["api.github.com"],
    "allowed_paths": []
  },
  "budget": {
    "per_run": 0.30,
    "daily_max": 10.00,
    "daily_spent": 0,
    "last_reset": "2026-03-31"
  },
  "memory": [],
  "created_at": "2026-03-31T00:00:00Z",
  "updated_at": "2026-03-31T00:00:00Z"
}
```

### Agent Definition Fields

| Field | Description |
|-------|-------------|
| `name` | Unique identifier |
| `description` | What this agent does |
| `enabled` | Whether it responds to triggers |
| `connections` | Which services it can use (must be connected first) |
| `triggers` | What activates it: `cron`, `webhook`, `event`, or `manual` |
| `action.goal` | The prompt sent to the LLM. Use `{{event}}` for trigger context |
| `action.tools` | Which kernel tools the agent can call |
| `action.max_steps` | Max tool-calling iterations per run |
| `permissions` | Sandbox constraints — filesystem paths, domains, capabilities |
| `budget.per_run` | Max cost per trigger in USD |
| `budget.daily_max` | Max daily spend in USD |

### Trigger Types

**Manual** — Run explicitly with `clove agent run <name>`
```json
{ "type": "manual" }
```

**Cron** — Run on a schedule
```json
{ "type": "cron", "schedule": "0 9 * * MON-FRI" }
```

**Webhook** — Run when an external event arrives
```json
{ "type": "webhook", "source": "github", "event_type": "pull_request.opened" }
```

**Event** — Run on internal kernel events
```json
{ "type": "event", "event_type": "BUDGET_EXCEEDED" }
```

## Step 3: Test Your Agent

Run it manually first:
```bash
clove agent run pr-reviewer
```

Check the output. If it works, enable it:
```bash
clove agent enable pr-reviewer
```

## Step 4: Monitor

```bash
# See agent details
clove agent show pr-reviewer

# Check all agents
clove agent list

# Check audit log for agent activity
clove logs 20

# Check cost
clove status
```

Or use the dashboard at `http://localhost:3000`.

## Example Agents

### API Health Monitor
```json
{
  "name": "api-monitor",
  "description": "Checks API health every 5 minutes",
  "enabled": true,
  "triggers": [{ "type": "cron", "schedule": "*/5 * * * *" }],
  "action": {
    "goal": "Check https://api.example.com/health. Report status code, response time. If unhealthy, store alert under 'alerts:api' in the kernel state.",
    "tools": ["http", "store"],
    "max_steps": 5
  },
  "permissions": { "can_http": true, "allowed_domains": ["api.example.com"] },
  "budget": { "per_run": 0.05, "daily_max": 2.00 }
}
```

### Daily News Digest
```json
{
  "name": "news-digest",
  "description": "Compiles AI news every morning",
  "enabled": true,
  "triggers": [{ "type": "cron", "schedule": "0 8 * * MON-FRI" }],
  "action": {
    "goal": "Search for the latest AI news from today. Compile a digest with: top 5 stories, key developments, and any notable funding rounds. Be concise.",
    "tools": ["search", "http", "remember"],
    "max_steps": 12
  },
  "budget": { "per_run": 0.40, "daily_max": 3.00 }
}
```

### Slack Support Bot
```json
{
  "name": "support-bot",
  "description": "Answers support questions in Slack",
  "enabled": true,
  "connections": ["slack"],
  "triggers": [{ "type": "webhook", "source": "slack", "filter": "channel=#support" }],
  "action": {
    "goal": "A support question was received: {{event}}. Search knowledge base, draft a response. Post it via Slack MCP.",
    "tools": ["search", "recall", "mcp_call"],
    "max_steps": 8
  },
  "budget": { "per_run": 0.10, "daily_max": 5.00 }
}
```

## Using Templates Instead

For common agent types, use pre-built templates:

```bash
# List available templates
clove templates

# Deploy a template
clove deploy research-digest --param topic="AI infrastructure"

# Deploy a fleet (parallel agents)
clove deploy competitor-tracker --param company="Acme" --param competitors="Foo, Bar"
```

## Multi-Agent Worlds

For complex tasks requiring coordinated agents:

```bash
# Launch a code health check (3 agents + synthesizer)
clove world launch code-health --param project_path=./my-app
```

See `examples/worlds/` for world template examples.

## Architecture

```
clove connect github    →  ~/.clove/connections.json + ~/.clove/mcp.yaml
clove agent create X    →  ~/.clove/agents/X/agent.json
clove agent enable X    →  agent.enabled = true
                           ↓
                    Agent Registry (port 8090)
                           ↓
              Cron fires / Webhook arrives / Manual trigger
                           ↓
              POST to kernel /api/run with agent's goal + tools + budget
                           ↓
              Kernel: LLM → tool calls → audit → result
                           ↓
              Cost recorded, daily budget updated
```

Every agent action goes through the kernel. The kernel enforces permissions, tracks cost, logs to audit, and manages the sandbox. The agent registry is a sidecar that manages persistence and triggers.
