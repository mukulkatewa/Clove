const API = process.env.NEXT_PUBLIC_API_URL || ''

async function fetchAPI<T>(path: string, options?: RequestInit): Promise<T> {
  const res = await fetch(`${API}${path}`, {
    ...options,
    headers: { 'Content-Type': 'application/json', ...options?.headers },
  })
  return res.json() as Promise<T>
}

// Health
export interface HealthResponse {
  status: string
  version: string
  syscall_count: number
  uptime_s: number
}
export const getHealth = () => fetchAPI<HealthResponse>('/api/health')

// Cost
export interface CostResponse {
  total_cost_usd: number
  max_cost_usd: number
  total_requests: number
  total_completed: number
}
export const getCost = () => fetchAPI<CostResponse>('/api/cost')

// Runs
export interface RunRequest {
  goal: string
  budget?: number
  model?: string
  tools?: string[]
  max_steps?: number
}
export interface RunResponse {
  success: boolean
  content: string
  total_cost_usd: number
  total_tokens: number
  steps: number
  chain_id: string
  step_log: Array<{ step: number; tool: string; args: string }>
}
export const submitRun = (req: RunRequest) =>
  fetchAPI<RunResponse>('/api/run', { method: 'POST', body: JSON.stringify(req) })

// Run history
export interface HistoryEntry {
  name: string
  chain_id: string
  description: string
  artifact_count: number
  created_at_ms: number
}
export const getHistory = () =>
  fetchAPI<{ runs: HistoryEntry[]; count: number }>('/api/history')

// Fleet
export interface FleetRequest {
  goal: string
  agents?: number
  budget?: number
  max_steps?: number
}

// Audit
export interface AuditEntry {
  id: number
  category: string
  event_type: string
  agent_id: number
  agent_name: string
  success: boolean
  timestamp: string
  details: Record<string, unknown>
}
export const getAudit = (limit = 50) =>
  fetchAPI<AuditEntry[]>(`/api/audit?limit=${limit}`)

// OpenClaw
export interface OpenClawInstance {
  id: string
  name: string
  state: string
  port: number
  pid: number
  budget_usd: number
  cost_usd: number
  channels: string[]
  started_at_ms: number
}
export interface SpawnRequest {
  name: string
  soul?: string
  budget_usd?: number
  channels?: string[]
  skills?: string[]
}
export const getOpenClawStatus = () =>
  fetchAPI<{ instances: OpenClawInstance[]; count: number }>('/api/openclaw/status')
export const spawnOpenClaw = (req: SpawnRequest) =>
  fetchAPI<{ id: string; name: string; state: string; port: number }>('/api/openclaw/spawn', {
    method: 'POST', body: JSON.stringify(req),
  })
export const stopOpenClaw = (id: string) =>
  fetchAPI<{ success: boolean }>(`/api/openclaw/${id}/stop`, { method: 'POST' })
export const stopAllOpenClaw = () =>
  fetchAPI<{ success: boolean }>('/api/openclaw/stop-all', { method: 'POST' })

// Schedules
export interface Schedule {
  name: string
  cron: string
  enabled: boolean
  run: { goal: string; budget: number; agents: number }
  created_at_ms: number
}
export const getSchedules = () =>
  fetchAPI<{ schedules: Schedule[]; count: number }>('/api/schedules')
export const createSchedule = (s: { name: string; cron: string; run: Record<string, unknown> }) =>
  fetchAPI<Schedule>('/api/schedules', { method: 'POST', body: JSON.stringify(s) })
export const deleteSchedule = (name: string) =>
  fetchAPI<{ success: boolean }>(`/api/schedules/${name}`, { method: 'DELETE' })

// Webhooks
export interface Webhook {
  id: string
  url: string
  events: string[]
  enabled: boolean
  created_at_ms: number
}
export const getWebhooks = () =>
  fetchAPI<{ webhooks: Webhook[]; count: number }>('/api/webhooks')
export const createWebhook = (w: { url: string; events: string[] }) =>
  fetchAPI<Webhook>('/api/webhooks', { method: 'POST', body: JSON.stringify(w) })
export const deleteWebhook = (id: string) =>
  fetchAPI<{ success: boolean }>(`/api/webhooks/${id}`, { method: 'DELETE' })

// Memory
export interface MemoryBlock {
  id: string
  name: string
  type: string
  content: string
  access: string
}
export const getMemoryBlocks = () =>
  fetchAPI<{ blocks: MemoryBlock[]; count: number }>('/api/memory')

// Agents
export const getAgents = () =>
  fetchAPI<Array<{ id: number; name: string; state: string }>>('/api/agents')

// IPC
export const sendMessage = (fromId: number, to: number | string, content: string) =>
  fetchAPI<{ success: boolean }>(`/api/agents/${fromId}/message`, {
    method: 'POST', body: JSON.stringify({ to, content }),
  })
export const getMessages = (agentId: number) =>
  fetchAPI<{ messages: Array<{ from: number; to: number; content: string; timestamp_ms: number }>; count: number }>(
    `/api/agents/${agentId}/messages`,
  )

// MCP
export interface McpServer { name: string; status: string; tools_count: number; command: string }
export interface McpTool { server_name: string; name: string; description: string; input_schema: Record<string, unknown> }
export const getMcpServers = () => fetchAPI<{ servers: McpServer[] }>('/api/mcp/servers')
export const getMcpTools = () => fetchAPI<{ tools: McpTool[] }>('/api/mcp/tools')
export const callMcpTool = (server: string, tool: string, args: Record<string, unknown>) =>
  fetchAPI<{ success: boolean; content: string; duration_ms: number }>('/api/mcp/call', { method: 'POST', body: JSON.stringify({ server, tool, arguments: args }) })

// World launch
export interface WorldLaunchRequest { template?: string; config?: Record<string, unknown>; params: Record<string, string> }
export const launchWorld = (req: WorldLaunchRequest) =>
  fetchAPI<{ success: boolean; report: string; cost: number; agents: number }>('/api/worlds/launch', { method: 'POST', body: JSON.stringify(req) })

// Worlds
export interface WorldSummary { id: number; name: string; member_count: number; metadata: Record<string, unknown> }
export const getWorlds = () => fetchAPI<{ worlds: WorldSummary[] }>('/api/worlds')
export const createWorld = (name: string) => fetchAPI<WorldSummary>('/api/worlds', { method: 'POST', body: JSON.stringify({ name }) })
export const deleteWorld = (id: number) => fetchAPI<{ success: boolean }>(`/api/worlds/${id}`, { method: 'DELETE' })

// Memory search
export const searchMemory = (query: string) => fetchAPI<{ blocks: MemoryBlock[] }>(`/api/memory/search?q=${encodeURIComponent(query)}`)

// SSE helpers
export function streamFleet(req: FleetRequest, onEvent: (event: Record<string, unknown>) => void) {
  fetch(`${API}/api/fleet`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(req),
  }).then(async (res) => {
    const reader = res.body?.getReader()
    if (!reader) return
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
        try { onEvent(JSON.parse(line.slice(6))) } catch {}
      }
    }
  })
}
