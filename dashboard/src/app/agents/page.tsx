'use client'
import { useEffect, useState } from 'react'
import Link from 'next/link'
import { useDemo } from '@/lib/demo-context'

interface AgentDef {
  name: string; description: string; enabled: boolean
  triggers: Array<{ type: string; schedule?: string; source?: string }>
  connections: string[]
  action: { goal: string; tools: string[]; max_steps: number }
  budget: { per_run: number; daily_max: number; daily_spent: number }
  created_at: string; updated_at: string
}

const DEMO_AGENTS: AgentDef[] = [
  { name: 'pr-reviewer', description: 'Reviews new PRs for security and code quality', enabled: true, triggers: [{ type: 'webhook', source: 'github' }], connections: ['github'], action: { goal: 'Review PR for issues', tools: ['read_file', 'exec', 'mcp_call'], max_steps: 15 }, budget: { per_run: 0.30, daily_max: 10, daily_spent: 1.2 }, created_at: '2026-03-28T10:00:00Z', updated_at: '2026-03-30T14:00:00Z' },
  { name: 'api-monitor', description: 'Checks API health every 5 minutes', enabled: true, triggers: [{ type: 'cron', schedule: '*/5 * * * *' }], connections: [], action: { goal: 'Check API health', tools: ['http', 'store'], max_steps: 5 }, budget: { per_run: 0.05, daily_max: 2, daily_spent: 0.45 }, created_at: '2026-03-29T08:00:00Z', updated_at: '2026-03-30T12:00:00Z' },
  { name: 'news-digest', description: 'Compiles AI news every weekday morning', enabled: true, triggers: [{ type: 'cron', schedule: '0 8 * * MON-FRI' }], connections: ['slack'], action: { goal: 'Compile AI news digest', tools: ['search', 'http', 'remember'], max_steps: 12 }, budget: { per_run: 0.40, daily_max: 3, daily_spent: 0.40 }, created_at: '2026-03-27T09:00:00Z', updated_at: '2026-03-30T08:00:00Z' },
  { name: 'dep-scanner', description: 'Weekly dependency security scan', enabled: false, triggers: [{ type: 'cron', schedule: '0 18 * * FRI' }], connections: ['github'], action: { goal: 'Scan dependencies', tools: ['read_file', 'exec', 'http'], max_steps: 10 }, budget: { per_run: 0.50, daily_max: 2, daily_spent: 0 }, created_at: '2026-03-25T16:00:00Z', updated_at: '2026-03-28T18:00:00Z' },
]

const REGISTRY_URL = 'http://localhost:8090'

export default function AgentsPage() {
  const { isDemo } = useDemo()
  const [agents, setAgents] = useState<AgentDef[]>([])
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    if (isDemo) { setAgents(DEMO_AGENTS); setLoading(false); return }
    fetch(`${REGISTRY_URL}/agents`).then(r => r.json()).then(d => { setAgents(d.agents || []); setLoading(false) }).catch(() => { setAgents([]); setLoading(false) })
    const i = setInterval(() => { fetch(`${REGISTRY_URL}/agents`).then(r => r.json()).then(d => setAgents(d.agents || [])).catch(() => {}) }, 5000)
    return () => clearInterval(i)
  }, [isDemo])

  const enabled = agents.filter(a => a.enabled)
  const totalDaily = agents.reduce((s, a) => s + (a.budget?.daily_spent || 0), 0)

  const toggleAgent = async (name: string, enable: boolean) => {
    if (isDemo) { setAgents(prev => prev.map(a => a.name === name ? { ...a, enabled: enable } : a)); return }
    await fetch(`${REGISTRY_URL}/agents/${name}/${enable ? 'enable' : 'disable'}`, { method: 'POST' }).catch(() => {})
  }

  const triggerLabel = (t: AgentDef['triggers'][0]) => {
    if (t.type === 'cron') return t.schedule || 'cron'
    if (t.type === 'webhook') return t.source ? `webhook:${t.source}` : 'webhook'
    return t.type
  }
  const triggerColor = (type: string) => {
    if (type === 'cron') return { bg: 'var(--blue-light)', fg: 'var(--blue)' }
    if (type === 'webhook') return { bg: 'var(--accent-light)', fg: 'var(--accent)' }
    return { bg: 'var(--bg-raised)', fg: 'var(--text-dim)' }
  }

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[24px] font-semibold tracking-[-0.03em]">Agents</h2>
          <p className="text-[14px] mt-1" style={{ color: 'var(--text-dim)' }}>{enabled.length} active · ${totalDaily.toFixed(2)} spent today</p>
        </div>
        <Link href="/agents/new" className="px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--accent)' }}>+ New Agent</Link>
      </div>

      <div className="grid grid-cols-4 gap-3 mb-8">
        <MStat label="Total" value={String(agents.length)} />
        <MStat label="Active" value={String(enabled.length)} color="var(--green)" />
        <MStat label="Daily Spend" value={`$${totalDaily.toFixed(2)}`} color="var(--accent)" />
        <MStat label="Triggers" value={String(agents.reduce((s, a) => s + a.triggers.length, 0))} color="var(--blue)" />
      </div>

      {loading ? <div className="card py-16 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>Loading...</div> :
      agents.length === 0 ? (
        <div className="card py-16 text-center">
          <div className="text-[16px] font-semibold mb-2">No agents yet</div>
          <p className="text-[13px] mb-4" style={{ color: 'var(--text-dim)' }}>Create a persistent agent with triggers, connections, and budget controls.</p>
          <Link href="/agents/new" className="inline-block px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Create Agent</Link>
        </div>
      ) : (
        <div className="space-y-3">
          {agents.map(agent => {
            const pct = agent.budget.daily_max > 0 ? (agent.budget.daily_spent / agent.budget.daily_max) * 100 : 0
            return (
              <div key={agent.name} className="card p-5 hover:shadow-md transition-shadow">
                <div className="flex items-start justify-between mb-3">
                  <div className="flex items-center gap-3">
                    <button onClick={() => toggleAgent(agent.name, !agent.enabled)}
                      className="w-[36px] h-[20px] rounded-full p-[2px] transition-all flex-shrink-0"
                      style={{ background: agent.enabled ? 'var(--green)' : 'var(--border)' }}>
                      <div className="w-[16px] h-[16px] rounded-full bg-white transition-all" style={{ transform: agent.enabled ? 'translateX(16px)' : 'translateX(0)' }} />
                    </button>
                    <div>
                      <div className="text-[15px] font-semibold">{agent.name}</div>
                      <div className="text-[13px]" style={{ color: 'var(--text-dim)' }}>{agent.description}</div>
                    </div>
                  </div>
                  <div className="flex items-center gap-2">
                    {agent.triggers.map((t, i) => { const tc = triggerColor(t.type); return <span key={i} className="text-[10px] font-semibold px-2 py-[3px] rounded-full" style={{ background: tc.bg, color: tc.fg }}>{triggerLabel(t)}</span> })}
                  </div>
                </div>
                <div className="grid grid-cols-4 gap-4">
                  <div>
                    <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Tools</div>
                    <div className="flex flex-wrap gap-1">
                      {agent.action.tools.slice(0, 4).map(t => <span key={t} className="text-[10px] px-1.5 py-0.5 rounded" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>{t}</span>)}
                      {agent.action.tools.length > 4 && <span className="text-[10px]" style={{ color: 'var(--text-dim)' }}>+{agent.action.tools.length - 4}</span>}
                    </div>
                  </div>
                  <div>
                    <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Connections</div>
                    {agent.connections.length > 0 ? <div className="flex gap-1">{agent.connections.map(c => <span key={c} className="text-[11px] font-medium" style={{ color: 'var(--accent)' }}>{c}</span>)}</div> : <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>none</span>}
                  </div>
                  <div>
                    <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Budget</div>
                    <div className="text-[12px] tabular-nums">${agent.budget.daily_spent.toFixed(2)} <span style={{ color: 'var(--text-dim)' }}>/ ${agent.budget.daily_max.toFixed(2)}</span></div>
                    <div className="h-[3px] rounded-full mt-1 overflow-hidden" style={{ background: 'var(--bg)' }}>
                      <div className="h-full rounded-full" style={{ width: `${Math.min(pct, 100)}%`, background: pct > 80 ? 'var(--red)' : 'var(--green)' }} />
                    </div>
                  </div>
                  <div className="flex items-end justify-end">
                    <button onClick={async () => { if (!isDemo) await fetch(`${REGISTRY_URL}/agents/${agent.name}/run`, { method: 'POST' }) }}
                      className="px-3 py-1.5 rounded-lg text-[11px] font-medium" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
                      Run Now
                    </button>
                  </div>
                </div>
              </div>
            )
          })}
        </div>
      )}
    </div>
  )
}

function MStat({ label, value, color }: { label: string; value: string; color?: string }) {
  return <div className="card p-4">
    <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>{label}</div>
    <div className="text-[20px] font-bold tabular-nums" style={{ color: color || 'var(--text)' }}>{value}</div>
  </div>
}
