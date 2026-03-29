'use client'
import { useEffect, useState, useRef } from 'react'
import { getCost, getHistory } from '@/lib/api'
import type { CostResponse, HistoryEntry } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoCost, demoRuns } from '@/lib/demo-data'

export default function CostPage() {
  const { isDemo } = useDemo()
  const [cost, setCost] = useState<CostResponse | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])
  const [costPts, setCostPts] = useState<Array<{ t: number; v: number }>>([])
  const ref = useRef(0)

  useEffect(() => {
    if (isDemo) {
      setCost(demoCost() as CostResponse); setHistory(demoRuns() as HistoryEntry[])
      // Generate demo chart data
      const pts: Array<{t:number;v:number}> = []; let c = 0
      for (let i = 0; i < 40; i++) { c += 0.05 + Math.random() * 0.15; pts.push({ t: Date.now() - (40 - i) * 60000, v: c }) }
      setCostPts(pts); return
    }
    getCost().then(setCost).catch(() => {}); getHistory().then(r => setHistory(r.runs || [])).catch(() => {})
    const interval = setInterval(() => {
      getCost().then(c => { setCost(c); if (c && c.total_cost_usd !== ref.current) { ref.current = c.total_cost_usd; setCostPts(p => [...p.slice(-59), { t: Date.now(), v: c.total_cost_usd }]) } }).catch(() => {})
    }, 3000)
    return () => clearInterval(interval)
  }, [isDemo])

  const totalCost = cost?.total_cost_usd ?? 0; const maxCost = cost?.max_cost_usd ?? 0
  const pct = maxCost > 0 ? Math.min((totalCost / maxCost) * 100, 100) : 0

  // Chart
  const cMax = Math.max(...costPts.map(d => d.v)) * 1.1 || 1
  const W = 100; const H = 80
  const pts = costPts.map((d, i) => `${(i / Math.max(costPts.length - 1, 1)) * W},${H - (d.v / cMax) * H}`).join(' ')
  const area = costPts.length >= 2 ? `0,${H} ${pts} ${W},${H}` : ''

  return (
    <div>
      <h2 className="text-[24px] font-semibold tracking-[-0.03em] mb-1">Cost</h2>
      <p className="text-[14px] mb-8" style={{ color: 'var(--text-dim)' }}>Every token tracked at the kernel level</p>

      <div className="grid grid-cols-3 gap-4 mb-6">
        <div className="card p-5">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Total Spend</div>
          <div className="text-[28px] font-bold tabular-nums tracking-[-0.03em]" style={{ color: 'var(--accent)' }}>${totalCost.toFixed(4)}</div>
          <div className="text-[12px] mt-1" style={{ color: 'var(--text-dim)' }}>{cost?.total_requests ?? 0} requests</div>
        </div>
        <div className="card p-5">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Budget</div>
          <div className="text-[28px] font-bold tabular-nums tracking-[-0.03em]" style={{ color: maxCost > 0 ? 'var(--yellow)' : 'var(--text-dim)' }}>{maxCost > 0 ? `$${maxCost.toFixed(2)}` : 'None'}</div>
          {maxCost > 0 && <div className="text-[12px] mt-1 tabular-nums" style={{ color: pct > 80 ? 'var(--red)' : 'var(--text-dim)' }}>{pct.toFixed(1)}% used</div>}
        </div>
        <div className="card p-5">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Runs</div>
          <div className="text-[28px] font-bold tabular-nums tracking-[-0.03em]" style={{ color: 'var(--blue)' }}>{history.length}</div>
        </div>
      </div>

      {maxCost > 0 && (
        <div className="card p-5 mb-6">
          <div className="flex justify-between text-[12px] mb-3" style={{ color: 'var(--text-dim)' }}>
            <span>Budget</span><span className="tabular-nums">${totalCost.toFixed(4)} / ${maxCost.toFixed(2)}</span>
          </div>
          <div className="h-[6px] rounded-full overflow-hidden" style={{ background: 'var(--bg-raised)' }}>
            <div className="h-full rounded-full transition-all duration-700" style={{ width: `${pct}%`, background: pct > 80 ? 'var(--red)' : pct > 50 ? 'var(--yellow)' : 'var(--green)' }} />
          </div>
        </div>
      )}

      {/* Cost chart */}
      <div className="card p-5 mb-8">
        <div className="flex items-center justify-between mb-4">
          <span className="text-[11px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>Cost Trend</span>
          {costPts.length > 0 && <span className="text-[12px] tabular-nums font-semibold" style={{ color: 'var(--accent)' }}>${costPts[costPts.length-1]?.v.toFixed(4)}</span>}
        </div>
        {costPts.length < 2 ? (
          <div className="h-[100px] flex items-center justify-center text-[13px]" style={{ color: 'var(--text-dim)' }}>Collecting data...</div>
        ) : (
          <svg viewBox={`0 0 ${W} ${H}`} className="w-full" style={{ height: 120 }} preserveAspectRatio="none">
            <defs><linearGradient id="cg" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stopColor="var(--accent)" stopOpacity="0.15"/><stop offset="100%" stopColor="var(--accent)" stopOpacity="0"/></linearGradient></defs>
            {[0.25,0.5,0.75].map(p => <line key={p} x1="0" y1={H*p} x2={W} y2={H*p} stroke="var(--border)" strokeWidth="0.3" vectorEffect="non-scaling-stroke"/>)}
            <polygon points={area} fill="url(#cg)"/>
            <polyline points={pts} fill="none" stroke="var(--accent)" strokeWidth="1.5" vectorEffect="non-scaling-stroke" strokeLinejoin="round"/>
          </svg>
        )}
      </div>

      <h3 className="text-[16px] font-semibold mb-4">Run History</h3>
      <div className="card overflow-hidden">
        {history.length === 0 ? <div className="py-14 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No runs yet</div> :
        <table className="w-full text-[13px]"><thead><tr style={{ borderBottom: '1px solid var(--border)' }}>
          {['Run','Goal','Time'].map((h,i) => <th key={h} className={`px-5 py-3 text-[11px] font-medium uppercase tracking-[0.06em] ${i===2?'text-right':'text-left'}`} style={{ color:'var(--text-dim)', background:'var(--bg)' }}>{h}</th>)}
        </tr></thead><tbody>{history.map((r,i) => <tr key={i} className="hover:bg-[var(--bg)]" style={{ borderBottom:'1px solid var(--border-subtle)' }}>
          <td className="px-5 py-3 mono text-[12px] font-medium" style={{ color:'var(--accent)' }}>{r.name}</td>
          <td className="px-5 py-3 truncate max-w-md" style={{ color:'var(--text-secondary)' }}>{r.description}</td>
          <td className="px-5 py-3 text-right text-[12px]" style={{ color:'var(--text-dim)' }}>{new Date(r.created_at_ms).toLocaleString()}</td>
        </tr>)}</tbody></table>}
      </div>
    </div>
  )
}
