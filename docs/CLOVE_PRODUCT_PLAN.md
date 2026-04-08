# CLOVE Product Plan — Making It Really Good

## The North Star

A user connects GitHub, describes what they want, and CLOVE builds a multi-runtime pipeline that runs autonomously. The PR Fixer is the first pipeline:

```
GitHub PR opened → CLOVE reviews (via DeepWiki context) → Claude Code fixes → Codex verifies build → Supervisor posts summary
```

Four different runtimes. Shared context. Automatic trigger. Real result.

Everything we build serves this experience.

---

## User Journey

### Day 0: Install

```
npm install -g @cloveos/cli
clove start
```

Dashboard opens. User sees: "Welcome to CLOVE. Connect a service to get started."

Not an empty dashboard with 20 sidebar items. One clear call to action.

### Day 0: Connect

Dashboard shows 4 service cards: GitHub, Slack, DeepWiki, Custom.

User clicks GitHub → pastes token → connected. Green checkmark. Done.

Behind the scenes: MCP server configured, webhook URL generated, kernel reloads config.

### Day 0: First Pipeline

User sees: "What do you want to automate?"

They pick from templates or describe in natural language:
- "Review and fix my PRs automatically"
- "Monitor my API and alert Slack when it's down"
- "Scan my codebase for security issues weekly"

They pick "Review and fix my PRs."

CLOVE shows the pipeline it will create:

```
┌─────────┐    ┌─────────┐    ┌─────────┐    ┌──────────┐
│ Reviewer │ →  │  Fixer  │ →  │Verifier │ →  │Supervisor│
│  CLOVE   │    │Claude   │    │ Codex   │    │  CLOVE   │
│          │    │ Code    │    │         │    │          │
└─────────┘    └─────────┘    └─────────┘    └──────────┘
     ↑
  GitHub
  Webhook
```

User confirms. Pipeline is live.

### Day 1+: It Works

A PR is opened. User gets a notification. Dashboard shows:
- Pipeline triggered by PR #47
- Reviewer running... found 3 issues
- Fixer running... applied fixes
- Verifier running... build passes ✓
- Supervisor posting summary to PR...
- Done. $0.80. 4 minutes.

User clicks into the pipeline run. Sees each step's output. Sees the context flowing between agents. Sees the cost per step. Sees the audit trail.

---

## What the Dashboard Needs

### Home Page

NOT 20 empty pages. ONE page with:

1. **Status bar** — kernel running, 3 agents active, $4.28 spent today
2. **Active pipelines** — cards showing running/recent pipeline executions
3. **Quick actions** — "New Pipeline", "Run Agent", "Connect Service"
4. **Agent activity feed** — what happened recently across all agents

### Pipeline View (THE core page)

This is the product. A pipeline is a sequence of steps, each using a runtime.

**Creation mode:**
- Visual flow builder (we have this in /pipelines)
- Each node: name, runtime (CLOVE/Claude Code/Codex/OpenClaw), role, tools, model
- Connections between nodes = data flow
- Trigger on the left (webhook, cron, manual)
- "Save & Enable" button

**Execution mode:**
- Same flow diagram but live
- Each node lights up when active
- Data flowing between nodes shown as animated dots
- Click a node to see its output, tool calls, cost
- Overall progress bar
- Cost accumulating in real time

**History:**
- Past runs as a list below the diagram
- Click to see full execution details

### Agent Detail

When you click an agent (in a pipeline or standalone):
- Its role and runtime
- Model it uses
- Connections it has access to
- Budget (per run + daily)
- Run history with cost and success rate
- Memory blocks it owns
- Sandbox permissions

### Settings (simplified)

One page, tabs:
- **Providers** — API keys for OpenRouter/Anthropic/OpenAI/Google
- **Connections** — MCP servers (GitHub, Slack, DeepWiki, etc.)
- **Runtimes** — installed runtimes and their status
- **Budget** — system-wide cost cap
- **Privacy** — PII mode (audit/redact/block)

---

## What the Kernel Needs

### Pipeline Execution Engine

New endpoint: `POST /api/pipeline/run`

```json
{
  "name": "pr-fixer",
  "trigger": {"type": "webhook", "source": "github", "event": "pull_request.opened"},
  "context": {"pr_number": 47, "repo": "aniiiiXD/Clove"},
  "steps": [
    {
      "name": "reviewer",
      "runtime": "clove",
      "model": "claude-sonnet-4",
      "role": "Review the PR diff. Check DeepWiki for repo context. List all issues found.",
      "tools": ["mcp_call", "search", "remember"],
      "budget": 0.15,
      "output_key": "review_findings"
    },
    {
      "name": "fixer",
      "runtime": "claude-code",
      "model": "claude-sonnet-4",
      "role": "Fix the issues found by the reviewer.",
      "depends_on": ["reviewer"],
      "input_keys": ["review_findings"],
      "budget": 0.40,
      "output_key": "fix_changes"
    },
    {
      "name": "verifier",
      "runtime": "codex",
      "role": "Run the build and tests. Report pass/fail.",
      "depends_on": ["fixer"],
      "input_keys": ["fix_changes"],
      "budget": 0.20,
      "output_key": "verify_result"
    },
    {
      "name": "supervisor",
      "runtime": "clove",
      "model": "claude-haiku-4",
      "role": "Compile findings, changes, and verification into a PR comment. Post via GitHub MCP.",
      "depends_on": ["verifier"],
      "input_keys": ["review_findings", "fix_changes", "verify_result"],
      "tools": ["mcp_call", "recall"],
      "budget": 0.05,
      "output_key": "summary"
    }
  ]
}
```

**Execution flow:**

1. Create a world for this pipeline run
2. Store trigger context in world state
3. For each step (respecting depends_on order):
   a. Read input_keys from world state
   b. Build goal = role + input context
   c. Dispatch to the right runtime:
      - "clove" → RunEngine.execute()
      - "claude-code" → spawn `claude -p "goal" --output-format json`
      - "codex" → spawn `codex exec "goal" --json`
      - "openclaw" → OpenClawManager.spawn()
   d. Capture output
   e. Store output in world state under output_key
   f. Emit SSE event: step_started, step_completed, step_failed
4. If a step fails and has retries configured → retry
5. When all steps complete → emit pipeline_done
6. Keep world alive for inspection

### World State as the Shared Context

This is how agents share information:

```
World: "pr-fix-47"
State:
  trigger_context: {pr_number: 47, repo: "...", diff: "..."}
  review_findings: "Found 3 issues: 1) SQL injection in auth.py..."
  fix_changes: "Modified 2 files: auth.py (fixed SQL injection), handler.py..."
  verify_result: "Build: PASS. Tests: 47/47 pass. Lint: clean."
  summary: "PR #47 review complete. 3 issues found and fixed..."
```

Each agent reads its `input_keys` from world state before running.
Each agent writes its output to `output_key` in world state after running.
The supervisor reads everything.

### Runtime Dispatch

The pipeline engine has a dispatcher:

```
switch (step.runtime):
  "clove":
    result = RunEngine.execute(goal_with_context, model, budget, tools)

  "claude-code":
    goal_with_context = step.role + "\n\nContext:\n" + input_context
    result = spawn("claude -p '{goal}' --output-format json --allowedTools Read,Edit,Bash")

  "codex":
    goal_with_context = step.role + "\n\nContext:\n" + input_context
    result = spawn("codex exec '{goal}' --json --approval-mode full-auto")

  "openclaw":
    result = OpenClawManager.spawn(config_with_goal_and_context)
```

---

## What the CLI Needs

```bash
# Pipeline management
clove pipeline create pr-fixer        # interactive or from template
clove pipeline list                    # show all pipelines
clove pipeline show pr-fixer           # show steps, triggers, history
clove pipeline run pr-fixer            # manual trigger
clove pipeline enable pr-fixer         # enable auto-trigger
clove pipeline disable pr-fixer        # pause
clove pipeline delete pr-fixer         # remove

# One-command pipeline from template
clove pipeline create --template pr-fixer --repo aniiiiXD/Clove
```

---

## Pipeline Templates

Ship with 3-4 real pipeline templates:

### 1. PR Fixer
```
Trigger: GitHub PR opened
Steps: reviewer (CLOVE) → fixer (Claude Code) → verifier (Codex) → supervisor (CLOVE)
Connections: GitHub, DeepWiki
```

### 2. Incident Responder
```
Trigger: PagerDuty webhook
Steps: detector (CLOVE/http) → diagnostician (Claude Code) → fixer (Claude Code) → verifier (Codex) → reporter (CLOVE/Slack)
Connections: PagerDuty, GitHub, Slack, Grafana
```

### 3. Weekly Security Audit
```
Trigger: Cron (Friday 6pm)
Steps: dep-scanner (CLOVE) → code-scanner (Claude Code) → reporter (CLOVE)
Connections: GitHub
```

### 4. Lead Finder
```
Trigger: Cron (Monday 9am)
Steps: researcher (CLOVE/search) → analyzer (CLOVE) → reporter (CLOVE/Slack)
Connections: Slack
```

---

## Build Order

### Week 1: Pipeline Engine (kernel)
1. `POST /api/pipeline/run` — sequential step execution with runtime dispatch
2. World state read/write during agent execution (context passing)
3. Context injection into Claude Code / Codex spawns
4. `POST /api/pipeline-defs` — save/load pipeline definitions
5. Webhook → pipeline trigger matching

### Week 2: Dashboard Pipeline Experience
1. Pipeline creation flow (visual builder → save)
2. Pipeline execution view (live step progress with data flow)
3. Pipeline list + history
4. Home page showing active pipelines

### Week 3: Connections + Templates
1. `clove connect` actually configures MCP + webhook ingestion
2. Ship 3-4 pipeline templates
3. Template → one-click deploy flow in dashboard
4. GitHub webhook verification (signature checking)

### Week 4: Polish + Real Testing
1. Test full PR Fixer pipeline end to end
2. Error handling and retries
3. Cost estimation before run
4. Notification when pipeline completes (Slack, email, webhook)

---

## Success Criteria

The product is "good" when:

1. A user can go from `npm install -g @cloveos/cli` to a working PR fixer pipeline in under 10 minutes
2. The pipeline actually fixes real PRs — not demo data, real code
3. The dashboard shows the pipeline running with clear step-by-step progress
4. Context flows between agents — the fixer knows what the reviewer found
5. Different runtimes work together — CLOVE, Claude Code, and Codex in the same pipeline
6. The whole thing costs less than $1 per PR
7. Everything is audited — you can prove to a compliance officer what every agent did
