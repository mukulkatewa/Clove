/**
 * Kernel Manager — start, stop, and health-check the CLOVE kernel binary.
 */

import { spawn, execSync } from 'node:child_process'
import { existsSync, readFileSync, writeFileSync, unlinkSync } from 'node:fs'
import { homedir } from 'node:os'
import { join } from 'node:path'
import type { ClovePluginConfig, PluginLogger } from './index.js'

const PID_FILE = '/tmp/clove-kernel.pid'

const KERNEL_SEARCH_PATHS = [
  join(homedir(), '.clove', 'bin', 'clove_kernel'),
  '/usr/local/bin/clove_kernel',
  '/usr/local/bin/clove-kernel',
  // Common build locations
  join(homedir(), 'clove-v2', 'build', 'kernel', 'clove_kernel'),
]

export function findKernelBinary(configPath: string): string | null {
  if (configPath && existsSync(configPath)) return configPath

  const envPath = process.env['CLOVE_KERNEL_PATH']
  if (envPath && existsSync(envPath)) return envPath

  for (const p of KERNEL_SEARCH_PATHS) {
    if (existsSync(p)) return p
  }

  // Try which
  try {
    const found = execSync('which clove_kernel', { encoding: 'utf-8' }).trim()
    if (found) return found
  } catch {
    // not on PATH
  }

  return null
}

export async function isKernelRunning(cfg: ClovePluginConfig): Promise<boolean> {
  try {
    const resp = await fetch(`http://localhost:${cfg.apiPort}/api/health`)
    return resp.ok
  } catch {
    return false
  }
}

export async function getKernelStatus(cfg: ClovePluginConfig): Promise<Record<string, unknown> | null> {
  try {
    const resp = await fetch(`http://localhost:${cfg.apiPort}/api/health`)
    if (resp.ok) return await resp.json() as Record<string, unknown>
  } catch {
    // not running
  }
  return null
}

export async function getKernelCost(cfg: ClovePluginConfig): Promise<Record<string, unknown> | null> {
  try {
    const resp = await fetch(`http://localhost:${cfg.apiPort}/api/cost`)
    if (resp.ok) return await resp.json() as Record<string, unknown>
  } catch {
    // not running
  }
  return null
}

export async function getKernelMetrics(cfg: ClovePluginConfig): Promise<Record<string, unknown> | null> {
  try {
    const resp = await fetch(`http://localhost:${cfg.apiPort}/api/metrics`)
    if (resp.ok) return await resp.json() as Record<string, unknown>
  } catch {
    // not running
  }
  return null
}

export async function startKernel(cfg: ClovePluginConfig, logger: PluginLogger): Promise<boolean> {
  const binary = findKernelBinary(cfg.kernelPath)
  if (!binary) {
    logger.error('CLOVE kernel binary not found.')
    logger.error('Install from https://cloveos.com or set kernelPath in plugin config.')
    return false
  }

  const args: string[] = [
    '--socket', cfg.socketPath,
    '--api', '--api-port', String(cfg.apiPort),
  ]

  // Privacy
  if (cfg.privacyEnabled) {
    args.push('--privacy', '--privacy-mode', cfg.privacyMode)
  }

  // LLM cost limit
  if (cfg.budgetMaxCostUsd > 0) {
    args.push('--llm-max-cost', String(cfg.budgetMaxCostUsd))
  }

  // Auto-detect LLM provider from env
  const providerKeys = ['OPENROUTER_API_KEY', 'OPENAI_API_KEY', 'ANTHROPIC_API_KEY', 'GROQ_API_KEY']
  for (const key of providerKeys) {
    const val = process.env[key]
    if (val) {
      args.push('--openrouter', '--openrouter-key', val)
      break
    }
  }

  logger.info(`Starting CLOVE kernel: ${binary}`)

  const child = spawn(binary, args, {
    detached: true,
    stdio: ['ignore', 'pipe', 'pipe'],
  })

  child.unref()

  // Save PID
  if (child.pid) {
    writeFileSync(PID_FILE, String(child.pid))
  }

  // Wait for socket/API
  const deadline = Date.now() + 10000 // 10s timeout
  while (Date.now() < deadline) {
    if (await isKernelRunning(cfg)) {
      logger.info(`CLOVE kernel started (PID ${child.pid})`)
      logger.info(`  Socket:    ${cfg.socketPath}`)
      logger.info(`  API:       http://localhost:${cfg.apiPort}`)
      logger.info(`  Dashboard: http://localhost:${cfg.apiPort}/dashboard`)
      return true
    }
    await new Promise(r => setTimeout(r, 200))
  }

  logger.error('CLOVE kernel failed to start within 10 seconds')
  return false
}

export async function stopKernel(cfg: ClovePluginConfig, logger: PluginLogger): Promise<boolean> {
  // Try PID file
  try {
    const pid = parseInt(readFileSync(PID_FILE, 'utf-8').trim(), 10)
    process.kill(pid, 'SIGTERM')
    logger.info(`CLOVE kernel stopped (PID ${pid})`)
    try { unlinkSync(PID_FILE) } catch { /* ignore */ }
    try { unlinkSync(cfg.socketPath) } catch { /* ignore */ }
    return true
  } catch {
    // No PID file or process already dead
  }

  // Try pkill as fallback
  try {
    execSync('pkill -f clove_kernel', { encoding: 'utf-8' })
    logger.info('CLOVE kernel stopped')
    return true
  } catch {
    logger.info('CLOVE kernel not running')
    return false
  }
}
