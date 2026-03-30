const AGENT_NAMES = ['researcher-alpha', 'writer-bravo', 'analyst-charlie', 'reviewer-delta', 'monitor-echo', 'coder-foxtrot']
const MODELS = ['anthropic/claude-sonnet-4', 'openai/gpt-4o', 'google/gemini-2.5-pro', 'meta/llama-4-scout']
const EVENT_TYPES = ['SYS_THINK', 'SYS_READ', 'SYS_WRITE', 'SYS_EXEC', 'SYS_HTTP', 'SYS_STORE', 'SYS_FETCH', 'SYS_MEM_CREATE', 'SYS_SPAWN', 'SYS_SEND']
const CATEGORIES = ['SYSCALL', 'SECURITY', 'NETWORK', 'RESOURCE', 'COMPLIANCE']
const GOALS = [
  'Research the AI agent market and identify top 5 competitors',
  'Analyze Q1 earnings data and produce executive summary',
  'Review authentication module for security vulnerabilities',
  'Generate API documentation for the payment service',
  'Monitor production endpoints and report anomalies',
  'Audit dependency tree for license compliance issues',
  'Compare cloud GPU pricing across AWS, GCP, and Azure',
  'Write a technical blog post about kernel sandboxing',
  'Prepare investor deck with market size analysis',
  'Scan codebase for hardcoded credentials',
]
const DOMAINS = ['api.openai.com', 'api.anthropic.com', 'openrouter.ai', 'github.com', 'arxiv.org', 'slack.com']
const TOOLS = ['read_file', 'write_file', 'exec', 'http', 'search', 'remember', 'recall', 'mcp_call']

let _s = 42
function sr(s: number) { _s = s }
function r(a: number, b: number) { _s = (_s * 16807) % 2147483647; return a + (_s / 2147483647) * (b - a) }
function pk<T>(arr: T[]): T { return arr[Math.floor(r(0, arr.length))] }
function pkN<T>(arr: T[], n: number): T[] { const c = [...arr]; const res: T[] = []; for (let i = 0; i < n && c.length; i++) { const idx = Math.floor(r(0, c.length)); res.push(c.splice(idx, 1)[0]) }; return res }

export function demoHealth() { return { version: '2.0.0', status: 'running', uptime_s: 15780, syscall_count: 86 } }
export function demoCost() { return { total_cost_usd: 4.2847, max_cost_usd: 25.0, total_requests: 347 } }

export function demoAgents() {
  sr(200)
  return AGENT_NAMES.map((name, i) => ({
    id: `agent-${i + 1}`, name, type: 'openclaw',
    state: i < 4 ? 'running' as const : 'stopped' as const,
    pid: 10000 + i * 137,
    budget_usd: +r(2, 20).toFixed(2), cost_usd: +r(0.01, 5).toFixed(4),
    channels: i % 2 === 0 ? ['slack'] : [],
    started_at_ms: Date.now() - Math.floor(r(60000, 3600000)),
    permissions: { can_exec: i < 3, can_read: true, can_write: i < 4, can_think: true, can_spawn: i === 0, can_http: i < 5, allowed_read_paths: ['/tmp', '~/Documents'], allowed_write_paths: ['/tmp'], allowed_domains: pkN(DOMAINS, 3), blocked_commands: [] },
    budget_tracking: { max_cost_usd: +r(5, 25).toFixed(2), cost_usd: +r(0.1, 4).toFixed(4), max_tokens: 100000, tokens_used: Math.floor(r(5000, 80000)), max_steps: 50, steps_taken: Math.floor(r(3, 40)) },
  }))
}

export function demoRuns() {
  sr(300)
  return Array.from({ length: 14 }, (_, i) => ({
    name: `run-${String(14 - i).padStart(3, '0')}`,
    chain_id: `chain_${Math.floor(r(100000, 999999))}${Math.floor(r(100000, 999999))}`,
    description: pk(GOALS), artifact_count: Math.floor(r(0, 8)),
    created_at_ms: Date.now() - i * Math.floor(r(300000, 1800000)),
  }))
}

export function demoAudit() {
  sr(400)
  return Array.from({ length: 120 }, (_, i) => ({
    id: 120 - i, event_type: pk(EVENT_TYPES), category: pk(CATEGORIES),
    agent_id: Math.floor(r(1, 7)), agent_name: pk(AGENT_NAMES),
    success: r(0, 1) > 0.12,
    timestamp: new Date(Date.now() - i * Math.floor(r(5000, 60000))).toISOString(),
    details: { tool: pk(TOOLS), duration_ms: Math.floor(r(10, 500)) },
  }))
}

export function demoOpenClaw() {
  return [
    { id: 'oc-1', name: 'research-bot', state: 'running' as const, pid: 22001, port: 18800, budget_usd: 10, cost_usd: 2.34, channels: ['slack', 'telegram'], started_at_ms: Date.now() - 7200000 },
    { id: 'oc-2', name: 'support-agent', state: 'running' as const, pid: 22002, port: 18801, budget_usd: 5, cost_usd: 0.87, channels: ['slack'], started_at_ms: Date.now() - 3600000 },
    { id: 'oc-3', name: 'code-reviewer', state: 'stopped' as const, pid: 0, port: 18802, budget_usd: 8, cost_usd: 3.12, channels: [], started_at_ms: Date.now() - 86400000 },
  ]
}

export function demoWorlds() {
  return [
    { id: 1, name: 'research-sandbox', member_count: 3, metadata: { topic: 'AI agents market', phase: 'collection' } },
    { id: 2, name: 'production-sim', member_count: 5, metadata: { environment: 'staging', load_test: true } },
    { id: 3, name: 'compliance-audit', member_count: 2, metadata: { regulation: 'EU AI Act', deadline: '2026-04-15' } },
    { id: 4, name: 'dev-playground', member_count: 0, metadata: {} },
  ]
}

export function demoMemory() {
  return [
    { id: 'mem-1', name: 'system-prompt', type: 'system', content: 'You are a research agent specialized in AI market analysis. Always cite sources. Produce structured outputs with executive summaries.', access: 'shared' },
    { id: 'mem-2', name: 'api-keys-policy', type: 'system', content: 'Never log, store, or transmit API keys in any form. If encountered in code review, flag immediately.', access: 'shared' },
    { id: 'mem-3', name: 'competitor-list', type: 'core', content: 'Primary competitors:\n1. OpenAI Assistants API\n2. LangGraph Cloud\n3. CrewAI\n4. AutoGen\n5. Fixie.ai', access: 'shared' },
    { id: 'mem-4', name: 'meeting-notes-q1', type: 'core', content: 'Q1 review:\n- Agent costs down 40%\n- Fleet runs averaging 7.2 agents\n- Sandbox violations down to 0.3%', access: 'private' },
    { id: 'mem-5', name: 'research-arxiv', type: 'recall', content: 'Papers: Reflexion, Observation Masking, Cost-Aware LLM Serving', access: 'shared' },
    { id: 'mem-6', name: 'debug-003', type: 'recall', content: 'Bug: world_id not propagated through SYS_SPAWN. Fixed in a3f91c2.', access: 'private' },
    { id: 'mem-7', name: 'user-prefs', type: 'core', content: 'Output: markdown. Max: 2000 tokens. Tone: professional.', access: 'private' },
    { id: 'mem-8', name: 'fleet-rules', type: 'system', content: 'Coordinator assigns subtopics. Agents store under cycle{N}:research:{idx}. Emit events for phase transitions.', access: 'shared' },
  ]
}

export function demoSchedules() {
  return [
    { name: 'morning-research', cron: '0 9 * * MON-FRI', enabled: true, run: { goal: 'Compile overnight AI news digest', budget: 0.5, agents: 1 } },
    { name: 'weekly-audit', cron: '0 18 * * FRI', enabled: true, run: { goal: 'Run full dependency security audit', budget: 1.0, agents: 1 } },
    { name: 'competitor-watch', cron: '0 12 * * WED', enabled: false, run: { goal: 'Track competitor product changes', budget: 2.0, agents: 3 } },
  ]
}

export function demoWebhooks() {
  return [
    { id: 'wh-1', url: 'https://hooks.slack.com/services/T0XXX/B0XXX/xxxxx', events: ['run_complete', 'budget_exceeded'], enabled: true, created_at_ms: Date.now() - 604800000 },
    { id: 'wh-2', url: 'https://api.pagerduty.com/webhooks/v1/xxxxx', events: ['agent_error', 'budget_exceeded'], enabled: true, created_at_ms: Date.now() - 172800000 },
  ]
}

export function demoSandboxOverview() {
  const agents = demoAgents()
  const audit = demoAudit()
  return {
    agents: agents.map(a => ({ id: a.id, name: a.name, state: a.state, pid: a.pid, budget_usd: a.budget_usd, cost_usd: a.cost_usd, channels: a.channels, permissions: a.permissions, budget_tracking: a.budget_tracking })),
    activity: audit.slice(0, 20).map(e => ({ event_type: e.event_type, category: e.category, agent_name: e.agent_name, timestamp: e.timestamp, success: e.success })),
    cost: { total_usd: 4.2847 }, memory_blocks: 8,
  }
}

export function demoMcpServers() {
  return [
    { name: 'filesystem', status: 'running', tools_count: 5, command: 'npx @modelcontextprotocol/server-filesystem /tmp' },
    { name: 'github', status: 'running', tools_count: 12, command: 'npx @modelcontextprotocol/server-github' },
    { name: 'slack', status: 'stopped', tools_count: 8, command: 'npx @modelcontextprotocol/server-slack' },
    { name: 'postgres', status: 'running', tools_count: 6, command: 'npx @modelcontextprotocol/server-postgres' },
  ]
}

export function demoMcpTools() {
  return [
    { server_name: 'filesystem', name: 'read_file', description: 'Read contents of a file', input_schema: { type: 'object', properties: { path: { type: 'string' } } } },
    { server_name: 'filesystem', name: 'write_file', description: 'Write content to a file', input_schema: { type: 'object', properties: { path: { type: 'string' }, content: { type: 'string' } } } },
    { server_name: 'filesystem', name: 'list_directory', description: 'List files in a directory', input_schema: { type: 'object', properties: { path: { type: 'string' } } } },
    { server_name: 'filesystem', name: 'search_files', description: 'Search for files matching a pattern', input_schema: { type: 'object', properties: { pattern: { type: 'string' } } } },
    { server_name: 'filesystem', name: 'get_file_info', description: 'Get file metadata', input_schema: { type: 'object', properties: { path: { type: 'string' } } } },
    { server_name: 'github', name: 'list_repos', description: 'List repositories for the authenticated user', input_schema: { type: 'object' } },
    { server_name: 'github', name: 'get_repo', description: 'Get repository details', input_schema: { type: 'object', properties: { owner: { type: 'string' }, repo: { type: 'string' } } } },
    { server_name: 'github', name: 'list_issues', description: 'List issues in a repository', input_schema: { type: 'object', properties: { owner: { type: 'string' }, repo: { type: 'string' } } } },
    { server_name: 'github', name: 'create_issue', description: 'Create a new issue', input_schema: { type: 'object', properties: { owner: { type: 'string' }, repo: { type: 'string' }, title: { type: 'string' } } } },
    { server_name: 'github', name: 'list_prs', description: 'List pull requests', input_schema: { type: 'object', properties: { owner: { type: 'string' }, repo: { type: 'string' } } } },
    { server_name: 'github', name: 'get_pr', description: 'Get pull request details', input_schema: { type: 'object', properties: { owner: { type: 'string' }, repo: { type: 'string' }, number: { type: 'number' } } } },
    { server_name: 'github', name: 'create_pr', description: 'Create a pull request', input_schema: {} },
    { server_name: 'slack', name: 'send_message', description: 'Send a message to a Slack channel', input_schema: { type: 'object', properties: { channel: { type: 'string' }, text: { type: 'string' } } } },
    { server_name: 'slack', name: 'list_channels', description: 'List available channels', input_schema: {} },
    { server_name: 'postgres', name: 'query', description: 'Execute a SQL query', input_schema: { type: 'object', properties: { sql: { type: 'string' } } } },
    { server_name: 'postgres', name: 'list_tables', description: 'List database tables', input_schema: {} },
    { server_name: 'postgres', name: 'describe_table', description: 'Get table schema', input_schema: { type: 'object', properties: { table: { type: 'string' } } } },
  ]
}

export function demoWorldTemplates() {
  return [
    { name: 'code-health', displayName: 'Code Health Check', description: '3 agents review security, deps, quality — synthesize into report', agents: 4, category: 'dev' },
    { name: 'incident-response', displayName: 'Incident Response', description: 'Sentinel + diagnostician + fixer + verifier for prod issues', agents: 4, category: 'ops' },
    { name: 'research-station', displayName: 'Research Station', description: 'Parallel researchers + writer + reviewer on any topic', agents: 5, category: 'research' },
  ]
}
