'use client'
import { useEffect, useState } from 'react'
import { useDemo } from '@/lib/demo-context'
import { demoSandboxOverview } from '@/lib/demo-data'

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
  const { isDemo } = useDemo()
  const [data, setData] = useState<SandboxOverview | null>(null)

  useEffect(() => {
    if (isDemo) { setData(demoSandboxOverview() as any); return }
    const load = () => {
      fetch(`${API}/api/sandbox/overview`)
        .then((r) => r.json())
        .then(setData)
        .catch(() => {})
    }
    load()
    const interval = setInterval(load, 2000)
    return () => clearInterval(interval)
  }, [isDemo])

  if (!data) {
    return <div className="text-center py-20" style={{ color: 'var(--text-dim)' }}>Loading...</div>
  }

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 style={{ fontSize: 24, fontWeight: 600, color: 'var(--text)' }}>Sandbox Visualizer</h2>
          <p style={{ fontSize: 14, marginTop: 4, color: 'var(--text-secondary)' }}>
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
        <div className="card" style={{ padding: 48, textAlign: 'center' }}>
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
        <h3 style={{ fontSize: 16, fontWeight: 600, marginBottom: 12, color: 'var(--text)' }}>Live Activity</h3>
        <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
          {data.activity.length === 0 ? (
            <div style={{ padding: 24, textAlign: 'center', fontSize: 14, color: 'var(--text-dim)' }}>No activity yet.</div>
          ) : (
            <div className="max-h-64 overflow-y-auto">
              {data.activity.map((e, i) => (
                <div
                  key={i}
                  className="flex items-center gap-3 px-4 py-2"
                  style={{
                    fontSize: 12,
                    borderBottom: '1px solid var(--border-subtle)',
                    background: i % 2 === 0 ? 'var(--bg-card)' : 'var(--bg)',
                  }}
                >
                  <span
                    className="w-1.5 h-1.5 rounded-full flex-shrink-0"
                    style={{ background: e.success ? 'var(--green)' : 'var(--red)' }}
                  />
                  <span className="mono" style={{ color: 'var(--accent)', minWidth: 160 }}>{e.event_type}</span>
                  <span style={{ color: 'var(--text-dim)', minWidth: 80 }}>{e.category}</span>
                  <span style={{ color: 'var(--text-secondary)' }}>{e.agent_name}</span>
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
      className="card"
      style={{
        padding: 20,
        borderLeft: `3px solid ${agent.state === 'running' ? 'var(--green)' : 'var(--border)'}`,
      }}
    >
      {/* Header */}
      <div className="flex items-center justify-between mb-4">
        <div className="flex items-center gap-3">
          <span style={{ fontSize: 18, fontWeight: 600, color: 'var(--text)' }}>{agent.name}</span>
          <span
            style={{
              fontSize: 12,
              padding: '2px 8px',
              borderRadius: 8,
              fontWeight: 500,
              background: agent.state === 'running' ? 'var(--green-light)' : 'var(--red-light)',
              color: agent.state === 'running' ? 'var(--green)' : 'var(--red)',
            }}
          >
            {agent.state}
          </span>
          <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>PID {agent.pid}</span>
          {agent.channels.length > 0 && (
            <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>{agent.channels.join(', ')}</span>
          )}
        </div>
        <div className="flex gap-2">
          <button
            onClick={handleKill}
            style={{
              padding: '4px 12px',
              borderRadius: 12,
              fontSize: 12,
              fontWeight: 600,
              border: '1px solid var(--red)',
              color: 'var(--red)',
              background: 'var(--red-light)',
            }}
          >
            Kill
          </button>
        </div>
      </div>

      <div className="grid grid-cols-3 gap-6">
        {/* Permissions */}
        <div>
          <div style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 8 }}>Tools</div>
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
          <div style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 8 }}>Filesystem</div>
          <div className="space-y-1 mb-3">
            {p.allowed_read_paths.length > 0 ? (
              p.allowed_read_paths.map((path, i) => (
                <div key={i} className="mono" style={{ fontSize: 12, color: 'var(--green)' }}>
                  {path} <span style={{ color: 'var(--text-dim)' }}>read</span>
                </div>
              ))
            ) : (
              <div style={{ fontSize: 12, color: 'var(--text-dim)' }}>No path restrictions</div>
            )}
            {p.allowed_write_paths.map((path, i) => (
              <div key={i} className="mono" style={{ fontSize: 12, color: 'var(--yellow)' }}>
                {path} <span style={{ color: 'var(--text-dim)' }}>write</span>
              </div>
            ))}
          </div>

          <div style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 8 }}>Network</div>
          <div className="space-y-1">
            {p.allowed_domains.length > 0 ? (
              p.allowed_domains.map((d, i) => (
                <div key={i} className="mono" style={{ fontSize: 12, color: 'var(--green)' }}>{d}</div>
              ))
            ) : (
              <div style={{ fontSize: 12, color: 'var(--text-dim)' }}>No domain restrictions</div>
            )}
          </div>
        </div>

        {/* Budget */}
        <div>
          <div style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 8 }}>Budget</div>
          <div className="mono" style={{ fontSize: 24, fontWeight: 600, color: 'var(--accent)' }}>
            ${b.cost_usd.toFixed(4)}
          </div>
          {b.max_cost_usd > 0 && (
            <>
              <div style={{ fontSize: 12, marginTop: 4, color: 'var(--text-dim)' }}>
                of ${b.max_cost_usd.toFixed(2)} ({budgetPct.toFixed(1)}%)
              </div>
              <div className="overflow-hidden" style={{ height: 8, borderRadius: 9999, marginTop: 8, background: 'var(--border)' }}>
                <div
                  className="transition-all"
                  style={{
                    height: '100%',
                    borderRadius: 9999,
                    width: `${budgetPct}%`,
                    background: budgetPct > 80 ? 'var(--red)' : budgetPct > 50 ? 'var(--yellow)' : 'var(--green)',
                  }}
                />
              </div>
            </>
          )}
          <div className="space-y-1" style={{ marginTop: 12, fontSize: 12, color: 'var(--text-dim)' }}>
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
    <div className="flex items-center gap-2" style={{ fontSize: 12 }}>
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
      <div style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>{label}</div>
      <div className="mono" style={{ fontWeight: 600, color }}>{value}</div>
    </div>
  )
}
