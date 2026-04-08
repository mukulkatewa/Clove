# CLOVE MCP Setup

Connect Claude Code to the CLOVE kernel. One command. No auth required.

---

## Install

```bash
npx @cloveos/mcp-server setup
```

That's it. This command:
1. Installs the CLOVE skill into `~/.claude/skills/clove/` — Claude Code becomes natively aware of CLOVE
2. Wires the MCP server into Claude Code via `claude mcp add` (or writes `.mcp.json` if the CLI isn't available)

Then open any project in Claude Code and ask:

> "check clove status"

You'll get a live response from the kernel. No extra prompting, no "use clove" prefix needed.

---

## What gets installed

### The skill (`~/.claude/skills/clove/SKILL.md`)
Teaches Claude Code everything about CLOVE — all tools, when to use them, and the core principle that CLOVE does the heavy work. Auto-triggers on phrases like "run an agent", "submit a job", "scale workflows", etc.

### The MCP server connection
Points Claude Code at the hosted CLOVE kernel on Railway:
```
https://mcp-production-07a6.up.railway.app/mcp
```
No API keys. No tokens. Open to the team.

---

## Verify it's working

```
"check clove status"
→ CLOVE v2.0.0 | ok | uptime 902s | 86 syscalls | $0.000000 spent
```

---

## What you can do once connected

Claude Code routes agent work through CLOVE automatically:

| Say this | What happens |
|----------|-------------|
| "run an agent to summarize the last 5 PRs" | `clove_run` → result |
| "submit a job to audit the codebase for security issues" | `clove_submit_job` → job_id to poll |
| "start the incident-monitor daemon" | `clove_start_daemon` |
| "what's in memory?" | `recall` → all memory blocks |
| "remember that the auth bug is in libs/api" | `remember` → stored |
| "delete memory block mem_abc123" | `clove_delete_memory` |
| "run 10 agents in parallel on this goal" | `clove_fleet` |
| "define a reusable code-reviewer agent" | `clove_define_agent` |

---

## Other AI clients (Cursor, Windsurf, VS Code, Claude Desktop)

The setup command detects which clients you have installed and configures them all:

```bash
npx @cloveos/mcp-server setup
```

Or target a specific client:

```bash
npx @cloveos/mcp-server setup --claude      # Claude Code
npx @cloveos/mcp-server setup --cursor      # Cursor
npx @cloveos/mcp-server setup --windsurf    # Windsurf
npx @cloveos/mcp-server setup --vscode      # VS Code (Copilot / Continue.dev)
npx @cloveos/mcp-server setup --desktop     # Claude Desktop app
npx @cloveos/mcp-server setup --prompt      # Print system prompt only (for any client)
```

### What each client gets

| Client | What's installed |
|--------|-----------------|
| **Claude Code** | Skill file (`~/.claude/skills/clove/SKILL.md`) — auto-triggers, no prompting needed |
| **Claude Desktop** | MCP server entry in `claude_desktop_config.json` |
| **Cursor** | MCP config in `~/.cursor/mcp.json` + system prompt for `.cursorrules` |
| **Windsurf** | MCP config in `~/.windsurf/mcp.json` + system prompt for `.windsurfrules` |
| **VS Code** | MCP server in `~/.vscode/settings.json` (requires MCP extension or Continue.dev) |
| **Any other client** | `--prompt` prints the system prompt to paste anywhere |

### System prompt (for any client that doesn't support skills)

If your client supports a custom system prompt or rules file, run:

```bash
npx @cloveos/mcp-server setup --prompt
```

Paste the output into your client's system prompt / rules file. It tells the AI what CLOVE is, all its tools, and when to use them.

---

## Manual install (if npx doesn't work)

**Step 1 — Add the MCP server to Claude Code:**

```bash
claude mcp add clove -s user -- npx -y mcp-remote https://mcp-production-07a6.up.railway.app/mcp --allow-http
```

Or add to `.mcp.json` in your project root:

```json
{
  "mcpServers": {
    "clove": {
      "command": "npx",
      "args": ["-y", "mcp-remote", "https://mcp-production-07a6.up.railway.app/mcp", "--allow-http"]
    }
  }
}
```

**Step 2 — Install the skill:**

```bash
mkdir -p ~/.claude/skills/clove
# Copy SKILL.md from this repo:
cp docs/clove-skill.md ~/.claude/skills/clove/SKILL.md
```

---

## Connecting to a local kernel

If you're running the kernel locally (`./start.sh`):

```bash
# Point the MCP server at localhost instead
claude mcp add clove -s project -- npx -y mcp-remote http://localhost:8080/mcp --allow-http
```

Or edit `.mcp.json`:
```json
{
  "mcpServers": {
    "clove": {
      "command": "npx",
      "args": ["-y", "mcp-remote", "http://localhost:8080/mcp", "--allow-http"]
    }
  }
}
```

---

## Managing memory

| Action | What to say |
|--------|-------------|
| Read all memory | "what's in clove memory?" |
| Search memory | "recall anything about [topic]" |
| Write a memory | "remember that X" |
| Delete a memory | "delete memory block mem_abc123" |

Memory IDs come from `recall` — each block shows its ID in the response.

Memory types:
- `system` — pinned, always visible to every agent
- `core` — automatically included in every agent's context
- `recall` — fetched on-demand by search query

---

## Publishing a new version

When the skill or installer changes:

```bash
cd mcp-server
npm version patch
npm publish --access public
```

Users re-run `npx @cloveos/mcp-server setup` to get the latest skill.

---

## Troubleshooting

| Issue | Fix |
|-------|-----|
| "No MCP tools available" | Re-open the project in Claude Code — MCP connects on project open |
| Kernel not responding | Check Railway service status, or run locally with `./start.sh` |
| `npx mcp-remote` not found | Ensure Node 18+ is installed |
| Tool calls time out | Hosted kernel may be cold-starting — retry after 10 seconds |
| `claude mcp add` not found | Install Claude Code CLI: `npm install -g @anthropic-ai/claude-code` |
