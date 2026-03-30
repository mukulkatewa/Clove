'use client'
import { useEffect, useState } from 'react'
import { getAudit, getCost, getHistory } from '@/lib/api'
import type { AuditEntry, CostResponse, HistoryEntry } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoAudit, demoCost, demoRuns } from '@/lib/demo-data'

export default function ActivityPage() {
  const { isDemo } = useDemo()
  const [entries, setEntries] = useState<AuditEntry[]>([])
  const [cost, setCost] = useState<CostResponse | null>(null)
  const [runs, setRuns] = useState<HistoryEntry[]>([])
  const [tab, setTab] = useState<'audit' | 'runs' | 'cost'>('audit')
  const [filter, setFilter] = useState('')

  useEffect(() => {
    if (isDemo) { setEntries(demoAudit() as AuditEntry[]); setCost(demoCost() as CostResponse); setRuns(demoRuns() as HistoryEntry[]); return }
    getAudit(200).then(setEntries).catch(() => {})
    getCost().then(setCost).catch(() => {})
    getHistory().then(r => setRuns(r.runs || [])).catch(() => {})
    const i = setInterval(() => { getAudit(200).then(setEntries).catch(() => {}); getCost().then(setCost).catch(() => {}) }, 5000)
    return () => clearInterval(i)
  }, [isDemo])

  const filtered = filter ? entries.filter(e => e.event_type.toLowerCase().includes(filter.toLowerCase()) || e.agent_name?.toLowerCase().includes(filter.toLowerCase())) : entries
  const failures = entries.filter(e => !e.success).length
  const successRate = entries.length > 0 ? ((entries.length - failures) / entries.length * 100).toFixed(0) : '0'

  // Hourly heatmap
  const hours: Record<string, { ok: number; fail: number }> = {}
  entries.forEach(e => { const h = String(new Date(e.timestamp).getHours()).padStart(2, '0'); if (!hours[h]) hours[h] = { ok: 0, fail: 0 }; if (e.success) hours[h].ok++; else hours[h].fail++ })
  const maxH = Math.max(1, ...Object.values(hours).map(b => b.ok + b.fail))

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Activity</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Audit log, run history, and cost tracking in one place.</p>
        </div>
        <input value={filter} onChange={e => setFilter(e.target.value)} placeholder="Filter..."
          className="w-48 rounded-lg px-3 py-2 text-[12px] outline-none placeholder:opacity-25"
          style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text)' }} />
      </div>

      {/* Top stats */}
      <div className="grid grid-cols-5 gap-2 mb-6">
        <div className="card px-4 py-3"><div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Events</div><div className="text-[17px] font-bold tabular-nums mt-0.5">{entries.length}</div></div>
        <div className="card px-4 py-3"><div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Failures</div><div className="text-[17px] font-bold tabular-nums mt-0.5" style={{ color: failures > 0 ? 'var(--red)' : 'var(--text)' }}>{failures}</div></div>
        <div className="card px-4 py-3"><div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Success</div><div className="text-[17px] font-bold tabular-nums mt-0.5" style={{ color: 'var(--green)' }}>{successRate}%</div></div>
        <div className="card px-4 py-3"><div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Runs</div><div className="text-[17px] font-bold tabular-nums mt-0.5" style={{ color: 'var(--blue)' }}>{runs.length}</div></div>
        <div className="card px-4 py-3"><div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Cost</div><div className="text-[17px] font-bold tabular-nums mt-0.5" style={{ color: 'var(--accent)' }}>${(cost?.total_cost_usd ?? 0).toFixed(2)}</div></div>
      </div>

      {/* Heatmap */}
      <div className="card p-4 mb-6">
        <div className="flex items-center justify-between mb-3">
          <span className="text-[10px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>Activity by Hour</span>
        </div>
        <div className="flex gap-[3px] items-end" style={{ height: 48 }}>
          {Array.from({ length: 24 }, (_, i) => {
            const k = String(i).padStart(2, '0')
            const b = hours[k] || { ok: 0, fail: 0 }
            const total = b.ok + b.fail
            const intensity = total / maxH
            return (
              <div key={i} className="flex-1 flex flex-col items-center gap-0.5">
                <div className="w-full rounded-sm transition-all" style={{
                  height: `${Math.max(3, intensity * 40)}px`,
                  background: total === 0 ? 'var(--border)' : b.fail > 0 ? 'var(--red)' : 'var(--green)',
                  opacity: total === 0 ? 0.2 : 0.3 + intensity * 0.7,
                }} title={`${k}:00 — ${total} events`} />
                {i % 4 === 0 && <span className="text-[8px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{k}</span>}
              </div>
            )
          })}
        </div>
      </div>

      {/* Tabs */}
      <div className="flex gap-1 mb-4">
        {(['audit', 'runs', 'cost'] as const).map(t => (
          <button key={t} onClick={() => setTab(t)} className="px-4 py-2 rounded-lg text-[12px] font-medium capitalize"
            style={{ background: tab === t ? 'var(--accent-light)' : 'transparent', color: tab === t ? 'var(--accent)' : 'var(--text-dim)' }}>
            {t}
          </button>
        ))}
      </div>

      {/* Content */}
      <div className="card overflow-hidden">
        {tab === 'audit' && (
          filtered.length === 0 ? <div className="py-14 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No entries</div> :
          <div className="max-h-[500px] overflow-y-auto">
            {filtered.map(e => (
              <div key={e.id} className="flex items-center gap-3 px-5 py-2.5 hover:bg-[var(--bg)] transition-colors" style={{ borderBottom: '1px solid var(--border-subtle)' }}>
                <span className="w-[6px] h-[6px] rounded-full flex-shrink-0" style={{ background: e.success ? 'var(--green)' : 'var(--red)' }} />
                <span className="text-[11px] tabular-nums w-[60px]" style={{ color: 'var(--text-dim)' }}>{new Date(e.timestamp).toLocaleTimeString()}</span>
                <span className="mono text-[12px] font-medium w-[140px] truncate">{e.event_type}</span>
                <span className="text-[11px] w-[80px]" style={{ color: 'var(--text-dim)' }}>{e.category}</span>
                <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{e.agent_name || `#${e.agent_id}`}</span>
              </div>
            ))}
          </div>
        )}
        {tab === 'runs' && (
          runs.length === 0 ? <div className="py-14 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No runs</div> :
          <div className="max-h-[500px] overflow-y-auto">
            {runs.map((r, i) => (
              <div key={i} className="flex items-center gap-4 px-5 py-3 hover:bg-[var(--bg)] transition-colors" style={{ borderBottom: '1px solid var(--border-subtle)' }}>
                <span className="mono text-[12px] font-medium" style={{ color: 'var(--accent)' }}>{r.name}</span>
                <span className="text-[12px] flex-1 truncate" style={{ color: 'var(--text-dim)' }}>{r.description}</span>
                <span className="text-[11px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{new Date(r.created_at_ms).toLocaleString()}</span>
              </div>
            ))}
          </div>
        )}
        {tab === 'cost' && (
          <div className="p-6">
            <div className="grid grid-cols-3 gap-4 mb-4">
              <div><div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Total Spend</div><div className="text-[24px] font-bold tabular-nums" style={{ color: 'var(--accent)' }}>${(cost?.total_cost_usd ?? 0).toFixed(4)}</div></div>
              <div><div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Budget</div><div className="text-[24px] font-bold tabular-nums" style={{ color: (cost?.max_cost_usd ?? 0) > 0 ? 'var(--yellow)' : 'var(--text-dim)' }}>{(cost?.max_cost_usd ?? 0) > 0 ? `$${cost!.max_cost_usd.toFixed(2)}` : 'None'}</div></div>
              <div><div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Requests</div><div className="text-[24px] font-bold tabular-nums">{cost?.total_requests ?? 0}</div></div>
            </div>
            {(cost?.max_cost_usd ?? 0) > 0 && (
              <div className="h-[6px] rounded-full overflow-hidden" style={{ background: 'var(--bg-raised)' }}>
                <div className="h-full rounded-full" style={{ width: `${Math.min(((cost?.total_cost_usd ?? 0) / cost!.max_cost_usd) * 100, 100)}%`, background: 'var(--accent)' }} />
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  )
}
