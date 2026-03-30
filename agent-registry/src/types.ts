/**
 * Agent definition — a persistent, named entity with identity,
 * connections, triggers, permissions, budget, and memory.
 */

export interface AgentDefinition {
  name: string
  description: string
  enabled: boolean
  created_at: string
  updated_at: string

  /** Connections this agent can use (references to connection configs) */
  connections: string[]

  /** What triggers this agent to run */
  triggers: AgentTrigger[]

  /** What the agent does when triggered */
  action: AgentAction

  /** Permission constraints */
  permissions: AgentPermissions

  /** Budget limits */
  budget: AgentBudget

  /** Named memory blocks this agent owns */
  memory: string[]
}

export interface AgentTrigger {
  type: 'cron' | 'webhook' | 'event' | 'manual'
  /** Cron expression (for type=cron) */
  schedule?: string
  /** Webhook source filter (for type=webhook) */
  source?: string
  /** Event type filter (for type=event) */
  event_type?: string
  /** Optional filter expression */
  filter?: string
}

export interface AgentAction {
  /** Goal template with {{event}} placeholder for trigger context */
  goal: string
  /** Allowed tools */
  tools: string[]
  /** Max steps per run */
  max_steps: number
  /** Model override */
  model?: string
  /** Optional world to run in */
  world?: string
}

export interface AgentPermissions {
  can_exec: boolean
  can_read: boolean
  can_write: boolean
  can_http: boolean
  allowed_domains: string[]
  allowed_paths: string[]
}

export interface AgentBudget {
  /** Max cost per trigger/run in USD */
  per_run: number
  /** Max daily spend in USD */
  daily_max: number
  /** Current daily spend (reset at midnight) */
  daily_spent: number
  /** Date of last reset */
  last_reset: string
}

export interface Connection {
  name: string
  type: 'mcp' | 'http' | 'webhook'
  /** MCP server command (for type=mcp) */
  command?: string
  args?: string[]
  /** Base URL (for type=http) */
  base_url?: string
  /** Credential keys to inject from vault */
  credentials: Record<string, string>
  /** Rate limit (requests per minute) */
  rate_limit?: number
  status: 'active' | 'inactive'
}

export interface InboundEvent {
  id: string
  source: string
  type: string
  payload: Record<string, unknown>
  received_at: string
}

export interface AgentRun {
  agent_name: string
  trigger: AgentTrigger
  event?: InboundEvent
  started_at: string
  completed_at?: string
  success?: boolean
  cost_usd: number
  result?: string
}
