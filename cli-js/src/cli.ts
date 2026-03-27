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
import { existsSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { homedir } from 'node:os'
import { join } from 'node:path'

// ── Config ──────────────────────────────────────────────────────────────

const CLOVE_DIR = join(homedir(), '.clove')
const PID_FILE = join(CLOVE_DIR, 'kernel.pid')
const CONFIG_FILE = join(CLOVE_DIR, 'config.json')
const DEFAULT_PORT = 8080

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

${c.bold('Commands:')}
  ${c.cyan('start')}                    Start kernel + open dashboard
  ${c.cyan('stop')}                     Stop kernel
  ${c.cyan('status')}                   Kernel health, agents, cost
  ${c.cyan('run')} "goal" [--budget N]  Run an agent
  ${c.cyan('fleet')} "goal" [-n N]      Run N agents in parallel
  ${c.cyan('recall')}                   Show shared memory
  ${c.cyan('logs')} [N]                 Recent audit entries
  ${c.cyan('dashboard')}                Open dashboard in browser
  ${c.cyan('config')} [set k v | get k] View/edit configuration

${c.bold('Examples:')}
  clove start
  clove run "Research the top 5 AI companies in 2026"
  clove fleet "Compare Rust, Go, and Python" -n 3
  clove recall
  clove logs 50

${c.bold('Shortcuts:')} s=status, r=run, f=fleet, d=dashboard, c=config
`)
    break
  default:
    console.log(`Unknown command: ${cmd}. Run ${c.cyan('clove help')} for usage.`)
}
