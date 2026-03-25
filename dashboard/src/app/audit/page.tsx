'use client'
import { useEffect, useState } from 'react'
import { getAudit } from '@/lib/api'
import type { AuditEntry } from '@/lib/api'

export default function AuditPage() {
  const [entries, setEntries] = useState<AuditEntry[]>([])
  const [filter, setFilter] = useState('')

  useEffect(() => {
    getAudit(200).then(setEntries).catch(() => {})
    const interval = setInterval(() => getAudit(200).then(setEntries).catch(() => {}), 5000)
    return () => clearInterval(interval)
  }, [])

  const filtered = filter
    ? entries.filter((e) =>
        e.event_type.toLowerCase().includes(filter.toLowerCase()) ||
        e.category.toLowerCase().includes(filter.toLowerCase()) ||
        e.agent_name.toLowerCase().includes(filter.toLowerCase()),
      )
    : entries

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-2xl font-semibold">Audit Log</h2>
          <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>
            Every action logged. EU AI Act compliant.
          </p>
        </div>
        <input
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
          placeholder="Filter..."
          className="w-48 rounded-md px-3 py-2 text-sm outline-none placeholder:opacity-30"
          style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text)' }}
        />
      </div>

      <div className="border rounded-lg overflow-hidden" style={{ borderColor: 'var(--border)' }}>
        {filtered.length === 0 ? (
          <div className="p-8 text-center text-sm" style={{ color: 'var(--text-dim)' }}>No audit entries.</div>
        ) : (
          <table className="w-full text-sm">
            <thead>
              <tr style={{ background: 'var(--bg-card)' }}>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Time</th>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Event</th>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Category</th>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Agent</th>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Status</th>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Details</th>
              </tr>
            </thead>
            <tbody>
              {filtered.map((e) => (
                <tr key={e.id} className="border-t" style={{ borderColor: 'var(--border)' }}>
                  <td className="p-3 text-xs mono" style={{ color: 'var(--text-dim)' }}>
                    {new Date(e.timestamp).toLocaleTimeString()}
                  </td>
                  <td className="p-3 mono text-xs" style={{ color: 'var(--accent)' }}>{e.event_type}</td>
                  <td className="p-3 text-xs">{e.category}</td>
                  <td className="p-3 text-xs">{e.agent_name || `#${e.agent_id}`}</td>
                  <td className="p-3">
                    <span
                      className="text-xs px-1.5 py-0.5 rounded"
                      style={{
                        background: e.success ? 'var(--green)' : 'var(--red)',
                        color: '#000',
                      }}
                    >
                      {e.success ? 'OK' : 'FAIL'}
                    </span>
                  </td>
                  <td className="p-3 text-xs mono truncate max-w-xs" style={{ color: 'var(--text-dim)' }}>
                    {JSON.stringify(e.details).slice(0, 80)}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  )
}
