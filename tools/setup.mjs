#!/usr/bin/env node
/**
 * CLOVE v2 — One-shot setup
 * Reads .env.kernel, configures all MCP connections, then runs the full seed.
 *
 * Usage:
 *   source .env.kernel && node tools/setup.mjs
 *
 * What it does:
 *   1. Waits for kernel to be up (retries /api/health for 30s)
 *   2. Registers every token found in env as a live MCP connection
 *   3. Seeds the incident-response pipeline (workspace + agents + swarm + jobs)
 */

import { execSync } from 'child_process'

const API = process.env.CLOVE_API || 'http://localhost:8080'

// ── Helpers ────────────────────────────────────────────────────────────────

async function api(method, path, body) {
  const r = await fetch(`${API}${path}`, {
    method,
    headers: { 'Content-Type': 'application/json' },
    ...(body ? { body: JSON.stringify(body) } : {}),
  })
  const text = await r.text()
  if (!r.ok && r.status !== 409) throw new Error(`${method} ${path} → ${r.status}: ${text}`)
  try { return JSON.parse(text) } catch { return { raw: text } }
}

async function waitForKernel(maxMs = 30000) {
  const start = Date.now()
  while (Date.now() - start < maxMs) {
    try {
      await api('GET', '/api/health')
      return true
    } catch {
      await new Promise(r => setTimeout(r, 1000))
      process.stdout.write('.')
    }
  }
  return false
}

// ── Step 1: Wait for kernel ────────────────────────────────────────────────

process.stdout.write('Waiting for kernel')
const up = await waitForKernel()
if (!up) { console.error('\nKernel not reachable. Run ./start.sh first.'); process.exit(1) }
console.log(' ✓')

// ── Step 2: Configure MCP connections from env ─────────────────────────────

console.log('\n[MCP Connections]')

const connections = [
  {
    service: 'github',
    token: process.env.GITHUB_TOKEN,
    env_var: 'GITHUB_TOKEN',
    mcp_package: '@modelcontextprotocol/server-github',
    label: 'GitHub (26 tools: issues, PRs, code, commits)',
  },
  {
    service: 'slack',
    token: process.env.SLACK_BOT_TOKEN,
    env_var: 'SLACK_BOT_TOKEN',
    mcp_package: '@modelcontextprotocol/server-slack',
    label: 'Slack (12 tools: post, search, channels)',
  },
  {
    service: 'linear',
    token: process.env.LINEAR_API_KEY,
    env_var: 'LINEAR_API_KEY',
    mcp_package: '@linear/mcp-server',
    label: 'Linear (issues, projects, teams)',
  },
  {
    service: 'notion',
    token: process.env.NOTION_TOKEN,
    env_var: 'NOTION_TOKEN',
    mcp_package: '@modelcontextprotocol/server-notion',
    label: 'Notion (pages, databases)',
  },
  {
    service: 'filesystem',
    token: null,
    env_var: null,
    mcp_package: '@modelcontextprotocol/server-filesystem',
    extra_arg: process.env.FILESYSTEM_PATH || process.env.HOME || '/',
    label: `Filesystem (root: ${process.env.FILESYSTEM_PATH || process.env.HOME || '/'})`,
  },
]

for (const c of connections) {
  if (c.service !== 'filesystem' && (!c.token || c.token.startsWith('REPLACE') || c.token.startsWith('xoxb_REPLACE') || c.token.startsWith('lin_api_REPLACE') || c.token.startsWith('secret_REPLACE'))) {
    console.log(`  ⚠  ${c.service}: no token (registered without MCP — agents will queue but skip live calls)`)
    continue
  }
  try {
    const r = await api('POST', '/api/connections/setup', {
      service: c.service,
      token: c.token || '',
      env_var: c.env_var || '',
      mcp_package: c.mcp_package,
      extra_arg: c.extra_arg || '',
    })
    const tools = r.tool_count || 0
    const status = r.mcp_active ? `✓ ${tools} tools` : `⚠ registered (MCP not yet started)`
    console.log(`  ${status}  ${c.label}`)
  } catch (e) {
    console.log(`  ✕  ${c.service}: ${e.message}`)
  }
}

// ── Step 3: Run the incident response seed ─────────────────────────────────

console.log('\n[Running seed: incident-response pipeline]')

// Dynamically import so env is fully set before seed runs
const { default: runSeed } = await import('./seed_incident_pipeline.mjs')
