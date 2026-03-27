'use client'
import { useEffect, useState } from 'react'

interface AgentOverview {
  id: string
  name: string
  type: string
  state: string
  pid: number
  budget_usd: number
  cost_usd: number
  channels: string[]
  started_at_ms: number
  permissions: {
    can_exec: boolean
    can_read: boolean
    can_write: boolean
    can_think: boolean
    can_spawn: boolean
    can_http: boolean
    allowed_read_paths: string[]
    allowed_write_paths: string[]
    allowed_domains: string[]
    blocked_commands: string[]
  }
  budget_tracking: {
    max_cost_usd: number
    cost_usd: number
    max_tokens: number
    tokens_used: number
    max_steps: number
    steps_taken: number
  }
}

interface ActivityEntry {
  event_type: string
  category: string
  agent_name: string
  timestamp: string
  success: boolean
  details: Record<string, unknown>
}

interface SandboxOverview {
  agents: AgentOverview[]
  activity: ActivityEntry[]
  cost: { total_usd: number }
  memory_blocks: number
}

const API = ''

export default function AgentsPage() {
  const [data, setData] = useState<SandboxOverview | null>(null)

  useEffect(() => {
    const load = () => {
      fetch(`${API}/api/sandbox/overview`)
        .then((r) => r.json())
        .then(setData)
        .catch(() => {})
    }
    load()
    const interval = setInterval(load, 2000)
    return () => clearInterval(interval)
  }, [])

  if (!data) {
    return <div className="text-center py-20" style={{ color: 'var(--text-dim)' }}>Loading...</div>
  }

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-2xl font-semibold">Sandbox Visualizer</h2>
          <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>
            Every permission explicit. Every action visible. Every agent governed.
          </p>
        </div>
        <div className="flex gap-4 text-sm">
          <Stat label="Cost" value={`$${data.cost.total_usd.toFixed(4)}`} color="var(--accent)" />
          <Stat label="Memory" value={String(data.memory_blocks)} color="var(--blue)" />
          <Stat label="Events" value={String(data.activity.length)} color="var(--yellow)" />
        </div>
      </div>

      {data.agents.length === 0 ? (
        <div className="border rounded-lg p-12 text-center" style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}>
          <p style={{ color: 'var(--text-dim)' }}>No agents running. Spawn one from the OpenClaw page or via API.</p>
        </div>
      ) : (
        <div className="space-y-4">
          {data.agents.map((agent) => (
            <AgentCard key={agent.id} agent={agent} />
          ))}
        </div>
      )}

      {/* Activity feed */}
      <div className="mt-8">
        <h3 className="text-sm font-semibold mb-3" style={{ color: 'var(--text-dim)' }}>LIVE ACTIVITY</h3>
        <div className="border rounded-lg overflow-hidden" style={{ borderColor: 'var(--border)' }}>
          {data.activity.length === 0 ? (
            <div className="p-6 text-center text-sm" style={{ color: 'var(--text-dim)' }}>No activity yet.</div>
          ) : (
            <div className="max-h-64 overflow-y-auto">
              {data.activity.map((e, i) => (
                <div
                  key={i}
                  className="flex items-center gap-3 px-4 py-2 text-xs border-b"
                  style={{ borderColor: 'var(--border)', background: i % 2 === 0 ? 'var(--bg-card)' : 'var(--bg)' }}
                >
                  <span
                    className="w-1.5 h-1.5 rounded-full flex-shrink-0"
                    style={{ background: e.success ? 'var(--green)' : 'var(--red)' }}
                  />
                  <span className="mono" style={{ color: 'var(--accent)', minWidth: 160 }}>{e.event_type}</span>
                  <span style={{ color: 'var(--text-dim)', minWidth: 80 }}>{e.category}</span>
                  <span style={{ color: 'var(--text-dim)' }}>{e.agent_name}</span>
                  <span className="ml-auto" style={{ color: 'var(--text-dim)' }}>
                    {e.timestamp ? new Date(e.timestamp).toLocaleTimeString() : ''}
                  </span>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}

function AgentCard({ agent }: { agent: AgentOverview }) {
  const p = agent.permissions
  const b = agent.budget_tracking
  const budgetPct = b.max_cost_usd > 0 ? Math.min((b.cost_usd / b.max_cost_usd) * 100, 100) : 0

  const handleKill = async () => {
    await fetch(`${API}/api/openclaw/${agent.id}/stop`, { method: 'POST' })
  }

  return (
    <div
      className="border rounded-lg p-5"
      style={{
        borderColor: agent.state === 'running' ? 'var(--green)' : 'var(--border)',
        borderLeftWidth: 3,
        background: 'var(--bg-card)',
      }}
    >
      {/* Header */}
      <div className="flex items-center justify-between mb-4">
        <div className="flex items-center gap-3">
          <span className="text-lg font-semibold">{agent.name}</span>
          <span
            className="text-xs px-2 py-0.5 rounded"
            style={{ background: agent.state === 'running' ? 'var(--green)' : 'var(--red)', color: '#000' }}
          >
            {agent.state}
          </span>
          <span className="text-xs" style={{ color: 'var(--text-dim)' }}>PID {agent.pid}</span>
          {agent.channels.length > 0 && (
            <span className="text-xs" style={{ color: 'var(--text-dim)' }}>{agent.channels.join(', ')}</span>
          )}
        </div>
        <div className="flex gap-2">
          <button
            onClick={handleKill}
            className="px-3 py-1 rounded text-xs font-semibold"
            style={{ border: '1px solid var(--red)', color: 'var(--red)' }}
          >
            Kill
          </button>
        </div>
      </div>

      <div className="grid grid-cols-3 gap-6">
        {/* Permissions */}
        <div>
          <div className="text-xs font-semibold mb-2" style={{ color: 'var(--text-dim)' }}>TOOLS</div>
          <div className="space-y-1">
            <PermRow label="read_file" allowed={p.can_read} />
            <PermRow label="write_file" allowed={p.can_write} />
            <PermRow label="exec" allowed={p.can_exec} />
            <PermRow label="http" allowed={p.can_http} />
            <PermRow label="think (LLM)" allowed={p.can_think} />
            <PermRow label="spawn" allowed={p.can_spawn} />
          </div>
        </div>

        {/* Filesystem + Network */}
        <div>
          <div className="text-xs font-semibold mb-2" style={{ color: 'var(--text-dim)' }}>FILESYSTEM</div>
          <div className="space-y-1 mb-3">
            {p.allowed_read_paths.length > 0 ? (
              p.allowed_read_paths.map((path, i) => (
                <div key={i} className="text-xs mono" style={{ color: 'var(--green)' }}>
                  {path} <span style={{ color: 'var(--text-dim)' }}>read</span>
                </div>
              ))
            ) : (
              <div className="text-xs" style={{ color: 'var(--text-dim)' }}>No path restrictions</div>
            )}
            {p.allowed_write_paths.map((path, i) => (
              <div key={i} className="text-xs mono" style={{ color: 'var(--yellow)' }}>
                {path} <span style={{ color: 'var(--text-dim)' }}>write</span>
              </div>
            ))}
          </div>

          <div className="text-xs font-semibold mb-2" style={{ color: 'var(--text-dim)' }}>NETWORK</div>
          <div className="space-y-1">
            {p.allowed_domains.length > 0 ? (
              p.allowed_domains.map((d, i) => (
                <div key={i} className="text-xs mono" style={{ color: 'var(--green)' }}>{d}</div>
              ))
            ) : (
              <div className="text-xs" style={{ color: 'var(--text-dim)' }}>No domain restrictions</div>
            )}
          </div>
        </div>

        {/* Budget */}
        <div>
          <div className="text-xs font-semibold mb-2" style={{ color: 'var(--text-dim)' }}>BUDGET</div>
          <div className="text-2xl font-semibold mono" style={{ color: 'var(--accent)' }}>
            ${b.cost_usd.toFixed(4)}
          </div>
          {b.max_cost_usd > 0 && (
            <>
              <div className="text-xs mt-1" style={{ color: 'var(--text-dim)' }}>
                of ${b.max_cost_usd.toFixed(2)} ({budgetPct.toFixed(1)}%)
              </div>
              <div className="h-2 rounded-full mt-2 overflow-hidden" style={{ background: 'var(--border)' }}>
                <div
                  className="h-full rounded-full transition-all"
                  style={{
                    width: `${budgetPct}%`,
                    background: budgetPct > 80 ? 'var(--red)' : budgetPct > 50 ? 'var(--yellow)' : 'var(--green)',
                  }}
                />
              </div>
            </>
          )}
          <div className="mt-3 space-y-1 text-xs" style={{ color: 'var(--text-dim)' }}>
            <div>Tokens: {b.tokens_used.toLocaleString()}{b.max_tokens > 0 ? ` / ${b.max_tokens.toLocaleString()}` : ''}</div>
            <div>Steps: {b.steps_taken}{b.max_steps > 0 ? ` / ${b.max_steps}` : ''}</div>
          </div>
        </div>
      </div>
    </div>
  )
}

function PermRow({ label, allowed }: { label: string; allowed: boolean }) {
  return (
    <div className="flex items-center gap-2 text-xs">
      <span
        className="w-4 text-center"
        style={{ color: allowed ? 'var(--green)' : 'var(--red)' }}
      >
        {allowed ? '\u2713' : '\u2717'}
      </span>
      <span className="mono" style={{ color: allowed ? 'var(--text)' : 'var(--text-dim)', opacity: allowed ? 1 : 0.5 }}>
        {label}
      </span>
    </div>
  )
}

function Stat({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div className="text-right">
      <div className="text-xs" style={{ color: 'var(--text-dim)' }}>{label}</div>
      <div className="font-semibold mono" style={{ color }}>{value}</div>
    </div>
  )
}
