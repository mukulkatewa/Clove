'use client'
import { useEffect, useState } from 'react'
import { getCost, getHistory } from '@/lib/api'
import type { CostResponse, HistoryEntry } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoCost, demoRuns } from '@/lib/demo-data'

export default function CostPage() {
  const { isDemo } = useDemo()
  const [cost, setCost] = useState<CostResponse | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])

  useEffect(() => {
    if (isDemo) { setCost(demoCost() as any); setHistory(demoRuns() as any); return }
    getCost().then(setCost).catch(() => {})
    getHistory().then((r) => setHistory(r.runs || [])).catch(() => {})
    const interval = setInterval(() => {
      getCost().then(setCost).catch(() => {})
    }, 3000)
    return () => clearInterval(interval)
  }, [isDemo])

  const totalCost = cost?.total_cost_usd ?? 0
  const maxCost = cost?.max_cost_usd ?? 0
  const pct = maxCost > 0 ? Math.min((totalCost / maxCost) * 100, 100) : 0

  return (
    <div>
      <h2 style={{ fontSize: 24, fontWeight: 600, marginBottom: 4, color: 'var(--text)' }}>Cost</h2>
      <p style={{ fontSize: 14, color: 'var(--text-secondary)', marginBottom: 32 }}>
        LLM spend tracked at the kernel level. Every token accounted for.
      </p>

      {/* Cost summary */}
      <div className="grid grid-cols-3 gap-4 mb-8">
        <div className="card" style={{ padding: 20 }}>
          <div style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 8 }}>Total Spend</div>
          <div className="mono" style={{ fontSize: 30, fontWeight: 600, color: 'var(--accent)' }}>
            ${totalCost.toFixed(4)}
          </div>
        </div>
        <div className="card" style={{ padding: 20 }}>
          <div style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 8 }}>Budget</div>
          <div className="mono" style={{ fontSize: 30, fontWeight: 600, color: maxCost > 0 ? 'var(--yellow)' : 'var(--text-dim)' }}>
            {maxCost > 0 ? `$${maxCost.toFixed(2)}` : 'None'}
          </div>
        </div>
        <div className="card" style={{ padding: 20 }}>
          <div style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 8 }}>Total Runs</div>
          <div className="mono" style={{ fontSize: 30, fontWeight: 600, color: 'var(--blue)' }}>
            {history.length}
          </div>
        </div>
      </div>

      {/* Budget bar */}
      {maxCost > 0 && (
        <div className="mb-8">
          <div className="flex justify-between" style={{ fontSize: 12, marginBottom: 8, color: 'var(--text-dim)' }}>
            <span>Budget usage</span>
            <span>{pct.toFixed(1)}%</span>
          </div>
          <div className="overflow-hidden" style={{ height: 12, borderRadius: 9999, background: 'var(--border)' }}>
            <div
              className="transition-all duration-500"
              style={{
                height: '100%',
                borderRadius: 9999,
                width: `${pct}%`,
                background: pct > 80 ? 'var(--red)' : pct > 50 ? 'var(--yellow)' : 'var(--green)',
              }}
            />
          </div>
        </div>
      )}

      {/* Cost per run */}
      <h3 style={{ fontSize: 16, fontWeight: 600, marginBottom: 12, color: 'var(--text)' }}>
        Cost Per Run
      </h3>
      <p style={{ fontSize: 12, color: 'var(--text-dim)', marginBottom: 16 }}>
        Detailed per-run cost breakdown coming soon. Current data shows run history.
      </p>
      <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
        {history.length === 0 ? (
          <div style={{ padding: 32, textAlign: 'center', fontSize: 14, color: 'var(--text-dim)' }}>No runs yet.</div>
        ) : (
          <table className="w-full text-sm">
            <thead>
              <tr style={{ background: 'var(--bg)' }}>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Run</th>
                <th className="text-left p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Goal</th>
                <th className="text-right p-3" style={{ fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>Time</th>
              </tr>
            </thead>
            <tbody>
              {history.map((run, i) => (
                <tr key={i} style={{ borderTop: '1px solid var(--border-subtle)' }}>
                  <td className="p-3 mono" style={{ fontSize: 12, color: 'var(--accent)' }}>{run.name}</td>
                  <td className="p-3 truncate max-w-md" style={{ color: 'var(--text-secondary)' }}>{run.description}</td>
                  <td className="p-3 text-right" style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                    {new Date(run.created_at_ms).toLocaleString()}
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
