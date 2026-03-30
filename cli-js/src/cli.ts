#!/usr/bin/env node
/**
 * CLOVE CLI — the product surface.
 *
 * clove start       Start kernel + open dashboard
 * clove stop        Stop kernel
 * clove status      Show kernel health, agents, cost
 * clove run "goal"  Run an agent
 * clove fleet "goal" -n 3  Run a fleet
 * clove recall      Show shared memory
 * clove logs        Show recent audit
 * clove dashboard   Open dashboard in browser
 * clove openclaw    Spawn OpenClaw instance
 */

import { spawn, execSync, type ChildProcess } from 'node:child_process'
import { existsSync, readFileSync, writeFileSync, mkdirSync, readdirSync } from 'node:fs'
import { homedir } from 'node:os'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

// ── Config ──────────────────────────────────────────────────────────────

const CLOVE_DIR = join(homedir(), '.clove')
const PID_FILE = join(CLOVE_DIR, 'kernel.pid')
const SCHEDULER_PID = join(CLOVE_DIR, 'scheduler.pid')
const SCHEDULER_STATE = join(CLOVE_DIR, 'scheduler-state.json')
const CONFIG_FILE = join(CLOVE_DIR, 'config.json')
const USER_AGENTS = join(CLOVE_DIR, 'agents')
const DEFAULT_PORT = 8080
const __cli_dir = dirname(fileURLToPath(import.meta.url))
const TEMPLATES_DIR = join(__cli_dir, '..', '..', 'templates')

interface CloveConfig {
  kernelPath: string
  apiPort: number
  openrouterKey: string
  privacy: boolean
  privacyMode: string
  sandbox: boolean
}

function loadConfig(): CloveConfig {
  mkdirSync(CLOVE_DIR, { recursive: true })
  const defaults: CloveConfig = {
    kernelPath: '',
    apiPort: DEFAULT_PORT,
    openrouterKey: process.env['OPENROUTER_API_KEY'] || '',
    privacy: true,
    privacyMode: 'redact',
    sandbox: true,
  }
  if (existsSync(CONFIG_FILE)) {
    try {
      const raw = JSON.parse(readFileSync(CONFIG_FILE, 'utf-8'))
      return { ...defaults, ...raw }
    } catch {}
  }
  return defaults
}

function saveConfig(cfg: CloveConfig): void {
  mkdirSync(CLOVE_DIR, { recursive: true })
  writeFileSync(CONFIG_FILE, JSON.stringify(cfg, null, 2))
}

// ── Kernel binary detection ─────────────────────────────────────────────

const SEARCH_PATHS = [
  process.env['CLOVE_KERNEL_PATH'],
  join(CLOVE_DIR, 'bin', 'clove_kernel'),
  '/usr/local/bin/clove_kernel',
  '/opt/homebrew/bin/clove_kernel',
  // Dev paths
  join(process.cwd(), 'build', 'kernel', 'clove_kernel'),
  join(homedir(), 'Documents', 'clove-v2', 'build', 'kernel', 'clove_kernel'),
]

function findKernel(configPath: string): string | null {
  if (configPath && existsSync(configPath)) return configPath
  for (const p of SEARCH_PATHS) {
    if (p && existsSync(p)) return p
  }
  try {
    const result = execSync('which clove_kernel', { encoding: 'utf-8' }).trim()
    if (result && existsSync(result)) return result
  } catch {}
  return null
}

// ── API helpers ─────────────────────────────────────────────────────────

async function api<T>(port: number, path: string, method = 'GET', body?: unknown): Promise<T | null> {
  try {
    const opts: RequestInit = { method, headers: { 'Content-Type': 'application/json' } }
    if (body) opts.body = JSON.stringify(body)
    const res = await fetch(`http://localhost:${port}${path}`, opts)
    return await res.json() as T
  } catch {
    return null
  }
}

async function isRunning(port: number): Promise<boolean> {
  const h = await api<{ status: string }>(port, '/api/health')
  return h?.status === 'ok'
}

// ── Colors ──────────────────────────────────────────────────────────────

const c = {
  green: (s: string) => `\x1b[32m${s}\x1b[0m`,
  red: (s: string) => `\x1b[31m${s}\x1b[0m`,
  cyan: (s: string) => `\x1b[36m${s}\x1b[0m`,
  dim: (s: string) => `\x1b[2m${s}\x1b[0m`,
  bold: (s: string) => `\x1b[1m${s}\x1b[0m`,
  yellow: (s: string) => `\x1b[33m${s}\x1b[0m`,
}

// ── Commands ────────────────────────────────────────────────────────────

async function cmdStart(): Promise<void> {
  const cfg = loadConfig()

  if (await isRunning(cfg.apiPort)) {
    console.log(c.green('CLOVE already running') + c.dim(` on port ${cfg.apiPort}`))
    return
  }

  const kernel = findKernel(cfg.kernelPath)
  if (!kernel) {
    console.log(c.red('Kernel binary not found.'))
    console.log('')
    console.log('Options:')
    console.log('  1. Build from source:')
    console.log(c.dim('     git clone https://github.com/aniiiiXD/Clove.git'))
    console.log(c.dim('     cd Clove && git checkout v2'))
    console.log(c.dim('     mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release'))
    console.log(c.dim('     cmake --build . -j$(sysctl -n hw.ncpu)'))
    console.log('')
    console.log('  2. Set the path:')
    console.log(c.dim('     clove config set kernelPath /path/to/clove_kernel'))
    return
  }

  if (!cfg.openrouterKey) {
    console.log(c.yellow('No LLM API key.') + ' Get one from https://openrouter.ai/keys')
    console.log(c.dim('  export OPENROUTER_API_KEY=sk-or-v1-...'))
    console.log(c.dim('  # or: clove config set openrouterKey sk-or-v1-...'))
    return
  }

  const args = ['--api', '--api-port', String(cfg.apiPort)]
  if (cfg.sandbox) args.push('--sandbox')
  if (cfg.privacy) args.push('--privacy', '--privacy-mode', cfg.privacyMode)
  args.push('--openrouter', '--openrouter-key', cfg.openrouterKey)

  console.log(c.cyan('Starting CLOVE...'))

  const child = spawn(kernel, args, { detached: true, stdio: 'ignore' })
  child.unref()

  if (child.pid) {
    writeFileSync(PID_FILE, String(child.pid))
  }

  // Wait for API
  const deadline = Date.now() + 10000
  while (Date.now() < deadline) {
    if (await isRunning(cfg.apiPort)) {
      console.log('')
      console.log(c.bold(c.green('  CLOVE READY')))
      console.log('')
      console.log(`  API:        http://localhost:${cfg.apiPort}`)
      console.log(`  Dashboard:  http://localhost:${cfg.apiPort}/dashboard`)
      console.log(`  Kernel:     ${kernel} ${c.dim(`(PID ${child.pid})`)}`)
      console.log('')
      console.log(c.dim('  clove run "your goal"    Run an agent'))
      console.log(c.dim('  clove fleet "goal" -n 3  Run a fleet'))
      console.log(c.dim('  clove status             Check health'))
      console.log(c.dim('  clove stop               Shutdown'))
      console.log('')

      // Open dashboard
      try { execSync(`open http://localhost:${cfg.apiPort}/dashboard`, { stdio: 'ignore' }) } catch {}
      return
    }
    await new Promise(r => setTimeout(r, 200))
  }
  console.log(c.red('Failed to start within 10s. Check logs.'))
}

async function cmdStop(): Promise<void> {
  try {
    const pid = parseInt(readFileSync(PID_FILE, 'utf-8').trim(), 10)
    process.kill(pid, 'SIGTERM')
    console.log(c.green(`Stopped`) + c.dim(` (PID ${pid})`))
    try { require('fs').unlinkSync(PID_FILE) } catch {}
  } catch {
    try {
      execSync('pkill -f clove_kernel', { stdio: 'ignore' })
      console.log(c.green('Stopped'))
    } catch {
      console.log(c.dim('Not running'))
    }
  }
}

async function cmdStatus(): Promise<void> {
  const cfg = loadConfig()
  const health = await api<{ status: string; version: string; uptime_s: number; syscall_count: number }>(cfg.apiPort, '/api/health')

  if (!health) {
    console.log(c.red('CLOVE not running'))
    console.log(c.dim('  clove start'))
    return
  }

  const cost = await api<{ total_cost_usd: number }>(cfg.apiPort, '/api/cost')
  const overview = await api<{ agents: unknown[]; memory_blocks: number; activity: unknown[] }>(cfg.apiPort, '/api/sandbox/overview')
  const history = await api<{ count: number }>(cfg.apiPort, '/api/history')

  const uptime = health.uptime_s
  const uptimeStr = uptime > 3600 ? `${Math.floor(uptime / 3600)}h ${Math.floor((uptime % 3600) / 60)}m`
    : uptime > 60 ? `${Math.floor(uptime / 60)}m ${uptime % 60}s` : `${uptime}s`

  console.log('')
  console.log(c.bold('  CLOVE Status'))
  console.log(c.dim('  ─────────────────────────────'))
  console.log(`  Status:     ${c.green('running')}`)
  console.log(`  Version:    ${health.version}`)
  console.log(`  Uptime:     ${uptimeStr}`)
  console.log(`  Syscalls:   ${health.syscall_count}`)
  console.log(`  Agents:     ${overview?.agents.length ?? 0}`)
  console.log(`  Runs:       ${history?.count ?? 0}`)
  console.log(`  Memory:     ${overview?.memory_blocks ?? 0} blocks`)
  console.log(`  Cost:       $${(cost?.total_cost_usd ?? 0).toFixed(6)}`)
  console.log(`  API:        http://localhost:${cfg.apiPort}`)
  console.log(c.dim('  ─────────────────────────────'))
  console.log('')
}

async function cmdRun(goal: string, budget = 0.50): Promise<void> {
  const cfg = loadConfig()
  if (!await isRunning(cfg.apiPort)) {
    console.log(c.red('CLOVE not running.') + c.dim(' Run: clove start'))
    return
  }

  console.log(c.cyan('Running agent...'))
  console.log(c.dim(`  Goal: ${goal}`))
  console.log(c.dim(`  Budget: $${budget}`))
  console.log('')

  const result = await api<{
    success: boolean; content: string; total_cost_usd: number;
    steps: number; total_tokens: number;
    step_log: Array<{ step: number; tool: string; args: unknown }>
  }>(cfg.apiPort, '/api/run', 'POST', { goal, budget })

  if (!result) {
    console.log(c.red('Failed to reach kernel'))
    return
  }

  // Step log
  if (result.step_log?.length) {
    for (const s of result.step_log) {
      const args = typeof s.args === 'string' ? s.args : JSON.stringify(s.args)
      console.log(c.dim(`  ${s.step}. `) + c.yellow(s.tool) + c.dim(`(${args.slice(0, 60)})`))
    }
    console.log('')
  }

  // Result
  console.log(result.content)
  console.log('')
  console.log(c.dim(`${result.success ? c.green('OK') : c.red('FAILED')} | ${result.steps} steps | ${result.total_tokens} tokens | $${result.total_cost_usd.toFixed(6)}`))
}

async function cmdFleet(goal: string, agents = 3, budget = 1.0): Promise<void> {
  const cfg = loadConfig()
  if (!await isRunning(cfg.apiPort)) {
    console.log(c.red('CLOVE not running.') + c.dim(' Run: clove start'))
    return
  }

  console.log(c.cyan(`Launching ${agents}-agent fleet...`))
  console.log(c.dim(`  Goal: ${goal}`))
  console.log(c.dim(`  Budget: $${budget}`))
  console.log('')

  const res = await fetch(`http://localhost:${cfg.apiPort}/api/fleet`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ goal, agents, budget }),
  })

  const reader = res.body?.getReader()
  if (!reader) { console.log(c.red('No stream')); return }

  const decoder = new TextDecoder()
  let buffer = ''

  while (true) {
    const { done, value } = await reader.read()
    if (done) break
    buffer += decoder.decode(value, { stream: true })
    const lines = buffer.split('\n')
    buffer = lines.pop() || ''

    for (const line of lines) {
      if (!line.startsWith('data: ')) continue
      try {
        const event = JSON.parse(line.slice(6)) as { type: string; data: Record<string, unknown> }
        printFleetEvent(event)
      } catch {}
    }
  }
}

function printFleetEvent(ev: { type: string; data: Record<string, unknown> }): void {
  const d = ev.data
  switch (ev.type) {
    case 'fleet_start':
      console.log(c.cyan(`  Fleet: ${d.agent_count} agents, $${(d.total_budget_usd as number)?.toFixed(2)} budget`))
      break
    case 'agent_event':
      if (d.event_type === 'tool_call')
        console.log(c.dim(`  [${d.agent}] `) + c.yellow(String(d.tool)) + c.dim(`(${JSON.stringify(d.args).slice(0, 50)})`))
      else if (d.event_type === 'done')
        console.log(c.green(`  [${d.agent}] done`) + c.dim(` $${(d.cost_usd as number)?.toFixed(4)}`))
      break
    case 'agent_done':
      console.log(c.dim(`  ${d.agent} complete (${d.completed}/${d.total})`))
      break
    case 'synthesizing':
      console.log(c.cyan('  Synthesizing...'))
      break
    case 'fleet_done':
      console.log('')
      console.log(c.bold(c.green('  Fleet complete')))
      console.log(c.dim(`  ${d.agent_count} agents | $${(d.total_cost_usd as number)?.toFixed(4)} | ${(d.total_tokens as number)?.toLocaleString()} tokens`))
      break
  }
}

async function cmdRecall(): Promise<void> {
  const cfg = loadConfig()
  const result = await api<{ blocks: Array<{ name: string; type: string; content: string; access: string }>; count: number }>(cfg.apiPort, '/api/memory')
  if (!result || result.count === 0) {
    console.log(c.dim('No memory blocks'))
    return
  }
  console.log(c.bold(`  ${result.count} memory block(s)`))
  console.log('')
  for (const b of result.blocks) {
    console.log(c.cyan(`  [${b.type}/${b.access}] ${b.name}`))
    console.log(`  ${b.content.slice(0, 200)}`)
    console.log('')
  }
}

async function cmdLogs(limit = 20): Promise<void> {
  const cfg = loadConfig()
  const entries = await api<Array<{ event_type: string; agent_name: string; category: string; timestamp: string; success: boolean }>>(cfg.apiPort, `/api/audit?limit=${limit}`)
  if (!entries?.length) {
    console.log(c.dim('No audit entries'))
    return
  }
  for (const e of entries) {
    const icon = e.success ? c.green('OK') : c.red('!!')
    const ts = e.timestamp?.slice(11, 19) || ''
    console.log(`  ${c.dim(ts)} ${icon} ${c.cyan(e.event_type.padEnd(25))} ${e.agent_name || c.dim('#0')}`)
  }
}

async function cmdDashboard(): Promise<void> {
  const cfg = loadConfig()
  if (!await isRunning(cfg.apiPort)) {
    console.log(c.red('CLOVE not running.') + c.dim(' Run: clove start'))
    return
  }
  console.log(`  Opening http://localhost:${cfg.apiPort}/dashboard`)
  try { execSync(`open http://localhost:${cfg.apiPort}/dashboard`, { stdio: 'ignore' }) } catch {}
}

async function cmdConfig(args: string[]): Promise<void> {
  const cfg = loadConfig()
  if (args.length === 0) {
    console.log(JSON.stringify(cfg, null, 2))
    return
  }
  if (args[0] === 'set' && args.length >= 3) {
    const key = args[1] as keyof CloveConfig
    let value: string | number | boolean = args[2]
    if (value === 'true') value = true
    else if (value === 'false') value = false
    else if (!isNaN(Number(value))) value = Number(value)
    ;(cfg as unknown as Record<string, unknown>)[key] = value
    saveConfig(cfg)
    console.log(c.green(`Set ${key} = ${value}`))
    return
  }
  if (args[0] === 'get' && args.length >= 2) {
    const key = args[1] as keyof CloveConfig
    console.log((cfg as unknown as Record<string, unknown>)[key])
    return
  }
  console.log('Usage: clove config [set <key> <value> | get <key>]')
}

// ── Templates ──────────────────────────────────────────────────────────

function loadRegistry(): Array<{ name: string; displayName: string; category: string; description: string; path: string; mode: string }> {
  const p = join(TEMPLATES_DIR, 'registry.json')
  if (existsSync(p)) { try { return JSON.parse(readFileSync(p, 'utf-8')).templates || [] } catch {} }
  return []
}

async function cmdTemplates(): Promise<void> {
  const reg = loadRegistry()
  const user: string[] = []
  if (existsSync(USER_AGENTS)) for (const d of readdirSync(USER_AGENTS)) { if (existsSync(join(USER_AGENTS, d, 'agent.yaml'))) user.push(d) }
  if (!reg.length && !user.length) { console.log(c.dim('No templates found.')); return }
  console.log(''); console.log(c.bold('  Agent Templates'))
  console.log(c.dim('  ─────────────────────────────────────────────'))
  for (const t of reg) {
    const mode = t.mode === 'fleet' ? c.yellow('fleet') : c.dim('single')
    console.log(`  ${c.cyan(t.name.padEnd(22))} ${c.dim(t.category.padEnd(12))} ${mode}  ${t.description.slice(0, 50)}`)
  }
  if (user.length) { console.log(''); console.log(c.dim('  User:')); for (const n of user) console.log(`  ${c.cyan(n)}`) }
  console.log(''); console.log(c.dim('  Deploy: clove deploy <name> --param topic="AI agents"')); console.log('')
}

async function cmdCreate(name: string): Promise<void> {
  if (!name) { console.log('Usage: clove create <agent-name>'); return }
  const dir = join(USER_AGENTS, name); mkdirSync(dir, { recursive: true })
  writeFileSync(join(dir, 'agent.yaml'), `apiVersion: clove/v1\nkind: AgentTemplate\nmetadata:\n  name: "${name}"\n  displayName: "${name.replace(/-/g, ' ').replace(/\b\w/g, c => c.toUpperCase())}"\n  description: "Describe what this agent does."\n  category: "dev"\n  author: "user"\n  version: "1.0.0"\n  tags: []\n\nspec:\n  mode: "single"\n  goal: "{{goal}}"\n  budget: 0.50\n  max_steps: 15\n  tools: ["search", "http", "read_file", "write_file", "exec"]\n\nparameters:\n  - name: "goal"\n    type: "string"\n    label: "Goal"\n    required: true\n`)
  console.log(c.green(`Created: ${join(dir, 'agent.yaml')}`))
  console.log(c.dim('  Edit the YAML, then: clove deploy ' + name))
}

function findTpl(name: string): string | null {
  const u = join(USER_AGENTS, name, 'agent.yaml'); if (existsSync(u)) return u
  const reg = loadRegistry(); const e = reg.find(t => t.name === name)
  if (e) { const p = join(TEMPLATES_DIR, e.path); if (existsSync(p)) return p }
  if (existsSync(name)) return name
  return null
}

function parseYaml(text: string): Record<string, unknown> {
  const lines = text.split('\n'); const result: Record<string, unknown> = {}
  const stack: Array<{ obj: Record<string, unknown>; indent: number }> = [{ obj: result, indent: -1 }]
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].replace(/\r$/, ''); if (!line.trim() || line.trim().startsWith('#')) continue
    const indent = line.search(/\S/); const content = line.trim()
    while (stack.length > 1 && stack[stack.length - 1].indent >= indent) stack.pop()
    const cur = stack[stack.length - 1].obj
    if (content.startsWith('- ')) { const pk = Object.keys(cur).pop(); if (pk && Array.isArray(cur[pk])) { const item = content.slice(2).trim(); if (item.includes(': ')) { const o: Record<string, unknown> = {}; const ci = item.indexOf(': '); o[item.slice(0, ci).trim().replace(/^["']|["']$/g, '')] = pv(item.slice(ci + 2).trim()); (cur[pk] as unknown[]).push(o); stack.push({ obj: o, indent: indent + 2 }) } else { (cur[pk] as unknown[]).push(pv(item)) } }; continue }
    const ci = content.indexOf(': ')
    if (ci > 0 || content.endsWith(':')) {
      const key = content.slice(0, ci > 0 ? ci : content.length - 1).trim().replace(/^["']|["']$/g, '')
      const rv = ci > 0 ? content.slice(ci + 2).trim() : ''
      if (!rv) { const nl = lines[i + 1] || ''; if (nl.trim().startsWith('- ')) cur[key] = []; else { const o: Record<string, unknown> = {}; cur[key] = o; stack.push({ obj: o, indent }) } }
      else if (rv.startsWith('[') && rv.endsWith(']')) cur[key] = rv.slice(1, -1).split(',').map(s => pv(s.trim())).filter(v => v !== '')
      else cur[key] = pv(rv)
    }
  }
  return result
}
function pv(r: string): unknown { if (!r) return ''; if ((r[0]==='"'&&r.at(-1)==='"')||(r[0]==="'"&&r.at(-1)==="'")) return r.slice(1,-1); if (r==='true') return true; if (r==='false') return false; if (/^-?\d+(\.\d+)?$/.test(r)) return Number(r); return r }

async function cmdDeploy(name: string, cliArgs: string[]): Promise<void> {
  if (!name) { console.log('Usage: clove deploy <name> [--param key=value ...]'); return }
  const cfg = loadConfig(); if (!await isRunning(cfg.apiPort)) { console.log(c.red('Not running.') + c.dim(' clove start')); return }
  const path = findTpl(name); if (!path) { console.log(c.red(`Template not found: ${name}`)); return }
  const tpl = parseYaml(readFileSync(path, 'utf-8'))
  const spec = tpl.spec as Record<string, unknown> || {}; const params: Record<string, string> = {}
  for (let i = 0; i < cliArgs.length; i++) { if (cliArgs[i] === '--param' && cliArgs[i+1]) { const eq = cliArgs[i+1].indexOf('='); if (eq > 0) params[cliArgs[i+1].slice(0, eq)] = cliArgs[i+1].slice(eq+1); i++ } }
  let goal = String(spec.goal || '').replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] || '')
  const budget = (spec.budget as number) || 0.5
  const meta = tpl.metadata as Record<string, unknown> || {}
  console.log(c.cyan(`Deploying: ${meta.displayName || name}`))
  console.log(c.dim(`  ${goal.slice(0, 80)}`)); console.log('')
  if (spec.mode === 'fleet') await cmdFleet(goal, (spec.agents as number) || 3, budget)
  else await cmdRun(goal, budget)
}

// ── Scheduler ──────────────────────────────────────────────────────────

function cronMatch(cron: string, now: Date): boolean {
  const p = cron.trim().split(/\s+/); if (p.length !== 5) return false
  const f = [now.getMinutes(), now.getHours(), now.getDate(), now.getMonth() + 1, now.getDay()]
  return p.every((pat, i) => { if (pat === '*') return true; if (pat.startsWith('*/')) { const s = +pat.slice(2); return s > 0 && f[i] % s === 0 }; return pat.split(',').some(v => { if (v.includes('-')) { const [a,b] = v.split('-').map(Number); return f[i] >= a && f[i] <= b }; return +v === f[i] }) })
}

async function cmdScheduler(): Promise<void> {
  const cfg = loadConfig(); console.log(c.cyan('Scheduler started'))
  while (true) {
    try {
      // Schedules
      const sr = await api<{ schedules: Array<{ name: string; cron: string; enabled: boolean; run: Record<string, unknown> }> }>(cfg.apiPort, '/api/schedules')
      if (sr?.schedules) {
        const state = existsSync(SCHEDULER_STATE) ? JSON.parse(readFileSync(SCHEDULER_STATE, 'utf-8')) : { fired: {}, lastAudit: 0 }
        const now = new Date(); const mk = `${now.getFullYear()}-${now.getMonth()}-${now.getDate()}-${now.getHours()}-${now.getMinutes()}`
        for (const s of sr.schedules) {
          if (!s.enabled) continue; const fk = `${s.name}:${mk}`; if (state.fired[fk]) continue
          if (cronMatch(s.cron, now)) { console.log(c.yellow(`  Fire: ${s.name}`)); await api(cfg.apiPort, '/api/run', 'POST', s.run); state.fired[fk] = Date.now() }
        }
        const keys = Object.keys(state.fired); if (keys.length > 100) for (const k of keys.slice(0, keys.length - 100)) delete state.fired[k]
        writeFileSync(SCHEDULER_STATE, JSON.stringify(state))
      }
      // Webhooks
      const wr = await api<{ webhooks: Array<{ url: string; events: string[]; enabled: boolean }> }>(cfg.apiPort, '/api/webhooks')
      if (wr?.webhooks?.filter(w => w.enabled).length) {
        const state = JSON.parse(readFileSync(SCHEDULER_STATE, 'utf-8'))
        const audit = await api<Array<{ id: number; event_type: string; success: boolean; timestamp: string; details: Record<string, unknown> }>>(cfg.apiPort, '/api/audit?limit=30')
        if (audit) {
          const newE = audit.filter(e => e.id > (state.lastAudit || 0))
          if (newE.length) { state.lastAudit = Math.max(...newE.map(e => e.id)); writeFileSync(SCHEDULER_STATE, JSON.stringify(state)) }
          for (const e of newE) {
            const evt = e.event_type === 'RUN_COMPLETE' ? (e.success ? 'run_complete' : 'run_failed') : e.event_type === 'BUDGET_EXCEEDED' ? 'budget_exceeded' : null
            if (!evt) continue
            for (const w of wr.webhooks.filter(w => w.enabled && w.events.includes(evt))) {
              try { await fetch(w.url, { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Clove-Event': evt }, body: JSON.stringify({ event: evt, timestamp: e.timestamp, data: e.details }), signal: AbortSignal.timeout(10000) }) } catch {}
            }
          }
        }
      }
    } catch {}
    await new Promise(r => setTimeout(r, 60000))
  }
}

// ── Connect ────────────────────────────────────────────────────────────

const CONNECTIONS_FILE = join(CLOVE_DIR, 'connections.json')

function loadConnections(): Array<{ name: string; type: string; command?: string; credentials: Record<string, string>; status: string }> {
  if (existsSync(CONNECTIONS_FILE)) { try { return JSON.parse(readFileSync(CONNECTIONS_FILE, 'utf-8')).connections || [] } catch {} }
  return []
}
function saveConnections(conns: Array<Record<string, unknown>>): void {
  writeFileSync(CONNECTIONS_FILE, JSON.stringify({ connections: conns }, null, 2))
}

const KNOWN_SERVICES: Record<string, { command: string; args: string[]; tokenEnv: string; tokenPrompt: string }> = {
  github: { command: 'npx', args: ['@modelcontextprotocol/server-github'], tokenEnv: 'GITHUB_TOKEN', tokenPrompt: 'GitHub Personal Access Token' },
  slack: { command: 'npx', args: ['@modelcontextprotocol/server-slack'], tokenEnv: 'SLACK_BOT_TOKEN', tokenPrompt: 'Slack Bot Token (xoxb-...)' },
  filesystem: { command: 'npx', args: ['@modelcontextprotocol/server-filesystem', '/tmp'], tokenEnv: '', tokenPrompt: '' },
  postgres: { command: 'npx', args: ['@modelcontextprotocol/server-postgres'], tokenEnv: 'DATABASE_URL', tokenPrompt: 'PostgreSQL connection string' },
  notion: { command: 'npx', args: ['@modelcontextprotocol/server-notion'], tokenEnv: 'NOTION_TOKEN', tokenPrompt: 'Notion integration token' },
  google_drive: { command: 'npx', args: ['@modelcontextprotocol/server-gdrive'], tokenEnv: 'GDRIVE_CREDENTIALS', tokenPrompt: 'Google Drive credentials path' },
}

async function cmdConnect(args: string[]): Promise<void> {
  const sub = args[0]

  if (!sub || sub === 'list' || sub === 'ls') {
    const conns = loadConnections()
    if (!conns.length) {
      console.log(''); console.log(c.dim('  No connections. Connect a service:'))
      console.log(c.dim('  clove connect github'))
      console.log(c.dim('  clove connect slack'))
      console.log('')
      console.log(c.dim('  Available: ' + Object.keys(KNOWN_SERVICES).join(', ')))
      console.log('')
      return
    }
    console.log(''); console.log(c.bold('  Connections'))
    console.log(c.dim('  ─────────────────────────────────────'))
    for (const conn of conns) {
      const st = conn.status === 'active' ? c.green('active') : c.dim(conn.status)
      console.log(`  ${c.cyan(conn.name.padEnd(16))} ${st.padEnd(18)} ${c.dim(conn.type)}`)
    }
    console.log('')
    return
  }

  if (sub === 'remove' || sub === 'rm') {
    const name = args[1]; if (!name) { console.log('Usage: clove connect remove <name>'); return }
    const conns = loadConnections().filter(c => c.name !== name)
    saveConnections(conns)
    console.log(c.green(`Removed connection: ${name}`))
    return
  }

  // Connect a service
  const service = KNOWN_SERVICES[sub]
  if (!service) {
    console.log(c.red(`Unknown service: ${sub}`))
    console.log(c.dim('  Available: ' + Object.keys(KNOWN_SERVICES).join(', ')))
    console.log(c.dim('  Or: clove connect add <name> --type mcp --command "npx @some/server"'))
    return
  }

  // Check for token
  let token = ''
  if (service.tokenEnv) {
    token = process.env[service.tokenEnv] || ''
    if (!token && args[1]) token = args[1]
    if (!token) {
      console.log(c.yellow(`  ${service.tokenPrompt} required.`))
      console.log(c.dim(`  Set it: export ${service.tokenEnv}=your-token`))
      console.log(c.dim(`  Or:     clove connect ${sub} <token>`))
      return
    }
  }

  // Save connection
  const conns = loadConnections().filter(c => c.name !== sub)
  const newConn: Record<string, unknown> = {
    name: sub,
    type: 'mcp',
    command: service.command,
    args: service.args,
    credentials: token ? { [service.tokenEnv]: token } : {},
    status: 'active',
  }
  conns.push(newConn as never)
  saveConnections(conns)

  // Also add to MCP config
  const mcpPath = join(CLOVE_DIR, 'mcp.yaml')
  let mcpContent = existsSync(mcpPath) ? readFileSync(mcpPath, 'utf-8') : 'servers:\n'
  if (!mcpContent.includes(`name: "${sub}"`)) {
    mcpContent += `  - name: "${sub}"\n    command: ${service.command}\n    args: [${service.args.map(a => `"${a}"`).join(', ')}]\n`
    if (token) mcpContent += `    env:\n      ${service.tokenEnv}: "${token}"\n`
    writeFileSync(mcpPath, mcpContent)
  }

  console.log(c.green(`  Connected: ${sub}`))
  console.log(c.dim(`  MCP server config saved to ~/.clove/mcp.yaml`))
  console.log(c.dim(`  Restart kernel to activate: clove stop && clove start`))
}

// ── Agent Management ──────────────────────────────────────────────────

const REGISTRY_PORT = 8090

async function cmdAgent(args: string[]): Promise<void> {
  const sub = args[0] || 'list'

  if (sub === 'list' || sub === 'ls') {
    // Try registry first, fall back to local files
    const agents = loadLocalAgents()
    if (!agents.length) {
      console.log(''); console.log(c.dim('  No agents defined.'))
      console.log(c.dim('  Create one: clove agent create <name>'))
      console.log(''); return
    }
    console.log(''); console.log(c.bold('  Agents'))
    console.log(c.dim('  ─────────────────────────────────────────────'))
    for (const a of agents) {
      const status = a.enabled ? c.green('enabled') : c.dim('disabled')
      const triggers = (a.triggers || []).map((t: Record<string, unknown>) => t.type).join(', ') || c.dim('manual')
      console.log(`  ${c.cyan(a.name.padEnd(20))} ${status.padEnd(18)} ${c.dim(triggers.padEnd(16))} ${a.description?.slice(0, 40) || ''}`)
    }
    console.log('')
    return
  }

  if (sub === 'create') {
    const name = args[1]; if (!name) { console.log('Usage: clove agent create <name>'); return }
    const dir = join(USER_AGENTS, name); mkdirSync(dir, { recursive: true })
    const agent = {
      name,
      description: '',
      enabled: false,
      connections: [],
      triggers: [{ type: 'manual' }],
      action: {
        goal: 'Describe what this agent should do when triggered.',
        tools: ['search', 'http', 'read_file', 'exec'],
        max_steps: 10,
      },
      permissions: { can_exec: false, can_read: true, can_write: false, can_http: true, allowed_domains: [], allowed_paths: [] },
      budget: { per_run: 0.20, daily_max: 5.0, daily_spent: 0, last_reset: new Date().toISOString().slice(0, 10) },
      memory: [],
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    }
    writeFileSync(join(dir, 'agent.json'), JSON.stringify(agent, null, 2))
    console.log(c.green(`  Created agent: ${name}`))
    console.log(c.dim(`  Config: ~/.clove/agents/${name}/agent.json`))
    console.log(c.dim(`  Edit the config, then: clove agent enable ${name}`))
    return
  }

  if (sub === 'enable') {
    const name = args[1]; if (!name) { console.log('Usage: clove agent enable <name>'); return }
    const agent = loadAgent(name); if (!agent) { console.log(c.red(`Agent not found: ${name}`)); return }
    agent.enabled = true; agent.updated_at = new Date().toISOString()
    writeFileSync(join(USER_AGENTS, name, 'agent.json'), JSON.stringify(agent, null, 2))
    console.log(c.green(`  Enabled: ${name}`))
    return
  }

  if (sub === 'disable') {
    const name = args[1]; if (!name) { console.log('Usage: clove agent disable <name>'); return }
    const agent = loadAgent(name); if (!agent) { console.log(c.red(`Agent not found: ${name}`)); return }
    agent.enabled = false; agent.updated_at = new Date().toISOString()
    writeFileSync(join(USER_AGENTS, name, 'agent.json'), JSON.stringify(agent, null, 2))
    console.log(c.green(`  Disabled: ${name}`))
    return
  }

  if (sub === 'run') {
    const name = args[1]; if (!name) { console.log('Usage: clove agent run <name>'); return }
    const cfg = loadConfig(); if (!await isRunning(cfg.apiPort)) { console.log(c.red('Kernel not running.')); return }
    const agent = loadAgent(name); if (!agent) { console.log(c.red(`Agent not found: ${name}`)); return }
    console.log(c.cyan(`  Running: ${name}`))
    console.log(c.dim(`  ${agent.action.goal.slice(0, 60)}`))
    console.log('')
    await cmdRun(agent.action.goal, agent.budget.per_run)
    return
  }

  if (sub === 'show' || sub === 'info') {
    const name = args[1]; if (!name) { console.log('Usage: clove agent show <name>'); return }
    const agent = loadAgent(name); if (!agent) { console.log(c.red(`Agent not found: ${name}`)); return }
    console.log(''); console.log(c.bold(`  ${agent.name}`))
    console.log(c.dim('  ─────────────────────────────────────'))
    console.log(`  Status:      ${agent.enabled ? c.green('enabled') : c.dim('disabled')}`)
    console.log(`  Description: ${agent.description || c.dim('none')}`)
    console.log(`  Triggers:    ${agent.triggers.map((t: Record<string, unknown>) => `${t.type}${t.schedule ? ' ' + t.schedule : ''}`).join(', ')}`)
    console.log(`  Tools:       ${agent.action.tools.join(', ')}`)
    console.log(`  Connections: ${agent.connections.length ? agent.connections.join(', ') : c.dim('none')}`)
    console.log(`  Budget:      $${agent.budget.per_run}/run, $${agent.budget.daily_max}/day (spent: $${agent.budget.daily_spent})`)
    console.log(`  Max steps:   ${agent.action.max_steps}`)
    console.log(`  Goal:`)
    console.log(c.dim(`    ${agent.action.goal.slice(0, 200)}`))
    console.log('')
    return
  }

  if (sub === 'delete' || sub === 'rm') {
    const name = args[1]; if (!name) { console.log('Usage: clove agent delete <name>'); return }
    const dir = join(USER_AGENTS, name)
    if (existsSync(dir)) {
      const { rmSync } = await import('node:fs')
      rmSync(dir, { recursive: true })
      console.log(c.green(`  Deleted: ${name}`))
    } else { console.log(c.red(`Agent not found: ${name}`)) }
    return
  }

  console.log('Usage: clove agent [list|create|enable|disable|run|show|delete] <name>')
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function loadLocalAgents(): any[] {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const agents: any[] = []
  if (!existsSync(USER_AGENTS)) return agents
  for (const dir of readdirSync(USER_AGENTS)) {
    const p = join(USER_AGENTS, dir, 'agent.json')
    if (existsSync(p)) { try { agents.push(JSON.parse(readFileSync(p, 'utf-8'))) } catch {} }
  }
  return agents
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function loadAgent(name: string): any | null {
  const p = join(USER_AGENTS, name, 'agent.json')
  if (!existsSync(p)) return null
  try { return JSON.parse(readFileSync(p, 'utf-8')) } catch { return null }
}

// ── MCP ────────────────────────────────────────────────────────────────

async function cmdMcp(args: string[]): Promise<void> {
  const cfg = loadConfig()
  const sub = args[0] || 'list'

  if (sub === 'list' || sub === 'ls') {
    if (!await isRunning(cfg.apiPort)) { console.log(c.red('Not running.')); return }
    const servers = await api<{ servers: Array<{ name: string; status: string; tools_count: number }> }>(cfg.apiPort, '/api/mcp/servers')
    const tools = await api<{ tools: Array<{ server_name: string; name: string; description: string }> }>(cfg.apiPort, '/api/mcp/tools')
    console.log(''); console.log(c.bold('  MCP Servers'))
    if (!servers?.servers?.length) { console.log(c.dim('  No servers configured. Add them in ~/.clove/mcp.yaml')); console.log(''); return }
    for (const s of servers.servers) {
      const st = s.status === 'running' ? c.green('running') : c.dim(s.status)
      console.log(`  ${c.cyan(s.name.padEnd(16))} ${st.padEnd(18)} ${s.tools_count} tools`)
    }
    if (tools?.tools?.length) {
      console.log(''); console.log(c.bold('  Tools'))
      for (const t of tools.tools) {
        console.log(`  ${c.dim(t.server_name.padEnd(16))} ${c.cyan(t.name.padEnd(20))} ${c.dim(t.description.slice(0, 50))}`)
      }
    }
    console.log('')
  } else if (sub === 'add') {
    const name = args[1]; const command = args.slice(2).join(' ')
    if (!name || !command) { console.log('Usage: clove mcp add <name> <command>'); return }
    const mcpPath = join(CLOVE_DIR, 'mcp.yaml')
    let content = existsSync(mcpPath) ? readFileSync(mcpPath, 'utf-8') : 'servers:\n'
    content += `  - name: "${name}"\n    command: ${command.split(' ')[0]}\n    args: [${command.split(' ').slice(1).map(a => `"${a}"`).join(', ')}]\n`
    writeFileSync(mcpPath, content)
    console.log(c.green(`Added MCP server: ${name}`))
    console.log(c.dim(`  Restart kernel to apply: clove stop && clove start`))
  } else if (sub === 'remove' || sub === 'rm') {
    console.log(c.dim('Edit ~/.clove/mcp.yaml to remove servers, then restart kernel.'))
  } else {
    console.log('Usage: clove mcp [list|add|remove]')
  }
}

// ── World Commands ─────────────────────────────────────────────────────

async function cmdWorld(args: string[]): Promise<void> {
  const cfg = loadConfig()
  const sub = args[0] || 'list'

  if (sub === 'list' || sub === 'ls') {
    if (!await isRunning(cfg.apiPort)) { console.log(c.red('Not running.')); return }
    const worlds = await api<{ worlds: Array<{ id: number; name: string; member_count: number }> }>(cfg.apiPort, '/api/worlds')
    console.log(''); console.log(c.bold('  Worlds'))
    if (!worlds?.worlds?.length) { console.log(c.dim('  No worlds. Create one: clove world create <name>')); console.log(''); return }
    for (const w of worlds.worlds) {
      const members = w.member_count > 0 ? c.green(`${w.member_count} agents`) : c.dim('empty')
      console.log(`  ${c.cyan(String(w.id).padEnd(4))} ${w.name.padEnd(24)} ${members}`)
    }
    console.log('')
  } else if (sub === 'create') {
    if (!await isRunning(cfg.apiPort)) { console.log(c.red('Not running.')); return }
    const name = args[1]; if (!name) { console.log('Usage: clove world create <name>'); return }
    await api(cfg.apiPort, '/api/worlds', 'POST', { name })
    console.log(c.green(`Created world: ${name}`))
  } else if (sub === 'launch') {
    if (!await isRunning(cfg.apiPort)) { console.log(c.red('Not running.')); return }
    const tplName = args[1]; if (!tplName) { console.log('Usage: clove world launch <template> --param key=value'); return }
    // Parse params
    const params: Record<string, string> = {}
    for (let i = 2; i < args.length; i++) {
      if (args[i] === '--param' && args[i+1]) { const eq = args[i+1].indexOf('='); if (eq > 0) params[args[i+1].slice(0, eq)] = args[i+1].slice(eq+1); i++ }
    }
    console.log(c.cyan(`Launching world: ${tplName}`))
    // For now, delegate to the Python world runner
    const scriptPath = join(__cli_dir, '..', '..', 'examples', 'worlds', `${tplName}.py`)
    if (existsSync(scriptPath)) {
      const paramArgs = Object.entries(params).map(([, v]) => v)
      const child = spawn('python3', [scriptPath, ...paramArgs], { stdio: 'inherit' })
      child.on('close', (code) => { if (code !== 0) console.log(c.red(`Exited with code ${code}`)) })
    } else {
      console.log(c.red(`World template not found: ${tplName}`))
      console.log(c.dim(`  Available: code-health`))
    }
  } else {
    console.log('Usage: clove world [list|create|launch]')
  }
}

// ── Main ────────────────────────────────────────────────────────────────

const args = process.argv.slice(2)
const cmd = args[0] || 'help'

switch (cmd) {
  case 'start':
    cmdStart()
    break
  case 'stop':
    cmdStop()
    break
  case 'status':
  case 's':
    cmdStatus()
    break
  case 'run':
  case 'r': {
    const goal = args.slice(1).filter(a => !a.startsWith('-')).join(' ')
    const budgetIdx = args.indexOf('--budget')
    const budget = budgetIdx >= 0 ? parseFloat(args[budgetIdx + 1]) : 0.50
    if (!goal) { console.log('Usage: clove run "your goal" [--budget 0.50]'); break }
    cmdRun(goal, budget)
    break
  }
  case 'fleet':
  case 'f': {
    const goal = args.slice(1).filter(a => !a.startsWith('-')).join(' ')
    const nIdx = args.indexOf('-n')
    const n = nIdx >= 0 ? parseInt(args[nIdx + 1]) : 3
    const bIdx = args.indexOf('--budget')
    const b = bIdx >= 0 ? parseFloat(args[bIdx + 1]) : 1.0
    if (!goal) { console.log('Usage: clove fleet "your goal" [-n 3] [--budget 1.0]'); break }
    cmdFleet(goal, n, b)
    break
  }
  case 'templates':
  case 'tpl':
    cmdTemplates()
    break
  case 'create':
    cmdCreate(args[1] || '')
    break
  case 'deploy':
  case 'dep':
    cmdDeploy(args[1] || '', args.slice(2))
    break
  case 'scheduler':
    cmdScheduler()
    break
  case 'connect':
  case 'conn':
    cmdConnect(args.slice(1))
    break
  case 'agent':
    cmdAgent(args.slice(1))
    break
  case 'mcp':
    cmdMcp(args.slice(1))
    break
  case 'world':
    cmdWorld(args.slice(1))
    break
  case 'recall':
  case 'memory':
    cmdRecall()
    break
  case 'logs':
  case 'audit': {
    const limit = args[1] ? parseInt(args[1]) : 20
    cmdLogs(limit)
    break
  }
  case 'dashboard':
  case 'd':
    cmdDashboard()
    break
  case 'config':
  case 'c':
    cmdConfig(args.slice(1))
    break
  case 'build': {
    const scriptPath = new URL('../scripts/postinstall.js', import.meta.url).pathname
    try {
      execSync(`node ${scriptPath}`, { stdio: 'inherit' })
    } catch {}
    break
  }
  case 'help':
  case '--help':
  case '-h':
    console.log(`
${c.bold('CLOVE')} — AI Agent Fleet OS

${c.bold('Usage:')} clove <command> [options]

${c.bold('Quick Start:')}
  ${c.cyan('clove start')}                          Start kernel
  ${c.cyan('clove connect github')}                  Connect GitHub
  ${c.cyan('clove agent create pr-reviewer')}        Create an agent
  ${c.cyan('clove agent enable pr-reviewer')}        Enable it

${c.bold('Commands:')}
  ${c.cyan('start / stop / status')}                 Kernel lifecycle
  ${c.cyan('run')} "goal" [--budget N]               Single agent run
  ${c.cyan('fleet')} "goal" [-n N]                   Parallel fleet run
  ${c.cyan('connect')} <service>                     Connect external service
  ${c.cyan('connect list')}                          Show connections
  ${c.cyan('agent create')} <name>                   Create persistent agent
  ${c.cyan('agent list')}                            List all agents
  ${c.cyan('agent enable/disable')} <name>           Toggle agent
  ${c.cyan('agent run')} <name>                      Manually trigger agent
  ${c.cyan('agent show')} <name>                     Show agent details
  ${c.cyan('agent delete')} <name>                   Remove agent
  ${c.cyan('templates / deploy')}                    Browse & deploy templates
  ${c.cyan('world list/create/launch')}              Manage worlds
  ${c.cyan('mcp list/add')}                          MCP server management
  ${c.cyan('recall / logs / dashboard / config')}    Utilities

${c.bold('Services:')} github, slack, filesystem, postgres, notion, google_drive

${c.bold('Examples:')}
  clove connect github ghp_abc123...
  clove agent create monitoring-bot
  clove agent run monitoring-bot
  clove world launch code-health --param project_path=./my-app
  clove fleet "Compare React vs Vue vs Svelte" -n 3
`)
    break
  default:
    console.log(`Unknown command: ${cmd}. Run ${c.cyan('clove help')} for usage.`)
}
