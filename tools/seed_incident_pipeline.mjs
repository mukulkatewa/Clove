#!/usr/bin/env node
/**
 * CLOVE v2 — Incident Response Pipeline Seed
 *
 * Creates the full "eng-ops" workspace with:
 *   - 5 chained agents using real MCP tools (GitHub + Slack + Filesystem)
 *   - 1 swarm: incident-swarm
 *   - 1 daemon: incident-monitor (watches Slack #incidents channel)
 *   - Sequential pipeline run (each job depends_on previous)
 *
 * Data flow:
 *   workspace_data["issue_context"]    ← triager writes
 *   workspace_data["patch_diff"]       ← patch-writer writes
 *   workspace_data["review_verdict"]   ← reviewer writes
 *   workspace_outputs[]                ← notifier writes Slack msg + GitHub comment URL
 */

const API          = process.env.CLOVE_API        || 'http://localhost:8080'
const REPO         = process.env.GITHUB_REPO      || 'aniiiiXD/Clove'
const TG_TOKEN     = process.env.TELEGRAM_TOKEN   || ''
const TG_CHAT_ID   = process.env.TELEGRAM_CHAT_ID || ''
const TG_API       = `https://api.telegram.org/bot${TG_TOKEN}/sendMessage`
const SB_URL       = process.env.SUPABASE_URL        || ''
const SB_KEY       = process.env.SUPABASE_SERVICE_KEY || ''
const GH_TOKEN     = process.env.GITHUB_TOKEN         || ''

// ── Helpers ────────────────────────────────────────────────────────────────

async function post(path, body) {
  const r = await fetch(`${API}${path}`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  })
  const text = await r.text()
  if (!r.ok && r.status !== 409) throw new Error(`POST ${path} ${r.status}: ${text}`)
  if (!text || !text.trim()) return {}
  return JSON.parse(text)
}

async function put(path, body) {
  const r = await fetch(`${API}${path}`, {
    method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  })
  const text = await r.text()
  if (!r.ok) throw new Error(`PUT ${path} ${r.status}: ${text}`)
  return JSON.parse(text)
}

async function get(path) {
  const r = await fetch(`${API}${path}`)
  return r.json()
}

async function sbUpsert(table, row) {
  if (!SB_URL || !SB_KEY) return
  await fetch(`${SB_URL}/rest/v1/${table}`, {
    method: 'POST',
    headers: {
      'apikey': SB_KEY,
      'Authorization': `Bearer ${SB_KEY}`,
      'Content-Type': 'application/json',
      'Prefer': 'resolution=merge-duplicates,return=minimal',
    },
    body: JSON.stringify(row),
  })
}

async function upsertAgent(def) {
  try {
    await post('/api/agent-defs', def)
    console.log(`  created: ${def.name}`)
  } catch (e) {
    if (e.message.includes('409') || e.message.includes('already') || e.message.includes('UNIQUE')) {
      await put(`/api/agent-defs/${def.name}`, def)
      console.log(`  updated: ${def.name}`)
    } else throw e
  }
  // Push to Supabase with flat schema (kernel sends nested JSON which fails schema check)
  await sbUpsert('agent_definitions', {
    name: def.name,
    workspace_id: def.workspace_id || null,
    description: def.description || '',
    enabled: def.enabled !== false,
    goal: def.action?.goal || def.goal || '',
    triggers: def.triggers || [],
    tools: def.action?.tools || def.tools || [],
    connections: def.connections || [],
    permissions: def.permissions || {},
    budget_per_run: def.budget?.per_run || 0.25,
    budget_daily_max: def.budget?.daily_max || 5.0,
    budget_daily_spent: 0,
    model: def.action?.model || def.model || 'google/gemini-2.5-pro-preview',
    max_steps: def.action?.max_steps || def.max_steps || 20,
  })
}

// ── 0. Set agent permissions (enable HTTP for notifier) ─────────────────────

const permRes = await fetch(`${API}/api/agents/0/permissions`, {
  method: 'PUT',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    can_http: true,
    can_read: true,
    can_write: true,
    allowed_domains: ['api.telegram.org', 'api.github.com']
  })
})
console.log('[0/6] Permissions: can_http enabled for agent 0 →', permRes.status === 200 ? 'OK' : await permRes.text())

// ── 1. Workspace ────────────────────────────────────────────────────────────

console.log('\n[1/6] Workspace: eng-ops')
// Always check existing first to avoid in-memory-only duplicate creation
const list = await get('/api/worlds')
const arr = Array.isArray(list) ? list : (list?.worlds || list?.workspaces || [])
let WS = arr.find(w => w.name === 'eng-ops')?.id
if (!WS) {
  const wsRes = await post('/api/worlds', {
    name: 'eng-ops',
    metadata: { repo: REPO, description: 'Engineering operations: incident response, code review, reporting' },
  })
  WS = wsRes?.id
}
if (!WS) throw new Error('Could not resolve eng-ops workspace id')
console.log(`  workspace id: ${WS}`)
// Ensure workspace exists in Supabase (kernel may not have upserted it if it was pre-existing in SQLite)
await sbUpsert('workspaces', { id: WS, name: 'eng-ops', status: 'active', metadata: { repo: REPO } })

// ── 2. Agent Definitions ────────────────────────────────────────────────────

console.log('\n[2/6] Agent definitions')

const TOOLS_MCP_GH  = ['mcp', 'store', 'fetch']
const TOOLS_MCP_FS  = ['mcp', 'store', 'fetch', 'read_file']
const TOOLS_MCP_ALL = ['mcp', 'store', 'fetch', 'read_file', 'write_file', 'http']
const TOOLS_WRITE   = ['mcp', 'store', 'fetch', 'write_file']

await upsertAgent({
  name: 'triager',
  description: 'Reads the latest open GitHub issues, classifies severity, saves structured triage to workspace.',
  enabled: true,
  workspace_id: WS,
  triggers: [{ type: 'manual' }],
  connections: ['github'],
  action: {
    goal: `You are an incident triager for the repo ${REPO}.

Steps:
1. Use mcp_call with server="github" and tool="list_issues" to get all open issues (state=open, per_page=20).
2. For each issue, determine: severity (CRITICAL/HIGH/MEDIUM/LOW), affected_component (guess from title/body), estimated_effort (hours).
3. Build a JSON object: { "issues": [ { "number": N, "title": "...", "severity": "...", "affected_component": "...", "estimated_effort": N, "summary": "one sentence" }, ... ], "top_issue": { issue object for highest severity } }
4. Use the store tool to save this JSON as key "issue_context" in workspace ${WS}.
5. Write a plain-English summary of what you found.`,
    tools: TOOLS_MCP_GH,
    max_steps: 10,
    model: 'google/gemini-2.5-pro-preview',
  },
  budget: { per_run: 0.20, daily_max: 3.0 },
})


await upsertAgent({
  name: 'patch-writer',
  description: 'Reads issue_context from workspace, writes the actual code fix, saves diff to workspace.',
  enabled: true,
  workspace_id: WS,
  triggers: [{ type: 'manual' }],
  connections: ['filesystem'],
  action: {
    goal: `You are a staff engineer writing a production-quality patch.

Steps:
1. Use the fetch tool with key "issue_context" to read the triage data. Extract top_issue.affected_component (filename) and top_issue.title/summary.
2. Use mcp_filesystem_read_file to read the full file that needs changing (use the affected_component filename from issue_context).
3. Based on the issue description, write the minimal, correct fix. Do not change anything unrelated to the bug.
4. Produce a unified diff (like git diff output) showing exactly what changes to make.
5. Also write a clear commit message: "fix(component): one-line description\\n\\nDetailed explanation of the change."
6. Save to workspace ${WS} as "patch_diff": { "file": "...", "diff": "...", "commit_message": "..." }`,
    tools: TOOLS_MCP_FS,
    max_steps: 12,
    model: 'google/gemini-2.5-pro-preview',
  },
  budget: { per_run: 0.30, daily_max: 4.0 },
})

await upsertAgent({
  name: 'reviewer',
  description: 'Reviews the patch for correctness, security, and regressions. Gives verdict.',
  enabled: true,
  workspace_id: WS,
  triggers: [{ type: 'manual' }],
  connections: ['github', 'filesystem'],
  action: {
    goal: `You are a principal engineer doing a critical code review.

Steps:
1. Use the fetch tool with key "patch_diff" and again with key "issue_context" to read both.
2. Read the diff carefully. Check for:
   - Does it actually fix the issue described in issue_context?
   - Any new bugs introduced?
   - Security issues (injection, overflow, race conditions)?
   - Missing error handling?
   - Does it follow the existing code style?
3. Give a verdict: APPROVE, REQUEST_CHANGES, or REJECT with specific reasons.
4. Save to workspace ${WS} as "review_verdict": { "verdict": "APPROVE|REQUEST_CHANGES|REJECT", "comments": ["..."], "confidence": 0-100 }`,
    tools: TOOLS_MCP_FS,
    max_steps: 10,
    model: 'google/gemini-2.5-pro-preview',
  },
  budget: { per_run: 0.25, daily_max: 3.0 },
})

await upsertAgent({
  name: 'notifier',
  description: 'Posts results to Telegram and GitHub. Final step of the incident response pipeline.',
  enabled: true,
  workspace_id: WS,
  triggers: [{ type: 'manual' }],
  connections: ['github'],
  action: {
    goal: `You are the final step in an automated incident response pipeline.

Steps:
1. Read workspace data using fetch: fetch("issue_context"), fetch("patch_diff"), fetch("review_verdict").
2. Build a concise plain-text incident summary (no Markdown, no backticks, no special chars):
   - Issue: #number - title (severity)
   - Root cause: file, function, one-line explanation (or "Not found" if missing)
   - Patch: verdict + confidence %
   - If APPROVE: "Ready to merge" | If REQUEST_CHANGES: bullet list of comments
3. Send to Telegram using the http tool (POST):
   URL: ${TG_API}
   Body (JSON string): {"chat_id":"${TG_CHAT_ID}","text":"<your plain-text summary>"}
4. Post the same summary as a GitHub issue comment using the http tool (POST):
   URL: https://api.github.com/repos/${REPO}/issues/<issue_number>/comments
   Body (JSON string): {"body":"<your summary>"}
   Headers: {"Authorization":"token ${GH_TOKEN}","Content-Type":"application/json","Accept":"application/vnd.github.v3+json"}
   Replace <issue_number> with issue_context.top_issue.number
5. Use store to save "notification_sent": {"telegram":true,"github_comment":true,"timestamp":"<now>"}.`,
    tools: TOOLS_MCP_ALL,
    max_steps: 12,
    model: 'google/gemini-2.5-pro-preview',
  },
  budget: { per_run: 0.20, daily_max: 3.0 },
})

// ── 3. Daemon: incident-monitor ─────────────────────────────────────────────

console.log('\n[3/6] Daemon: incident-monitor')
await upsertAgent({
  name: 'incident-monitor',
  description: 'Always-on daemon. Watches GitHub issues for new critical reports. Sends Telegram alert when detected.',
  enabled: true,
  workspace_id: WS,
  triggers: [{ type: 'daemon', tick_interval_s: 120 }],
  connections: ['github'],
  action: {
    goal: `You are an always-on incident monitor for ${REPO}.

On each tick:
1. Use mcp_call with server="github" tool="list_issues" to get open issues (owner="${REPO.split('/')[0]}", repo="${REPO.split('/')[1]}", state="open").
2. Use the fetch tool with key "processed_incidents" to get the list of already-processed issue numbers (returns empty if first run).
3. For any new issue with keywords "bug", "error", "crash", "broken", "critical", "incident", "fail" in the title or body:
   a. Use the http tool to POST ${TG_API} with body { "chat_id": "${TG_CHAT_ID}", "text": "New incident detected: #<number> <title>. Starting automated triage." }
   b. Add the issue number to "processed_incidents" in workspace ${WS} using the store tool.
4. If nothing new, write "tick ok — no new incidents" to the store.`,
    tools: TOOLS_MCP_ALL,
    max_steps: 8,
    model: 'google/gemini-2.5-pro-preview',
  },
  budget: { per_run: 0.02, daily_max: 1.0 },
  daemon: {
    enabled: true,
    tick_interval_s: 120,
    dream_enabled: true,
    dream_min_hours: 4,
  },
})

// Start the daemon
try {
  await post('/api/daemons/incident-monitor/start', {})
  console.log('  daemon started (ticking every 2 min)')
} catch (e) {
  console.log(`  daemon: ${e.message.includes('already') ? 'already running' : e.message}`)
}

// ── 4. Swarm ────────────────────────────────────────────────────────────────

console.log('\n[4/6] Swarm: incident-swarm')
try {
  await post('/api/swarms', {
    name: 'incident-swarm',
    workspace_id: WS,
    agents: ['triager', 'patch-writer', 'reviewer', 'notifier'],
    goal: `Full automated incident response for ${REPO}: triage → patch → review → notify`,
    budget: 1.30,
  })
  console.log('  created')
} catch (e) {
  if (e.message.includes('409') || e.message.includes('already')) console.log('  exists')
  else throw e
}
await sbUpsert('swarms', {
  name: 'incident-swarm',
  workspace_id: WS,
  agents: ['triager', 'patch-writer', 'reviewer', 'notifier'],
  goal: `Full automated incident response for ${REPO}: triage → patch → review → notify`,
  budget: 1.30,
  status: 'active',
})

// ── 5. Submit chained pipeline jobs ────────────────────────────────────────

console.log('\n[5/6] Submitting chained pipeline jobs')

const agents = [
  { name: 'triager',      priority: 4 },
  { name: 'patch-writer', priority: 3 },
  { name: 'reviewer',     priority: 2 },
  { name: 'notifier',     priority: 1 },
]

const defs = await get('/api/agent-defs')
const defMap = {}
for (const d of (defs.agents || [])) defMap[d.name] = d

let prev_id = null
const jobIds = []

for (const { name, priority } of agents) {
  const def = defMap[name]
  if (!def) { console.log(`  ⚠ agent def not found: ${name}`); continue }

  const body = {
    agent_name: name,
    workspace_id: WS,
    goal: def.action?.goal || def.goal || '',
    model: def.action?.model || def.model || 'google/gemini-2.5-pro-preview',
    budget_usd: def.budget?.per_run || 0.25,
    max_steps: def.action?.max_steps || 12,
    allowed_tools: def.action?.tools || ['mcp_call', 'store'],
    priority,
    ...(prev_id ? { depends_on: prev_id } : {}),
  }

  const j = await post('/api/jobs', body)
  console.log(`  queued ${j.id?.slice(0,8)} → ${name}${prev_id ? ` (after ${prev_id.slice(0,8)})` : ' (first)'}`)
  prev_id = j.id
  jobIds.push({ id: j.id, name })
}

// ── 6. Summary ──────────────────────────────────────────────────────────────

console.log('\n[6/6] Summary')
console.log(`\n  Workspace:  eng-ops (${WS})`)
console.log(`  Agents:     triager → patch-writer → reviewer → notifier`)
console.log(`  Daemon:     incident-monitor (watching ${REPO} issues, Telegram alerts)`)
console.log(`  Swarm:      incident-swarm`)
console.log(`\n  Pipeline jobs (chained):`)
for (const { id, name } of jobIds) {
  console.log(`    ${name.padEnd(16)} ${id}`)
  console.log(`    stream: curl -N ${API}/api/jobs/${id}/stream`)
}
console.log(`\n  Supabase: https://supabase.com/dashboard/project/pzldqapdbiszeumueyzh/editor`)
console.log(`  Dashboard: ${API}/api/health\n`)

export default true
