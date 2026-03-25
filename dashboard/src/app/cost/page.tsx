'use client'
import { useEffect, useState } from 'react'
import { getCost, getHistory } from '@/lib/api'
import type { CostResponse, HistoryEntry } from '@/lib/api'

export default function CostPage() {
  const [cost, setCost] = useState<CostResponse | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])

  useEffect(() => {
    getCost().then(setCost).catch(() => {})
    getHistory().then((r) => setHistory(r.runs || [])).catch(() => {})
    const interval = setInterval(() => {
      getCost().then(setCost).catch(() => {})
    }, 3000)
    return () => clearInterval(interval)
  }, [])

  const totalCost = cost?.total_cost_usd ?? 0
  const maxCost = cost?.max_cost_usd ?? 0
  const pct = maxCost > 0 ? Math.min((totalCost / maxCost) * 100, 100) : 0

  return (
    <div>
      <h2 className="text-2xl font-semibold mb-1">Cost</h2>
      <p className="text-sm mb-8" style={{ color: 'var(--text-dim)' }}>
        LLM spend tracked at the kernel level. Every token accounted for.
      </p>

      {/* Cost summary */}
      <div className="grid grid-cols-3 gap-4 mb-8">
        <div className="border rounded-lg p-5" style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}>
          <div className="text-xs mb-2" style={{ color: 'var(--text-dim)' }}>Total Spend</div>
          <div className="text-3xl font-semibold mono" style={{ color: 'var(--accent)' }}>
            ${totalCost.toFixed(4)}
          </div>
        </div>
        <div className="border rounded-lg p-5" style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}>
          <div className="text-xs mb-2" style={{ color: 'var(--text-dim)' }}>Budget</div>
          <div className="text-3xl font-semibold mono" style={{ color: maxCost > 0 ? 'var(--yellow)' : 'var(--text-dim)' }}>
            {maxCost > 0 ? `$${maxCost.toFixed(2)}` : 'None'}
          </div>
        </div>
        <div className="border rounded-lg p-5" style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}>
          <div className="text-xs mb-2" style={{ color: 'var(--text-dim)' }}>Total Runs</div>
          <div className="text-3xl font-semibold mono" style={{ color: 'var(--blue)' }}>
            {history.length}
          </div>
        </div>
      </div>

      {/* Budget bar */}
      {maxCost > 0 && (
        <div className="mb-8">
          <div className="flex justify-between text-xs mb-2" style={{ color: 'var(--text-dim)' }}>
            <span>Budget usage</span>
            <span>{pct.toFixed(1)}%</span>
          </div>
          <div className="h-3 rounded-full overflow-hidden" style={{ background: 'var(--border)' }}>
            <div
              className="h-full rounded-full transition-all duration-500"
              style={{
                width: `${pct}%`,
                background: pct > 80 ? 'var(--red)' : pct > 50 ? 'var(--yellow)' : 'var(--green)',
              }}
            />
          </div>
        </div>
      )}

      {/* Cost per run */}
      <h3 className="text-sm font-semibold mb-3" style={{ color: 'var(--text-dim)' }}>
        COST PER RUN
      </h3>
      <p className="text-xs mb-4" style={{ color: 'var(--text-dim)' }}>
        Detailed per-run cost breakdown coming soon. Current data shows run history.
      </p>
      <div className="border rounded-lg overflow-hidden" style={{ borderColor: 'var(--border)' }}>
        {history.length === 0 ? (
          <div className="p-8 text-center text-sm" style={{ color: 'var(--text-dim)' }}>No runs yet.</div>
        ) : (
          <table className="w-full text-sm">
            <thead>
              <tr style={{ background: 'var(--bg-card)' }}>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Run</th>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Goal</th>
                <th className="text-right p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Time</th>
              </tr>
            </thead>
            <tbody>
              {history.map((run, i) => (
                <tr key={i} className="border-t" style={{ borderColor: 'var(--border)' }}>
                  <td className="p-3 mono text-xs" style={{ color: 'var(--accent)' }}>{run.name}</td>
                  <td className="p-3 truncate max-w-md">{run.description}</td>
                  <td className="p-3 text-right text-xs" style={{ color: 'var(--text-dim)' }}>
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
