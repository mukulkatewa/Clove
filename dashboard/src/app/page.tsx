'use client'
import { useEffect, useState } from 'react'
import { getHealth, getCost, getHistory, getOpenClawStatus, getAudit } from '@/lib/api'
import type { HealthResponse, CostResponse, HistoryEntry, OpenClawInstance, AuditEntry } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoHealth, demoCost, demoRuns, demoOpenClaw, demoAudit } from '@/lib/demo-data'

export default function Overview() {
  const { isDemo } = useDemo()
  const [health, setHealth] = useState<HealthResponse | null>(null)
  const [cost, setCost] = useState<CostResponse | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])
  const [instances, setInstances] = useState<OpenClawInstance[]>([])
  const [audit, setAudit] = useState<AuditEntry[]>([])
  const [error, setError] = useState('')

  useEffect(() => {
    if (isDemo) { setHealth(demoHealth() as any); setCost(demoCost() as any); setHistory(demoRuns() as any); setInstances(demoOpenClaw() as any); setAudit(demoAudit() as any); setError(''); return }
    const load = () => {
      getHealth().then(setHealth).catch(() => setError('offline'))
      getCost().then(setCost).catch(() => {})
      getHistory().then((r) => setHistory(r.runs || [])).catch(() => {})
      getOpenClawStatus().then((r) => setInstances(r.instances || [])).catch(() => {})
      getAudit(10).then(setAudit).catch(() => {})
    }
    load()
    const interval = setInterval(load, 5000)
    return () => clearInterval(interval)
  }, [isDemo])

  if (error) {
    return (
      <div className="flex items-center justify-center h-[60vh]">
        <div className="text-center">
          <div className="w-12 h-12 rounded-2xl mx-auto mb-4 flex items-center justify-center" style={{ background: 'var(--red-light)' }}>
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="var(--red)" strokeWidth="2"><circle cx="12" cy="12" r="10"/><path d="M12 8v4m0 4h.01"/></svg>
          </div>
          <div className="text-[18px] font-semibold mb-2">Kernel Offline</div>
          <p className="text-[13px] mb-5" style={{ color: 'var(--text-dim)' }}>Not reachable at localhost:8080</p>
          <div className="inline-block card px-4 py-3">
            <code className="mono text-[12px]" style={{ color: 'var(--text-secondary)' }}>./clove_kernel --sandbox --privacy --api</code>
          </div>
        </div>
      </div>
    )
  }

  const running = instances.filter((i) => i.state === 'running')
  const totalCost = cost?.total_cost_usd ?? 0

  return (
    <div>
      {/* Header */}
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[24px] font-semibold tracking-[-0.03em]">Welcome to Clove</h2>
          <p className="text-[14px] mt-1" style={{ color: 'var(--text-dim)' }}>
            {health ? `Kernel v${health.version} · ${health.syscall_count} syscalls · ${fmtUp(health.uptime_s)} uptime` : 'Connecting...'}
          </p>
        </div>
        <div className="flex items-center gap-2 px-3 py-1.5 rounded-full" style={{ background: health ? 'var(--green-light)' : 'var(--red-light)' }}>
          <span className="w-[7px] h-[7px] rounded-full" style={{ background: health ? 'var(--green)' : 'var(--red)' }} />
          <span className="text-[12px] font-medium" style={{ color: health ? 'var(--green)' : 'var(--red)' }}>{health ? 'Running' : 'Offline'}</span>
        </div>
      </div>

      {/* Hero card + stats */}
      <div className="grid grid-cols-3 gap-4 mb-8">
        {/* Hero kernel card */}
        <div className="rounded-2xl p-6 text-white relative overflow-hidden" style={{ background: 'linear-gradient(135deg, #e07628 0%, #f59e0b 50%, #fbbf24 100%)' }}>
          <div className="absolute top-0 right-0 w-32 h-32 rounded-full opacity-10" style={{ background: 'white', transform: 'translate(30%, -30%)' }} />
          <div className="text-[11px] uppercase tracking-[0.08em] opacity-70 mb-1">Kernel</div>
          <div className="text-[28px] font-bold tracking-[-0.03em]">v{health?.version || '2.0.0'}</div>
          <div className="text-[13px] opacity-70 mt-1">{health?.syscall_count || 86} syscalls registered</div>
          <div className="flex items-center gap-4 mt-4 text-[12px] opacity-80">
            <span>{running.length} agents active</span>
            <span>·</span>
            <span>{history.length} runs</span>
          </div>
        </div>

        {/* Cost card */}
        <div className="card p-5">
          <div className="flex items-center justify-between mb-2">
            <span className="text-[11px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>Total Cost</span>
            <span className="text-[10px] px-2 py-0.5 rounded-full" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>All time</span>
          </div>
          <div className="text-[26px] font-bold tracking-[-0.03em] tabular-nums" style={{ color: totalCost > 0 ? 'var(--text)' : 'var(--text-dim)' }}>
            ${totalCost.toFixed(2)}
          </div>
          {cost?.max_cost_usd ? (
            <div className="mt-3">
              <div className="flex justify-between text-[11px] mb-1" style={{ color: 'var(--text-dim)' }}>
                <span>Budget</span><span>${cost.max_cost_usd.toFixed(2)}</span>
              </div>
              <div className="h-[5px] rounded-full overflow-hidden" style={{ background: 'var(--bg-raised)' }}>
                <div className="h-full rounded-full transition-all" style={{ width: `${Math.min((totalCost / cost.max_cost_usd) * 100, 100)}%`, background: 'var(--accent)' }} />
              </div>
            </div>
          ) : <div className="text-[12px] mt-2" style={{ color: 'var(--text-dim)' }}>No budget set</div>}
        </div>

        {/* Agents card */}
        <div className="card p-5">
          <div className="flex items-center justify-between mb-2">
            <span className="text-[11px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>Active Agents</span>
          </div>
          <div className="text-[26px] font-bold tracking-[-0.03em] tabular-nums" style={{ color: running.length > 0 ? 'var(--green)' : 'var(--text-dim)' }}>
            {running.length}
          </div>
          <div className="text-[12px] mt-2" style={{ color: 'var(--text-dim)' }}>{instances.length} total instances</div>
          {running.length > 0 && (
            <div className="flex flex-wrap gap-1.5 mt-3">
              {running.map((inst) => (
                <span key={inst.id} className="text-[11px] px-2 py-0.5 rounded-full font-medium" style={{ background: 'var(--green-light)', color: 'var(--green)' }}>
                  {inst.name}
                </span>
              ))}
            </div>
          )}
        </div>
      </div>

      {/* Recent runs */}
      <div className="mb-8">
        <div className="flex items-center justify-between mb-4">
          <h3 className="text-[16px] font-semibold">Recent Runs</h3>
          <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>{history.length} total</span>
        </div>
        <div className="card overflow-hidden">
          {history.length === 0 ? (
            <div className="py-14 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No runs yet. Submit one from the Runs page.</div>
          ) : (
            <table className="w-full text-[13px]">
              <thead><tr style={{ borderBottom: '1px solid var(--border)' }}>
                {['Name', 'Goal', 'Chain', 'Time'].map((h, i) => (
                  <th key={h} className={`px-5 py-3 text-[11px] font-medium uppercase tracking-[0.06em] ${i === 3 ? 'text-right' : 'text-left'}`} style={{ color: 'var(--text-dim)', background: 'var(--bg)' }}>{h}</th>
                ))}
              </tr></thead>
              <tbody>
                {history.slice(0, 8).map((run, i) => (
                  <tr key={i} className="hover:bg-[var(--bg)]" style={{ borderBottom: '1px solid var(--border-subtle)', transition: 'background 100ms' }}>
                    <td className="px-5 py-3 mono text-[12px] font-medium" style={{ color: 'var(--accent)' }}>{run.name}</td>
                    <td className="px-5 py-3 truncate max-w-xs" style={{ color: 'var(--text-secondary)' }}>{run.description}</td>
                    <td className="px-5 py-3 mono text-[12px]" style={{ color: 'var(--text-dim)' }}>{run.chain_id.slice(0, 12)}</td>
                    <td className="px-5 py-3 text-right text-[12px]" style={{ color: 'var(--text-dim)' }}>{new Date(run.created_at_ms).toLocaleTimeString()}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </div>

      {/* Audit */}
      <div>
        <div className="flex items-center justify-between mb-4">
          <h3 className="text-[16px] font-semibold">Recent Audit</h3>
        </div>
        <div className="card overflow-hidden">
          {audit.length === 0 ? (
            <div className="py-14 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No audit entries yet.</div>
          ) : (
            <table className="w-full text-[13px]">
              <thead><tr style={{ borderBottom: '1px solid var(--border)' }}>
                {['Event', 'Category', 'Agent', 'Status', 'Time'].map((h, i) => (
                  <th key={h} className={`px-5 py-3 text-[11px] font-medium uppercase tracking-[0.06em] ${i === 4 ? 'text-right' : 'text-left'}`} style={{ color: 'var(--text-dim)', background: 'var(--bg)' }}>{h}</th>
                ))}
              </tr></thead>
              <tbody>
                {audit.map((e) => (
                  <tr key={e.id} className="hover:bg-[var(--bg)]" style={{ borderBottom: '1px solid var(--border-subtle)' }}>
                    <td className="px-5 py-3 mono text-[12px] font-medium">{e.event_type}</td>
                    <td className="px-5 py-3" style={{ color: 'var(--text-secondary)' }}>{e.category}</td>
                    <td className="px-5 py-3" style={{ color: 'var(--text-secondary)' }}>{e.agent_name || `#${e.agent_id}`}</td>
                    <td className="px-5 py-3">
                      <span className="text-[10px] font-semibold px-2 py-[3px] rounded-full" style={{ background: e.success ? 'var(--green-light)' : 'var(--red-light)', color: e.success ? 'var(--green)' : 'var(--red)' }}>
                        {e.success ? 'OK' : 'FAIL'}
                      </span>
                    </td>
                    <td className="px-5 py-3 text-right text-[12px]" style={{ color: 'var(--text-dim)' }}>{new Date(e.timestamp).toLocaleTimeString()}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </div>
    </div>
  )
}

function fmtUp(s: number): string {
  if (s > 86400) return `${Math.floor(s / 86400)}d ${Math.floor((s % 86400) / 3600)}h`
  if (s > 3600) return `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m`
  return `${Math.floor(s / 60)}m`
}
