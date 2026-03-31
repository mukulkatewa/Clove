'use client'
import { useEffect, useState } from 'react'
import { getWorlds, createWorld, deleteWorld } from '@/lib/api'
import type { WorldSummary } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoWorlds } from '@/lib/demo-data'

interface TeamRole { name: string; status: 'active' | 'idle' | 'waiting'; task: string; cost: number }

interface Team extends WorldSummary {
  roles: TeamRole[]
  total_cost: number
  status: 'running' | 'idle' | 'completed'
}

const DEMO_TEAMS: Team[] = [
  {
    id: 1, name: 'research-sandbox', member_count: 3, metadata: { topic: 'AI agents market', phase: 'synthesis' },
    status: 'running', total_cost: 1.24,
    roles: [
      { name: 'researcher-1', status: 'idle', task: 'Primary market research', cost: 0.42 },
      { name: 'researcher-2', status: 'idle', task: 'Competitive analysis', cost: 0.38 },
      { name: 'synthesizer', status: 'active', task: 'Merging findings into report', cost: 0.44 },
    ],
  },
  {
    id: 2, name: 'incident-room-47', member_count: 4, metadata: { incident: 'API latency spike', severity: 'high' },
    status: 'running', total_cost: 2.15,
    roles: [
      { name: 'sentinel', status: 'idle', task: 'Detected: p99 latency > 4s', cost: 0.08 },
      { name: 'diagnostician', status: 'idle', task: 'Root cause: connection pool exhaustion', cost: 0.62 },
      { name: 'fixer', status: 'active', task: 'Applying pool size increase', cost: 0.95 },
      { name: 'verifier', status: 'waiting', task: 'Waiting for fix to be applied', cost: 0.50 },
    ],
  },
  {
    id: 3, name: 'compliance-audit', member_count: 2, metadata: { regulation: 'EU AI Act', deadline: '2026-04-15' },
    status: 'completed', total_cost: 0.89,
    roles: [
      { name: 'auditor', status: 'idle', task: 'Reviewed 86 syscalls for compliance', cost: 0.52 },
      { name: 'reporter', status: 'idle', task: 'Generated compliance report', cost: 0.37 },
    ],
  },
]

const STATUS_STYLES: Record<string, { bg: string; fg: string }> = {
  running: { bg: 'var(--green-light)', fg: 'var(--green)' },
  idle: { bg: 'var(--bg-raised)', fg: 'var(--text-dim)' },
  completed: { bg: 'var(--blue-light)', fg: 'var(--blue)' },
  active: { bg: 'var(--green-light)', fg: 'var(--green)' },
  waiting: { bg: 'var(--yellow-light)', fg: 'var(--yellow)' },
}

export default function WorldsPage() {
  const { isDemo } = useDemo()
  const [worlds, setWorlds] = useState<Team[]>([])
  const [name, setName] = useState('')
  const [expanded, setExpanded] = useState<number | null>(null)

  useEffect(() => {
    if (isDemo) { setWorlds(DEMO_TEAMS); return }
    getWorlds().then(r => {
      setWorlds((r.worlds || []).map(w => ({ ...w, roles: [], total_cost: 0, status: w.member_count > 0 ? 'running' as const : 'idle' as const })))
    }).catch(() => {})
    const i = setInterval(() => {
      getWorlds().then(r => setWorlds((r.worlds || []).map(w => ({ ...w, roles: [], total_cost: 0, status: w.member_count > 0 ? 'running' as const : 'idle' as const })))).catch(() => {})
    }, 3000)
    return () => clearInterval(i)
  }, [isDemo])

  const handleCreate = async () => {
    if (!name.trim()) return
    await createWorld(name.trim()).catch(() => {})
    setName('')
  }

  const runningTeams = worlds.filter(w => w.status === 'running')
  const totalCost = worlds.reduce((s, w) => s + w.total_cost, 0)

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Worlds</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>
            Isolated team environments. {runningTeams.length} active · ${totalCost.toFixed(2)} total cost
          </p>
        </div>
      </div>

      {/* Create */}
      <div className="card p-4 mb-6">
        <div className="flex gap-3">
          <input value={name} onChange={e => setName(e.target.value)} placeholder="New world name"
            className="flex-1 rounded-lg px-3 py-2.5 text-[13px] outline-none placeholder:opacity-25"
            style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
            onKeyDown={e => { if (e.key === 'Enter') handleCreate() }} />
          <button onClick={handleCreate} disabled={!name.trim()} className="px-4 py-2.5 rounded-lg text-[12px] font-semibold text-white disabled:opacity-30" style={{ background: 'var(--accent)' }}>Create World</button>
        </div>
      </div>

      {/* Teams */}
      {worlds.length === 0 ? (
        <div className="card py-16 text-center">
          <div className="text-[15px] font-semibold mb-1">No worlds</div>
          <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>Worlds are isolated environments where agent teams coordinate on complex tasks.</p>
        </div>
      ) : (
        <div className="space-y-3">
          {worlds.map(team => {
            const ss = STATUS_STYLES[team.status]
            const isOpen = expanded === team.id
            return (
              <div key={team.id} className="card overflow-hidden">
                <button onClick={() => setExpanded(isOpen ? null : team.id)} className="w-full text-left p-5 hover:bg-[var(--bg)] transition-colors">
                  <div className="flex items-center gap-4">
                    {/* Team visual — stacked role circles */}
                    <div className="flex -space-x-2 flex-shrink-0">
                      {team.roles.slice(0, 4).map((r, i) => {
                        const rs = STATUS_STYLES[r.status] || STATUS_STYLES.idle
                        return (
                          <div key={i} className="w-9 h-9 rounded-full flex items-center justify-center text-[10px] font-bold border-2"
                            style={{ background: rs.bg, color: rs.fg, borderColor: 'var(--bg-card)', zIndex: 4 - i }}>
                            {r.name.slice(0, 2).toUpperCase()}
                          </div>
                        )
                      })}
                      {team.roles.length === 0 && <div className="w-9 h-9 rounded-full flex items-center justify-center text-[9px]" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>{team.member_count}</div>}
                    </div>

                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-[14px] font-semibold">{team.name}</span>
                        <span className="text-[9px] font-semibold uppercase px-1.5 py-[2px] rounded-full" style={{ background: ss.bg, color: ss.fg }}>{team.status}</span>
                      </div>
                      {team.metadata && Object.keys(team.metadata).length > 0 && (
                        <div className="text-[11px] mt-0.5 flex gap-2" style={{ color: 'var(--text-dim)' }}>
                          {Object.entries(team.metadata).slice(0, 3).map(([k, v]) => (
                            <span key={k}>{k}: <strong>{String(v)}</strong></span>
                          ))}
                        </div>
                      )}
                    </div>

                    <div className="flex gap-4 text-[11px] tabular-nums flex-shrink-0" style={{ color: 'var(--text-dim)' }}>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--text)' }}>{team.roles.length || team.member_count}</div>roles</div>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--accent)' }}>${team.total_cost.toFixed(2)}</div>cost</div>
                    </div>

                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ color: 'var(--text-dim)', transform: isOpen ? 'rotate(180deg)' : '', transition: '150ms' }}><polyline points="6 9 12 15 18 9"/></svg>
                  </div>
                </button>

                {isOpen && team.roles.length > 0 && (
                  <div className="px-5 pb-5" style={{ borderTop: '1px solid var(--border)' }}>
                    {/* Role cards */}
                    <div className="grid grid-cols-2 gap-3 pt-4">
                      {team.roles.map((role, i) => {
                        const rs = STATUS_STYLES[role.status] || STATUS_STYLES.idle
                        return (
                          <div key={i} className="rounded-xl p-3.5" style={{ background: 'var(--bg)', border: '1px solid var(--border)' }}>
                            <div className="flex items-center justify-between mb-2">
                              <div className="flex items-center gap-2">
                                <div className={`w-2 h-2 rounded-full ${role.status === 'active' ? 'animate-pulse' : ''}`} style={{ background: rs.fg }} />
                                <span className="text-[13px] font-semibold">{role.name}</span>
                              </div>
                              <span className="text-[9px] font-semibold uppercase px-1.5 py-[2px] rounded-full" style={{ background: rs.bg, color: rs.fg }}>{role.status}</span>
                            </div>
                            <div className="text-[11px] mb-2" style={{ color: 'var(--text-dim)' }}>{role.task}</div>
                            <div className="text-[11px] tabular-nums font-medium" style={{ color: 'var(--accent)' }}>${role.cost.toFixed(2)}</div>
                          </div>
                        )
                      })}
                    </div>

                    <div className="flex gap-2 mt-4">
                      <button className="px-4 py-2 rounded-lg text-[11px] font-medium" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>Snapshot</button>
                      <button onClick={() => { deleteWorld(team.id); setWorlds(w => w.filter(x => x.id !== team.id)) }}
                        className="px-4 py-2 rounded-lg text-[11px] font-medium" style={{ color: 'var(--red)' }}>Destroy</button>
                    </div>
                  </div>
                )}
              </div>
            )
          })}
        </div>
      )}
    </div>
  )
}
