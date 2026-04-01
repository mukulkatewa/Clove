'use client'
import { useEffect, useState } from 'react'
import Link from 'next/link'
import { useDemo } from '@/lib/demo-context'
import { getAgentDefs } from '@/lib/api'

interface Agent {
  name: string; description: string; enabled: boolean; state: 'running' | 'idle' | 'stopped'
  triggers: Array<{ type: string; schedule?: string; source?: string }>
  connections: string[]; tools: string[]
  budget: { per_run: number; daily_max: number; daily_spent: number }
  runs_today: number; last_run?: string; success_rate: number
  sandbox: { can_exec: boolean; can_http: boolean; can_write: boolean; allowed_domains: string[]; allowed_paths: string[] }
  mcp_servers: string[]
}

const DEMO: Agent[] = [
  { name: 'pr-reviewer', description: 'Reviews new PRs for security and code quality', enabled: true, state: 'running',
    triggers: [{ type: 'webhook', source: 'github' }], connections: ['github'], tools: ['read_file', 'exec', 'mcp_call'],
    budget: { per_run: 0.30, daily_max: 10, daily_spent: 2.4 }, runs_today: 8, last_run: '2 min ago', success_rate: 94,
    sandbox: { can_exec: true, can_http: true, can_write: false, allowed_domains: ['api.github.com'], allowed_paths: ['/tmp'] },
    mcp_servers: ['github'] },
  { name: 'api-monitor', description: 'Checks API health every 5 minutes, alerts on degradation', enabled: true, state: 'running',
    triggers: [{ type: 'cron', schedule: '*/5 * * * *' }], connections: [], tools: ['http', 'store'],
    budget: { per_run: 0.05, daily_max: 2, daily_spent: 0.65 }, runs_today: 13, last_run: '3 min ago', success_rate: 100,
    sandbox: { can_exec: false, can_http: true, can_write: false, allowed_domains: ['api.example.com', 'status.example.com'], allowed_paths: [] },
    mcp_servers: [] },
  { name: 'incident-responder', description: 'Diagnoses production incidents and proposes fixes', enabled: true, state: 'idle',
    triggers: [{ type: 'webhook', source: 'pagerduty' }], connections: ['github', 'slack'], tools: ['exec', 'read_file', 'http', 'mcp_call', 'delegate'],
    budget: { per_run: 1.50, daily_max: 15, daily_spent: 3.2 }, runs_today: 2, last_run: '1 hour ago', success_rate: 85,
    sandbox: { can_exec: true, can_http: true, can_write: true, allowed_domains: ['api.github.com', 'api.slack.com', 'grafana.internal'], allowed_paths: ['/app', '/tmp'] },
    mcp_servers: ['github', 'slack'] },
  { name: 'dep-scanner', description: 'Weekly dependency vulnerability scan and report', enabled: false, state: 'stopped',
    triggers: [{ type: 'cron', schedule: '0 18 * * FRI' }], connections: ['github'], tools: ['read_file', 'exec', 'http'],
    budget: { per_run: 0.50, daily_max: 2, daily_spent: 0 }, runs_today: 0, last_run: '3 days ago', success_rate: 92,
    sandbox: { can_exec: true, can_http: true, can_write: false, allowed_domains: ['api.github.com', 'registry.npmjs.org'], allowed_paths: ['/app'] },
    mcp_servers: ['github'] },
]

const STATE_STYLE: Record<string, { bg: string; fg: string; label: string }> = {
  running: { bg: 'var(--green-light)', fg: 'var(--green)', label: 'Running' },
  idle: { bg: 'var(--yellow-light)', fg: 'var(--yellow)', label: 'Idle' },
  stopped: { bg: 'var(--bg-raised)', fg: 'var(--text-dim)', label: 'Stopped' },
}

export default function AgentsPage() {
  const { isDemo } = useDemo()
  const [agents, setAgents] = useState<Agent[]>([])
  const [selected, setSelected] = useState<Agent | null>(null)
  const [view, setView] = useState<'list' | 'grid'>('list')

  useEffect(() => {
    if (isDemo) { setAgents(DEMO); return }
    const load = () => {
      getAgentDefs().then(r => {
        const defs = (r.agents || []).map((a: any) => ({
          ...a,
          state: a.enabled ? 'running' as const : 'stopped' as const,
          tools: a.action?.tools || [],
          budget: { per_run: a.budget?.per_run || 0, daily_max: a.budget?.daily_max || 0, daily_spent: a.budget?.daily_spent || 0 },
          runs_today: 0, success_rate: 100,
          sandbox: a.permissions || { can_exec: false, can_http: false, can_write: false, allowed_domains: [], allowed_paths: [] },
          mcp_servers: [],
        }))
        setAgents(defs)
      }).catch(() => {})
    }
    load(); const i = setInterval(load, 5000); return () => clearInterval(i)
  }, [isDemo])

  const running = agents.filter(a => a.state === 'running').length
  const totalSpend = agents.reduce((s, a) => s + a.budget.daily_spent, 0)
  const totalRuns = agents.reduce((s, a) => s + a.runs_today, 0)

  return (
    <div className="flex gap-0 -mx-10 -my-8 h-[calc(100vh)]">
      {/* Main area */}
      <div className="flex-1 overflow-y-auto px-8 py-6">
        {/* Header */}
        <div className="flex items-center justify-between mb-6">
          <div>
            <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Agents</h2>
            <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>
              {running} running · {totalRuns} runs today · ${totalSpend.toFixed(2)} spent
            </p>
          </div>
          <div className="flex items-center gap-2">
            <div className="flex rounded-lg overflow-hidden" style={{ border: '1px solid var(--border)' }}>
              <button onClick={() => setView('list')} className="px-3 py-1.5 text-[11px] font-medium" style={{ background: view === 'list' ? 'var(--bg-raised)' : 'var(--bg-card)', color: view === 'list' ? 'var(--text)' : 'var(--text-dim)' }}>List</button>
              <button onClick={() => setView('grid')} className="px-3 py-1.5 text-[11px] font-medium" style={{ background: view === 'grid' ? 'var(--bg-raised)' : 'var(--bg-card)', color: view === 'grid' ? 'var(--text)' : 'var(--text-dim)' }}>Grid</button>
            </div>
            <Link href="/agents/new" className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>
              + New Agent
            </Link>
          </div>
        </div>

        {/* Quick stats */}
        <div className="grid grid-cols-4 gap-2 mb-6">
          {[
            { label: 'Total', value: String(agents.length), color: 'var(--text)' },
            { label: 'Running', value: String(running), color: 'var(--green)' },
            { label: 'Today', value: `${totalRuns} runs`, color: 'var(--blue)' },
            { label: 'Spent', value: `$${totalSpend.toFixed(2)}`, color: 'var(--accent)' },
          ].map(s => (
            <div key={s.label} className="card px-4 py-3">
              <div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>{s.label}</div>
              <div className="text-[17px] font-bold tabular-nums mt-0.5" style={{ color: s.color }}>{s.value}</div>
            </div>
          ))}
        </div>

        {/* Agent list */}
        {agents.length === 0 ? (
          <div className="card py-20 text-center">
            <div className="text-[40px] mb-3 opacity-20">
              <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1" className="mx-auto"><path d="M17 21v-2a4 4 0 00-4-4H5a4 4 0 00-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 00-3-3.87"/><path d="M16 3.13a4 4 0 010 7.75"/></svg>
            </div>
            <div className="text-[15px] font-semibold mb-1">No agents yet</div>
            <p className="text-[13px] mb-5" style={{ color: 'var(--text-dim)' }}>Create your first agent to automate tasks, monitor systems, or handle events.</p>
            <Link href="/agents/new" className="inline-block px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Create your first agent</Link>
          </div>
        ) : view === 'list' ? (
          <div className="space-y-2">
            {agents.map(agent => {
              const ss = STATE_STYLE[agent.state]
              const isSelected = selected?.name === agent.name
              return (
                <button key={agent.name} onClick={() => setSelected(isSelected ? null : agent)}
                  className="card w-full text-left p-4 transition-all hover:shadow-md"
                  style={{ borderColor: isSelected ? 'var(--accent)' : undefined, borderWidth: isSelected ? 2 : 1 }}>
                  <div className="flex items-center gap-4">
                    {/* Status + toggle */}
                    <div className="w-[40px] h-[40px] rounded-xl flex items-center justify-center flex-shrink-0" style={{ background: ss.bg }}>
                      <div className={`w-2.5 h-2.5 rounded-full ${agent.state === 'running' ? 'animate-pulse' : ''}`} style={{ background: ss.fg }} />
                    </div>

                    {/* Info */}
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-[14px] font-semibold">{agent.name}</span>
                        <span className="text-[10px] font-semibold px-1.5 py-[2px] rounded-full" style={{ background: ss.bg, color: ss.fg }}>{ss.label}</span>
                      </div>
                      <div className="text-[12px] mt-0.5 truncate" style={{ color: 'var(--text-dim)' }}>{agent.description}</div>
                    </div>

                    {/* Trigger */}
                    <div className="text-right flex-shrink-0">
                      {agent.triggers.map((t, i) => (
                        <div key={i} className="text-[10px] font-medium px-2 py-[2px] rounded-full inline-block"
                          style={{ background: t.type === 'cron' ? 'var(--blue-light)' : t.type === 'webhook' ? 'var(--accent-light)' : 'var(--bg-raised)', color: t.type === 'cron' ? 'var(--blue)' : t.type === 'webhook' ? 'var(--accent)' : 'var(--text-dim)' }}>
                          {t.type === 'cron' ? t.schedule : t.type === 'webhook' ? `${t.source}` : 'manual'}
                        </div>
                      ))}
                      {agent.last_run && <div className="text-[10px] mt-1" style={{ color: 'var(--text-dim)' }}>{agent.last_run}</div>}
                    </div>

                    {/* Stats */}
                    <div className="flex items-center gap-4 flex-shrink-0 text-[11px] tabular-nums" style={{ color: 'var(--text-dim)' }}>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--text)' }}>{agent.runs_today}</div><div>runs</div></div>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--green)' }}>{agent.success_rate}%</div><div>success</div></div>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--accent)' }}>${agent.budget.daily_spent.toFixed(2)}</div><div>spent</div></div>
                    </div>
                  </div>
                </button>
              )
            })}
          </div>
        ) : (
          <div className="grid grid-cols-2 gap-3">
            {agents.map(agent => {
              const ss = STATE_STYLE[agent.state]
              return (
                <button key={agent.name} onClick={() => setSelected(agent)} className="card p-5 text-left hover:shadow-md transition-all">
                  <div className="flex items-center justify-between mb-3">
                    <div className="flex items-center gap-2">
                      <div className={`w-2.5 h-2.5 rounded-full ${agent.state === 'running' ? 'animate-pulse' : ''}`} style={{ background: ss.fg }} />
                      <span className="text-[14px] font-semibold">{agent.name}</span>
                    </div>
                    <span className="text-[10px] font-semibold px-1.5 py-[2px] rounded-full" style={{ background: ss.bg, color: ss.fg }}>{ss.label}</span>
                  </div>
                  <p className="text-[12px] mb-3 line-clamp-2" style={{ color: 'var(--text-dim)' }}>{agent.description}</p>
                  <div className="flex gap-1 mb-3 flex-wrap">
                    {agent.connections.map(c => <span key={c} className="text-[10px] font-medium px-1.5 py-0.5 rounded" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{c}</span>)}
                    {agent.mcp_servers.map(s => <span key={s} className="text-[10px] font-medium px-1.5 py-0.5 rounded" style={{ background: 'var(--blue-light)', color: 'var(--blue)' }}>mcp:{s}</span>)}
                  </div>
                  <div className="flex justify-between text-[11px]" style={{ color: 'var(--text-dim)' }}>
                    <span>{agent.runs_today} runs</span>
                    <span className="tabular-nums">${agent.budget.daily_spent.toFixed(2)}</span>
                  </div>
                </button>
              )
            })}
          </div>
        )}
      </div>

      {/* Detail panel */}
      {selected && (
        <div className="w-[380px] overflow-y-auto border-l flex-shrink-0" style={{ background: 'var(--bg)', borderColor: 'var(--border)' }}>
          <div className="px-6 py-5 sticky top-0 z-10 flex items-center justify-between" style={{ background: 'var(--bg)', borderBottom: '1px solid var(--border)' }}>
            <h3 className="text-[15px] font-semibold">{selected.name}</h3>
            <button onClick={() => setSelected(null)} className="text-[12px]" style={{ color: 'var(--text-dim)' }}>Close</button>
          </div>

          <div className="px-6 py-4 space-y-5">
            {/* Status */}
            <div className="flex items-center gap-3">
              <div className={`w-3 h-3 rounded-full ${selected.state === 'running' ? 'animate-pulse' : ''}`} style={{ background: STATE_STYLE[selected.state].fg }} />
              <span className="text-[13px] font-medium capitalize">{selected.state}</span>
              {selected.last_run && <span className="text-[11px] ml-auto" style={{ color: 'var(--text-dim)' }}>Last run: {selected.last_run}</span>}
            </div>

            <p className="text-[13px]" style={{ color: 'var(--text-secondary)' }}>{selected.description}</p>

            {/* Budget visual */}
            <DetailSection title="Budget">
              <div className="flex items-end justify-between mb-2">
                <span className="text-[20px] font-bold tabular-nums" style={{ color: 'var(--accent)' }}>${selected.budget.daily_spent.toFixed(2)}</span>
                <span className="text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>/ ${selected.budget.daily_max.toFixed(2)} daily</span>
              </div>
              <div className="h-[6px] rounded-full overflow-hidden" style={{ background: 'var(--bg-raised)' }}>
                <div className="h-full rounded-full transition-all" style={{ width: `${(selected.budget.daily_spent / selected.budget.daily_max) * 100}%`, background: selected.budget.daily_spent / selected.budget.daily_max > 0.8 ? 'var(--red)' : 'var(--accent)' }} />
              </div>
              <div className="flex justify-between mt-2 text-[11px]" style={{ color: 'var(--text-dim)' }}>
                <span>${selected.budget.per_run.toFixed(2)} per run</span>
                <span>{selected.runs_today} runs today</span>
              </div>
            </DetailSection>

            {/* Connections & MCP */}
            <DetailSection title="Connections">
              {selected.connections.length === 0 && selected.mcp_servers.length === 0 ? (
                <div className="text-[12px]" style={{ color: 'var(--text-dim)' }}>No external connections</div>
              ) : (
                <div className="space-y-2">
                  {selected.connections.map(c => (
                    <div key={c} className="flex items-center gap-2 px-3 py-2 rounded-lg" style={{ background: 'var(--bg-raised)' }}>
                      <div className="w-2 h-2 rounded-full" style={{ background: 'var(--green)' }} />
                      <span className="text-[12px] font-medium capitalize">{c}</span>
                      <span className="text-[10px] ml-auto" style={{ color: 'var(--text-dim)' }}>service</span>
                    </div>
                  ))}
                  {selected.mcp_servers.map(s => (
                    <div key={s} className="flex items-center gap-2 px-3 py-2 rounded-lg" style={{ background: 'var(--bg-raised)' }}>
                      <div className="w-2 h-2 rounded-full" style={{ background: 'var(--blue)' }} />
                      <span className="text-[12px] font-medium">{s}</span>
                      <span className="text-[10px] ml-auto" style={{ color: 'var(--text-dim)' }}>MCP</span>
                    </div>
                  ))}
                </div>
              )}
            </DetailSection>

            {/* Sandbox */}
            <DetailSection title="Sandbox">
              <div className="grid grid-cols-2 gap-2">
                {[
                  { label: 'Shell', allowed: selected.sandbox.can_exec },
                  { label: 'HTTP', allowed: selected.sandbox.can_http },
                  { label: 'Write', allowed: selected.sandbox.can_write },
                ].map(p => (
                  <div key={p.label} className="flex items-center gap-2 text-[12px]">
                    <span style={{ color: p.allowed ? 'var(--green)' : 'var(--red)' }}>{p.allowed ? '✓' : '✗'}</span>
                    <span style={{ color: p.allowed ? 'var(--text)' : 'var(--text-dim)' }}>{p.label}</span>
                  </div>
                ))}
              </div>
              {selected.sandbox.allowed_domains.length > 0 && (
                <div className="mt-3">
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Allowed Domains</div>
                  <div className="space-y-1">
                    {selected.sandbox.allowed_domains.map(d => (
                      <div key={d} className="text-[11px] mono px-2 py-1 rounded" style={{ background: 'var(--bg-raised)', color: 'var(--green)' }}>{d}</div>
                    ))}
                  </div>
                </div>
              )}
              {selected.sandbox.allowed_paths.length > 0 && (
                <div className="mt-3">
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Allowed Paths</div>
                  <div className="space-y-1">
                    {selected.sandbox.allowed_paths.map(p => (
                      <div key={p} className="text-[11px] mono px-2 py-1 rounded" style={{ background: 'var(--bg-raised)', color: 'var(--text-secondary)' }}>{p}</div>
                    ))}
                  </div>
                </div>
              )}
            </DetailSection>

            {/* Tools */}
            <DetailSection title="Tools">
              <div className="flex flex-wrap gap-1.5">
                {selected.tools.map(t => (
                  <span key={t} className="text-[10px] font-medium px-2 py-1 rounded-lg" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{t}</span>
                ))}
              </div>
            </DetailSection>

            {/* Trigger */}
            <DetailSection title="Trigger">
              {selected.triggers.map((t, i) => (
                <div key={i} className="px-3 py-2.5 rounded-lg" style={{ background: 'var(--bg-raised)' }}>
                  <div className="text-[12px] font-medium capitalize">{t.type}</div>
                  {t.schedule && <div className="text-[11px] mono mt-0.5" style={{ color: 'var(--text-dim)' }}>{t.schedule}</div>}
                  {t.source && <div className="text-[11px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Source: {t.source}</div>}
                </div>
              ))}
            </DetailSection>

            {/* Actions */}
            <div className="flex gap-2 pt-2">
              <button className="flex-1 px-4 py-2.5 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Run Now</button>
              <button className="px-4 py-2.5 rounded-lg text-[12px] font-medium" style={{ background: 'var(--bg-raised)', color: 'var(--text-secondary)' }}>Edit</button>
              <button className="px-4 py-2.5 rounded-lg text-[12px] font-medium" style={{ color: 'var(--red)' }}>Delete</button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

function DetailSection({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div>
      <div className="text-[10px] uppercase tracking-[0.06em] font-semibold mb-2" style={{ color: 'var(--text-dim)' }}>{title}</div>
      {children}
    </div>
  )
}
