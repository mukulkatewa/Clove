/**
 * openclaw clove fleet — Launch multiple OpenClaw agents with per-agent config.
 *
 * Uses the kernel's POST /api/fleet endpoint for parallel execution,
 * or POST /api/openclaw/fleet for full OpenClaw-specific fleet management.
 */

import { readFileSync, existsSync } from 'node:fs'
import type { PluginLogger, ClovePluginConfig } from '../index.js'
import { isKernelRunning, startKernel } from '../kernel-manager.js'

export interface FleetOptions {
  configPath: string
  goal?: string
  agentCount: number
  totalBudget: number
  logger: PluginLogger
  pluginConfig: ClovePluginConfig
}

interface FleetAgent {
  name: string
  soul?: string
  channels?: string[]
  skills?: string[]
  budget_usd?: number
  can?: string[]
}

interface FleetConfig {
  fleet?: { name?: string; budget_usd?: number }
  agents?: Record<string, FleetAgent>
  goal?: string
}

export async function cliFleet(opts: FleetOptions): Promise<void> {
  const { configPath, goal, agentCount, totalBudget, logger, pluginConfig: cfg } = opts

  // Ensure kernel is running
  if (!await isKernelRunning(cfg)) {
    logger.info('CLOVE kernel not running. Starting...')
    const ok = await startKernel(cfg, logger)
    if (!ok) {
      logger.error('Failed to start kernel.')
      return
    }
  }

  // Load fleet config if exists
  let fleetConfig: FleetConfig = {}
  if (existsSync(configPath)) {
    try {
      // Simple YAML-like parsing (for proper YAML, users should use js-yaml)
      const raw = readFileSync(configPath, 'utf-8')
      fleetConfig = JSON.parse(raw) // Expect JSON for now; YAML support via optional dep
    } catch {
      logger.warn(`Could not parse ${configPath} as JSON. Using CLI args.`)
    }
  }

  // Build fleet request
  const fleetGoal = goal || fleetConfig.goal || 'Complete the assigned tasks'
  const budget = fleetConfig.fleet?.budget_usd || totalBudget
  const agents = fleetConfig.agents
    ? Object.entries(fleetConfig.agents).map(([name, a]) => ({
        name,
        soul: a.soul || '',
        channels: a.channels || [],
        skills: a.skills || [],
        budget_usd: a.budget_usd || budget / Object.keys(fleetConfig.agents!).length,
        can: a.can || [],
      }))
    : null

  logger.info('')
  logger.info('  CLOVE Fleet')
  logger.info('  ────────────────────────────────────')

  if (agents) {
    logger.info(`  Agents: ${agents.length}`)
    for (const a of agents) {
      logger.info(`    ${a.name.padEnd(20)} $${a.budget_usd.toFixed(2)}  [${a.can.join(', ')}]`)
    }
    logger.info(`  Total budget: $${budget.toFixed(2)}`)
  } else {
    logger.info(`  Agents: ${agentCount} (generic)`)
    logger.info(`  Budget: $${budget.toFixed(2)} ($${(budget / agentCount).toFixed(2)} each)`)
  }

  logger.info(`  Goal: ${fleetGoal.substring(0, 80)}`)
  logger.info('  ────────────────────────────────────')
  logger.info('')

  // Call kernel fleet API
  try {
    const body = agents
      ? { goal: fleetGoal, agents: agents.length, budget, tools: [] as string[] }
      : { goal: fleetGoal, agents: agentCount, budget }

    const resp = await fetch(`http://localhost:${cfg.apiPort}/api/fleet`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })

    if (!resp.ok) {
      const err = await resp.json() as { error?: string }
      logger.error(`Fleet failed: ${err.error || resp.statusText}`)
      return
    }

    // Stream SSE events
    const reader = resp.body?.getReader()
    if (!reader) {
      logger.error('No response stream')
      return
    }

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
          const event = JSON.parse(line.slice(6))
          printEvent(event, logger)
        } catch {
          // Skip malformed events
        }
      }
    }

  } catch (e) {
    logger.error(`Fleet error: ${e}`)
  }
}

function printEvent(event: Record<string, unknown>, logger: PluginLogger): void {
  const type = event['type'] as string
  const data = event['data'] as Record<string, unknown> || {}

  switch (type) {
    case 'fleet_start':
      logger.info(`  Fleet started: ${data['agent_count']} agents, $${(data['total_budget_usd'] as number)?.toFixed(2)} budget`)
      break

    case 'agent_event': {
      const agent = data['agent'] as string
      const evType = data['event_type'] as string
      if (evType === 'tool_call') {
        logger.info(`  [${agent}] → ${data['tool']}(${JSON.stringify(data['args']).substring(0, 60)})`)
      } else if (evType === 'done') {
        const cost = (data['cost_usd'] as number)?.toFixed(4)
        logger.info(`  [${agent}] ✓ done ($${cost})`)
      }
      break
    }

    case 'agent_done': {
      const agent = data['agent'] as string
      const completed = data['completed'] as number
      const total = data['total'] as number
      const cost = (data['cost_usd'] as number)?.toFixed(4)
      logger.info(`  Agent ${agent} complete (${completed}/${total}) — $${cost}`)
      break
    }

    case 'synthesizing':
      logger.info('  Synthesizing outputs from all agents...')
      break

    case 'fleet_done': {
      const totalCost = (data['total_cost_usd'] as number)?.toFixed(4)
      const totalTokens = data['total_tokens'] as number
      const agentCount = data['agent_count'] as number
      logger.info('')
      logger.info('  ────────────────────────────────────')
      logger.info(`  Fleet complete`)
      logger.info(`  Agents: ${agentCount}`)
      logger.info(`  Cost: $${totalCost}`)
      logger.info(`  Tokens: ${totalTokens?.toLocaleString()}`)
      logger.info('  ────────────────────────────────────')
      break
    }
  }
}
