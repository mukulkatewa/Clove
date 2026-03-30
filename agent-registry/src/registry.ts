/**
 * Agent Registry — manages persistent agent definitions.
 *
 * Stores agents as YAML files in ~/.clove/agents/<name>/agent.yaml.
 * Loads on startup, watches for changes, provides CRUD API.
 */

import { readFileSync, writeFileSync, existsSync, mkdirSync, readdirSync, unlinkSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import { homedir } from 'node:os'
import type { AgentDefinition, Connection } from './types.js'

const AGENTS_DIR = join(homedir(), '.clove', 'agents')
const CONNECTIONS_FILE = join(homedir(), '.clove', 'connections.json')

export class Registry {
  private agents: Map<string, AgentDefinition> = new Map()
  private connections: Map<string, Connection> = new Map()

  constructor() {
    mkdirSync(AGENTS_DIR, { recursive: true })
    this.loadAll()
  }

  // ── Agents ──────────────────────────────────────────────────────

  loadAll(): void {
    this.agents.clear()
    if (!existsSync(AGENTS_DIR)) return

    for (const dir of readdirSync(AGENTS_DIR)) {
      const configPath = join(AGENTS_DIR, dir, 'agent.json')
      if (existsSync(configPath)) {
        try {
          const raw = JSON.parse(readFileSync(configPath, 'utf-8'))
          this.agents.set(raw.name || dir, raw as AgentDefinition)
        } catch {}
      }
    }

    // Load connections
    if (existsSync(CONNECTIONS_FILE)) {
      try {
        const raw = JSON.parse(readFileSync(CONNECTIONS_FILE, 'utf-8'))
        for (const conn of raw.connections || []) {
          this.connections.set(conn.name, conn as Connection)
        }
      } catch {}
    }
  }

  list(): AgentDefinition[] {
    return [...this.agents.values()]
  }

  get(name: string): AgentDefinition | undefined {
    return this.agents.get(name)
  }

  create(agent: AgentDefinition): void {
    const dir = join(AGENTS_DIR, agent.name)
    mkdirSync(dir, { recursive: true })
    agent.created_at = agent.created_at || new Date().toISOString()
    agent.updated_at = new Date().toISOString()
    writeFileSync(join(dir, 'agent.json'), JSON.stringify(agent, null, 2))
    this.agents.set(agent.name, agent)
  }

  update(name: string, updates: Partial<AgentDefinition>): AgentDefinition | null {
    const agent = this.agents.get(name)
    if (!agent) return null
    const updated = { ...agent, ...updates, updated_at: new Date().toISOString() }
    const dir = join(AGENTS_DIR, name)
    writeFileSync(join(dir, 'agent.json'), JSON.stringify(updated, null, 2))
    this.agents.set(name, updated)
    return updated
  }

  delete(name: string): boolean {
    const dir = join(AGENTS_DIR, name)
    if (existsSync(dir)) {
      rmSync(dir, { recursive: true })
      this.agents.delete(name)
      return true
    }
    return false
  }

  enable(name: string): boolean {
    return !!this.update(name, { enabled: true })
  }

  disable(name: string): boolean {
    return !!this.update(name, { enabled: false })
  }

  // ── Connections ─────────────────────────────────────────────────

  listConnections(): Connection[] {
    return [...this.connections.values()]
  }

  getConnection(name: string): Connection | undefined {
    return this.connections.get(name)
  }

  setConnection(conn: Connection): void {
    this.connections.set(conn.name, conn)
    this.saveConnections()
  }

  removeConnection(name: string): boolean {
    const deleted = this.connections.delete(name)
    if (deleted) this.saveConnections()
    return deleted
  }

  private saveConnections(): void {
    const data = { connections: [...this.connections.values()] }
    writeFileSync(CONNECTIONS_FILE, JSON.stringify(data, null, 2))
  }

  // ── Query ───────────────────────────────────────────────────────

  /** Find agents that match a trigger type */
  findByTrigger(type: string, source?: string): AgentDefinition[] {
    return this.list().filter(a => {
      if (!a.enabled) return false
      return a.triggers.some(t => {
        if (t.type !== type) return false
        if (source && t.source && t.source !== source) return false
        return true
      })
    })
  }

  /** Find agents with cron triggers */
  getCronAgents(): Array<{ agent: AgentDefinition; trigger: AgentDefinition['triggers'][0] }> {
    const results: Array<{ agent: AgentDefinition; trigger: AgentDefinition['triggers'][0] }> = []
    for (const agent of this.list()) {
      if (!agent.enabled) continue
      for (const trigger of agent.triggers) {
        if (trigger.type === 'cron' && trigger.schedule) {
          results.push({ agent, trigger })
        }
      }
    }
    return results
  }

  /** Check if agent is within daily budget */
  checkBudget(name: string): boolean {
    const agent = this.agents.get(name)
    if (!agent) return false
    const today = new Date().toISOString().slice(0, 10)
    if (agent.budget.last_reset !== today) {
      agent.budget.daily_spent = 0
      agent.budget.last_reset = today
      this.update(name, { budget: agent.budget })
    }
    return agent.budget.daily_spent < agent.budget.daily_max
  }

  /** Record cost for an agent run */
  recordCost(name: string, cost: number): void {
    const agent = this.agents.get(name)
    if (!agent) return
    agent.budget.daily_spent += cost
    this.update(name, { budget: agent.budget })
  }
}
