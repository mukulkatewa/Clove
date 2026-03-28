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
          <h2 style={{ fontSize: 24, fontWeight: 600, color: 'var(--text)' }}>Audit Log</h2>
          <p style={{ fontSize: 14, marginTop: 4, color: 'var(--text-secondary)' }}>
            Every action logged. EU AI Act compliant.
          </p>
        </div>
        <input
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
          placeholder="Filter..."
          className="w-48 text-sm outline-none placeholder:opacity-30"
          style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--text)' }}
        />
      </div>

      <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
        {filtered.length === 0 ? (
          <div style={{ padding: 32, textAlign: 'center', fontSize: 14, color: 'var(--text-dim)' }}>No audit entries.</div>
        ) : (
          <table className="w-full text-sm">
            <thead>
              <tr style={{ background: 'var(--bg)' }}>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Time</th>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Event</th>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Category</th>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Agent</th>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Status</th>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Details</th>
              </tr>
            </thead>
            <tbody>
              {filtered.map((e) => (
                <tr key={e.id} style={{ borderTop: '1px solid var(--border-subtle)' }}>
                  <td className="p-3 mono" style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                    {new Date(e.timestamp).toLocaleTimeString()}
                  </td>
                  <td className="p-3 mono" style={{ fontSize: 12, color: 'var(--accent)' }}>{e.event_type}</td>
                  <td className="p-3" style={{ fontSize: 12, color: 'var(--text-secondary)' }}>{e.category}</td>
                  <td className="p-3" style={{ fontSize: 12, color: 'var(--text-secondary)' }}>{e.agent_name || `#${e.agent_id}`}</td>
                  <td className="p-3">
                    <span
                      style={{
                        fontSize: 12,
                        padding: '2px 6px',
                        borderRadius: 8,
                        fontWeight: 500,
                        background: e.success ? 'var(--green-light)' : 'var(--red-light)',
                        color: e.success ? 'var(--green)' : 'var(--red)',
                      }}
                    >
                      {e.success ? 'OK' : 'FAIL'}
                    </span>
                  </td>
                  <td className="p-3 mono truncate max-w-xs" style={{ fontSize: 12, color: 'var(--text-dim)' }}>
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
