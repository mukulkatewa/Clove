#!/usr/bin/env node
/**
 * CLOVE v2 — GitHub Swarm Seed
 * Creates the workspace, 3 agent defs, and the swarm in one shot.
 * Run AFTER the kernel is up: node tools/seed_github_swarm.mjs
 */

const API   = process.env.CLOVE_API  || 'http://localhost:8080'
const REPO  = process.env.GITHUB_REPO || 'aniiiiXD/Clove'
const TOKEN = process.env.GITHUB_TOKEN || ''

async function post(path, body) {
  const r = await fetch(`${API}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  const text = await r.text()
  if (!r.ok) throw new Error(`POST ${path} → ${r.status}: ${text}`)
  return JSON.parse(text)
}

async function put(path, body) {
  const r = await fetch(`${API}${path}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  const text = await r.text()
  if (!r.ok) throw new Error(`PUT ${path} → ${r.status}: ${text}`)
  return JSON.parse(text)
}

// ── 1. Workspace ────────────────────────────────────────────────────────────
console.log('\n[1/4] Creating workspace: github-ops')
const ws = await post('/api/workspaces', {
  name: 'github-ops',
  metadata: {
    repo: REPO,
    description: 'GitHub issue triage & fix swarm',
  },
}).catch(e => {
  // May already exist — that's fine
  if (e.message.includes('409') || e.message.includes('already')) {
    console.log('  workspace already exists, continuing')
    return { id: null }
  }
  throw e
})
console.log('  workspace id:', ws.id || '(existing)')

// Fetch workspace id by name if not returned
let wsId = ws.id
if (!wsId) {
  const r = await fetch(`${API}/api/workspaces`)
  const list = await r.json()
  const found = (list.workspaces || list).find(w => w.name === 'github-ops')
  wsId = found?.id
}
if (!wsId) throw new Error('Could not resolve github-ops workspace id')
console.log('  resolved id:', wsId)

// ── 2. Agent Definitions ────────────────────────────────────────────────────
if (!TOKEN || TOKEN === 'ghp_REPLACE_WITH_YOUR_TOKEN') {
  console.error('\nERROR: Set GITHUB_TOKEN in .env.kernel before seeding.\n')
  process.exit(1)
}

const GH_HEADERS = `Authorization: Bearer ${TOKEN}, Accept: application/vnd.github+json, X-GitHub-Api-Version: 2022-11-28`

const agents = [
  {
    name: 'gh-scout',
    description: 'Lists open issues and PRs from a GitHub repo and writes findings to workspace data.',
    enabled: true,
    workspace_id: wsId,
    triggers: [{ type: 'manual' }],
    connections: [],
    action: {
      goal: `Use the http tool to call https://api.github.com/repos/${REPO}/issues?state=open&per_page=20 with headers "${GH_HEADERS}". Parse the JSON response. For each issue, extract: number, title, labels, created_at, body (first 200 chars). Then use the store tool to save the findings as JSON under the key "gh_issues" in workspace ${wsId}. Finally, write a plain English summary of what you found.`,
      tools: ['http', 'store'],
      max_steps: 8,
      model: 'claude-opus-4-5',
    },
    budget: { per_run: 0.15, daily_max: 2.0 },
  },
  {
    name: 'gh-analyst',
    description: 'Reads issue findings from workspace data and produces a prioritized triage report.',
    enabled: true,
    workspace_id: wsId,
    triggers: [{ type: 'manual' }],
    connections: [],
    action: {
      goal: `Use the store tool to read the value of key "gh_issues" from workspace ${wsId}. Analyze the issues and categorize them as: CRITICAL (data loss / security), HIGH (feature broken), MEDIUM (UX / perf), LOW (docs / typos). For each issue, note the number, title, and your recommended priority + a one-sentence fix suggestion. Write the prioritized triage report and save it as "gh_triage" in workspace ${wsId}.`,
      tools: ['store', 'search'],
      max_steps: 10,
      model: 'claude-opus-4-5',
    },
    budget: { per_run: 0.20, daily_max: 2.0 },
  },
  {
    name: 'gh-fixer',
    description: 'Takes the top-priority issue from the triage report and posts a detailed fix comment via GitHub API.',
    enabled: true,
    workspace_id: wsId,
    triggers: [{ type: 'manual' }],
    connections: [],
    action: {
      goal: `Use the store tool to read "gh_triage" from workspace ${wsId}. Identify the single highest-priority issue. Then use the http tool to POST a comment to https://api.github.com/repos/${REPO}/issues/{number}/comments with headers "${GH_HEADERS}" and body containing: your analysis of the root cause, a concrete step-by-step fix plan, and any relevant code snippets or file paths if known. Save the comment URL to workspace ${wsId} under key "gh_fix_posted".`,
      tools: ['http', 'store'],
      max_steps: 8,
      model: 'claude-opus-4-5',
    },
    budget: { per_run: 0.20, daily_max: 2.0 },
  },
]

console.log('\n[2/4] Creating agent definitions')
for (const agent of agents) {
  const body = {
    name: agent.name,
    description: agent.description,
    enabled: agent.enabled,
    workspace_id: agent.workspace_id,
    triggers: agent.triggers,
    connections: agent.connections,
    action: agent.action,
    budget: agent.budget,
  }
  try {
    await post('/api/agent-defs', body)
    console.log(`  created: ${agent.name}`)
  } catch (e) {
    if (e.message.includes('409') || e.message.includes('already')) {
      await put(`/api/agent-defs/${agent.name}`, body)
      console.log(`  updated: ${agent.name}`)
    } else throw e
  }
}

// ── 3. Swarm ────────────────────────────────────────────────────────────────
console.log('\n[3/4] Creating swarm: github-swarm')
const swarmBody = {
  name: 'github-swarm',
  workspace_id: wsId,
  agents: ['gh-scout', 'gh-analyst', 'gh-fixer'],
  goal: `Triage open issues in ${REPO}, prioritize them, and post a fix plan on the top issue.`,
  budget: 0.6,
}
try {
  await post('/api/swarms', swarmBody)
  console.log('  swarm created')
} catch (e) {
  if (e.message.includes('409') || e.message.includes('already')) {
    console.log('  swarm already exists')
  } else throw e
}

// ── 4. Submit jobs in sequence ──────────────────────────────────────────────
console.log('\n[4/4] Submitting pipeline jobs')

const jobs = [
  { agent_name: 'gh-scout',   priority: 3 },
  { agent_name: 'gh-analyst', priority: 2 },
  { agent_name: 'gh-fixer',   priority: 1 },
]

const jobIds = []
for (const { agent_name, priority } of jobs) {
  const agentDef = agents.find(a => a.name === agent_name)
  const j = await post('/api/jobs', {
    agent_name,
    workspace_id: wsId,
    goal: agentDef.action.goal,
    model: agentDef.action.model,
    budget_usd: agentDef.budget.per_run,
    max_steps: agentDef.action.max_steps,
    allowed_tools: agentDef.action.tools,
    priority,
  })
  console.log(`  queued job ${j.id.slice(0, 8)} → ${agent_name}`)
  jobIds.push({ id: j.id, agent_name })
}

console.log('\nAll done. Job IDs:')
for (const { id, agent_name } of jobIds) {
  console.log(`  ${agent_name}: ${id}`)
  console.log(`  stream: curl -N ${API}/api/jobs/${id}/stream`)
}

console.log('\nSupabase dashboard: https://supabase.com/dashboard/project/pzldqapdbiszeumueyzh/editor')
console.log('Watch the agent_runs table to see jobs flow through in real time.')
