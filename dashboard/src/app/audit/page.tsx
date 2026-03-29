'use client'
import { useEffect, useState } from 'react'
import { getAudit } from '@/lib/api'
import type { AuditEntry } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoAudit } from '@/lib/demo-data'

export default function AuditPage() {
  const { isDemo } = useDemo()
  const [entries, setEntries] = useState<AuditEntry[]>([])
  const [filter, setFilter] = useState('')

  useEffect(() => {
    if (isDemo) { setEntries(demoAudit() as AuditEntry[]); return }
    getAudit(200).then(setEntries).catch(() => {})
    const interval = setInterval(() => getAudit(200).then(setEntries).catch(() => {}), 5000)
    return () => clearInterval(interval)
  }, [isDemo])

  const filtered = filter ? entries.filter(e => e.event_type.toLowerCase().includes(filter.toLowerCase()) || e.category.toLowerCase().includes(filter.toLowerCase()) || e.agent_name.toLowerCase().includes(filter.toLowerCase())) : entries

  // Category counts
  const cats: Record<string, { total: number; fail: number }> = {}
  entries.forEach(e => { if (!cats[e.category]) cats[e.category] = { total: 0, fail: 0 }; cats[e.category].total++; if (!e.success) cats[e.category].fail++ })

  // Hourly heatmap
  const hours: Record<string, { ok: number; fail: number }> = {}
  entries.forEach(e => { const h = String(new Date(e.timestamp).getHours()).padStart(2, '0'); if (!hours[h]) hours[h] = { ok: 0, fail: 0 }; if (e.success) hours[h].ok++; else hours[h].fail++ })
  const maxH = Math.max(1, ...Object.values(hours).map(b => b.ok + b.fail))

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[24px] font-semibold tracking-[-0.03em]">Audit</h2>
          <p className="text-[14px] mt-1" style={{ color: 'var(--text-dim)' }}>Every action logged. {entries.length} entries.</p>
        </div>
        <input value={filter} onChange={e => setFilter(e.target.value)} placeholder="Filter..."
          className="w-48 rounded-xl px-3 py-2 text-[13px] outline-none placeholder:opacity-25"
          style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text)' }} />
      </div>

      {/* Stats row */}
      <div className="grid grid-cols-2 gap-4 mb-6">
        <div className="card p-5">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Categories</div>
          <div className="space-y-2">
            {Object.entries(cats).sort((a,b) => b[1].total - a[1].total).map(([cat, { total, fail }]) => (
              <div key={cat} className="flex items-center justify-between">
                <span className="text-[12px]" style={{ color: 'var(--text-secondary)' }}>{cat}</span>
                <div className="flex items-center gap-2">
                  <div className="w-[60px] h-[4px] rounded-full overflow-hidden" style={{ background: 'var(--bg-raised)' }}>
                    <div className="h-full rounded-full" style={{ width: `${(total / entries.length) * 100}%`, background: 'var(--accent)' }} />
                  </div>
                  <span className="text-[11px] tabular-nums w-6 text-right font-medium">{total}</span>
                  {fail > 0 && <span className="text-[10px] font-semibold px-1.5 py-0.5 rounded-full" style={{ background: 'var(--red-light)', color: 'var(--red)' }}>{fail}</span>}
                </div>
              </div>
            ))}
          </div>
        </div>

        <div className="card p-5">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Activity by Hour</div>
          <div className="flex gap-[3px] items-end" style={{ height: 60 }}>
            {Array.from({ length: 24 }, (_, i) => {
              const k = String(i).padStart(2, '0')
              const b = hours[k] || { ok: 0, fail: 0 }
              const total = b.ok + b.fail
              const intensity = total / maxH
              const hasFail = b.fail > 0
              return (
                <div key={i} className="flex-1 flex flex-col items-center gap-1">
                  <div className="w-full rounded-sm transition-all" style={{
                    height: `${Math.max(4, intensity * 50)}px`,
                    background: total === 0 ? 'var(--border)' : hasFail ? 'var(--red)' : 'var(--green)',
                    opacity: total === 0 ? 0.3 : 0.3 + intensity * 0.7,
                  }} title={`${k}:00 — ${total} events`} />
                  {i % 6 === 0 && <span className="text-[8px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{k}</span>}
                </div>
              )
            })}
          </div>
        </div>
      </div>

      {/* Table */}
      <div className="card overflow-hidden">
        {filtered.length === 0 ? <div className="py-14 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No entries</div> :
        <table className="w-full text-[13px]"><thead><tr style={{ borderBottom: '1px solid var(--border)' }}>
          {['Time','Event','Category','Agent','Status','Details'].map(h => <th key={h} className="px-5 py-3 text-left text-[11px] font-medium uppercase tracking-[0.06em]" style={{ color:'var(--text-dim)', background:'var(--bg)' }}>{h}</th>)}
        </tr></thead><tbody>
          {filtered.map(e => <tr key={e.id} className="hover:bg-[var(--bg)]" style={{ borderBottom:'1px solid var(--border-subtle)' }}>
            <td className="px-5 py-3 text-[12px] tabular-nums" style={{ color:'var(--text-dim)' }}>{new Date(e.timestamp).toLocaleTimeString()}</td>
            <td className="px-5 py-3 mono text-[12px] font-medium">{e.event_type}</td>
            <td className="px-5 py-3" style={{ color:'var(--text-secondary)' }}>{e.category}</td>
            <td className="px-5 py-3" style={{ color:'var(--text-secondary)' }}>{e.agent_name || `#${e.agent_id}`}</td>
            <td className="px-5 py-3"><span className="text-[10px] font-semibold px-2 py-[3px] rounded-full" style={{ background: e.success ? 'var(--green-light)' : 'var(--red-light)', color: e.success ? 'var(--green)' : 'var(--red)' }}>{e.success ? 'OK' : 'FAIL'}</span></td>
            <td className="px-5 py-3 mono text-[12px] truncate max-w-[200px]" style={{ color:'var(--text-dim)' }}>{JSON.stringify(e.details).slice(0, 60)}</td>
          </tr>)}
        </tbody></table>}
      </div>
    </div>
  )
}
