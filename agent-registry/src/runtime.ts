/**
 * Agent Runtime — evaluates triggers, dispatches runs, manages lifecycle.
 *
 * Runs as a sidecar process alongside the kernel.
 * - Evaluates cron triggers every 60s
 * - Listens for inbound webhook events
 * - Dispatches runs to the kernel via HTTP
 * - Tracks cost and enforces daily budgets
 */

import { Registry } from './registry.js'
import type { AgentDefinition, InboundEvent, AgentRun } from './types.js'

const KERNEL_URL = process.env.CLOVE_API_URL || 'http://localhost:8080'

export class Runtime {
  private registry: Registry
  private runHistory: AgentRun[] = []
  private cronState: Map<string, string> = new Map() // name:minute → last fired

  constructor(registry: Registry) {
    this.registry = registry
  }

  // ── Kernel API ──────────────────────────────────────────────────

  private async kernel<T>(path: string, method = 'GET', body?: unknown): Promise<T | null> {
    try {
      const opts: RequestInit = { method, headers: { 'Content-Type': 'application/json' } }
      if (body) opts.body = JSON.stringify(body)
      const res = await fetch(`${KERNEL_URL}${path}`, opts)
      return await res.json() as T
    } catch {
      return null
    }
  }

  // ── Run an agent ────────────────────────────────────────────────

  async runAgent(agent: AgentDefinition, trigger: AgentDefinition['triggers'][0], event?: InboundEvent): Promise<AgentRun> {
    // Budget check
    if (!this.registry.checkBudget(agent.name)) {
      console.log(`  [${agent.name}] SKIPPED — daily budget exceeded`)
      return {
        agent_name: agent.name,
        trigger,
        event,
        started_at: new Date().toISOString(),
        completed_at: new Date().toISOString(),
        success: false,
        cost_usd: 0,
        result: 'Daily budget exceeded',
      }
    }

    // Build goal with event context
    let goal = agent.action.goal
    if (event) {
      goal = goal.replace('{{event}}', JSON.stringify(event.payload))
      goal = goal.replace('{{event.source}}', event.source)
      goal = goal.replace('{{event.type}}', event.type)
    }

    console.log(`  [${agent.name}] RUNNING — ${goal.slice(0, 60)}...`)

    const started_at = new Date().toISOString()

    const result = await this.kernel<{
      success: boolean
      content: string
      total_cost_usd: number
      steps: number
    }>('/api/run', 'POST', {
      goal,
      budget: agent.budget.per_run,
      max_steps: agent.action.max_steps,
      tools: agent.action.tools,
      model: agent.action.model,
      agent_name: agent.name,
    })

    const run: AgentRun = {
      agent_name: agent.name,
      trigger,
      event,
      started_at,
      completed_at: new Date().toISOString(),
      success: result?.success ?? false,
      cost_usd: result?.total_cost_usd ?? 0,
      result: result?.content,
    }

    // Track cost
    this.registry.recordCost(agent.name, run.cost_usd)
    this.runHistory.push(run)
    if (this.runHistory.length > 500) this.runHistory = this.runHistory.slice(-250)

    const status = run.success ? 'OK' : 'FAIL'
    console.log(`  [${agent.name}] ${status} — $${run.cost_usd.toFixed(4)}`)

    return run
  }

  // ── Cron evaluation ─────────────────────────────────────────────

  async evaluateCrons(): Promise<void> {
    const now = new Date()
    const minuteKey = `${now.getFullYear()}-${now.getMonth()}-${now.getDate()}-${now.getHours()}-${now.getMinutes()}`

    for (const { agent, trigger } of this.registry.getCronAgents()) {
      const firedKey = `${agent.name}:${minuteKey}`
      if (this.cronState.get(firedKey)) continue

      if (this.cronMatches(trigger.schedule!, now)) {
        this.cronState.set(firedKey, new Date().toISOString())
        await this.runAgent(agent, trigger)
      }
    }

    // Prune old cron state
    if (this.cronState.size > 200) {
      const entries = [...this.cronState.entries()]
      this.cronState = new Map(entries.slice(-100))
    }
  }

  private cronMatches(cron: string, now: Date): boolean {
    const parts = cron.trim().split(/\s+/)
    if (parts.length !== 5) return false
    const fields = [now.getMinutes(), now.getHours(), now.getDate(), now.getMonth() + 1, now.getDay()]
    return parts.every((pat, i) => {
      if (pat === '*') return true
      if (pat.startsWith('*/')) { const s = parseInt(pat.slice(2)); return s > 0 && fields[i] % s === 0 }
      return pat.split(',').some(v => {
        if (v.includes('-')) { const [a, b] = v.split('-').map(Number); return fields[i] >= a && fields[i] <= b }
        return parseInt(v) === fields[i]
      })
    })
  }

  // ── Webhook dispatch ────────────────────────────────────────────

  async handleWebhook(event: InboundEvent): Promise<AgentRun[]> {
    const agents = this.registry.findByTrigger('webhook', event.source)
    const runs: AgentRun[] = []

    for (const agent of agents) {
      const trigger = agent.triggers.find(t => t.type === 'webhook' && (!t.source || t.source === event.source))
      if (!trigger) continue

      // Apply filter if present
      if (trigger.filter) {
        const filterKey = trigger.filter.split('=')[0]
        const filterValue = trigger.filter.split('=')[1]
        const eventValue = String((event.payload as Record<string, unknown>)[filterKey] ?? '')
        if (eventValue !== filterValue) continue
      }

      runs.push(await this.runAgent(agent, trigger, event))
    }

    return runs
  }

  // ── History ─────────────────────────────────────────────────────

  getHistory(limit = 50): AgentRun[] {
    return this.runHistory.slice(-limit).reverse()
  }

  // ── Main loop ───────────────────────────────────────────────────

  async start(): Promise<void> {
    console.log('Agent Runtime started')
    console.log(`  Kernel: ${KERNEL_URL}`)
    console.log(`  Agents: ${this.registry.list().length} registered, ${this.registry.list().filter(a => a.enabled).length} enabled`)
    console.log()

    while (true) {
      try {
        await this.evaluateCrons()
      } catch (e) {
        console.error('Cron error:', (e as Error).message)
      }
      await new Promise(r => setTimeout(r, 60_000))
    }
  }
}
