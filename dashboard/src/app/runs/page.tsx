'use client'
import { useEffect, useState, useRef } from 'react'
import { submitRun, getHistory } from '@/lib/api'
import type { RunResponse, HistoryEntry } from '@/lib/api'

const ALL_TOOLS = ['read_file', 'write_file', 'exec', 'http', 'search', 'store', 'fetch', 'remember', 'recall']

export default function RunsPage() {
  const [goal, setGoal] = useState(''); const [budget, setBudget] = useState(0.5); const [model, setModel] = useState('')
  const [tools, setTools] = useState<string[]>([...ALL_TOOLS]); const [loading, setLoading] = useState(false)
  const [result, setResult] = useState<RunResponse | null>(null); const [history, setHistory] = useState<HistoryEntry[]>([])
  const [elapsed, setElapsed] = useState(0); const timerRef = useRef<NodeJS.Timeout | null>(null)

  useEffect(() => { getHistory().then((r) => setHistory(r.runs || [])).catch(() => {}) }, [result])

  const handleSubmit = async () => {
    if (!goal.trim() || loading) return; setLoading(true); setResult(null); setElapsed(0)
    const start = Date.now(); timerRef.current = setInterval(() => setElapsed(Date.now() - start), 100)
    try { const res = await submitRun({ goal: goal.trim(), budget, model: model || undefined, tools: tools.length < ALL_TOOLS.length ? tools : undefined }); setResult(res) }
    catch (e) { setResult({ success: false, content: `Error: ${e}`, total_cost_usd: 0, total_tokens: 0, steps: 0, chain_id: '', step_log: [] }) }
    finally { setLoading(false); if (timerRef.current) clearInterval(timerRef.current) }
  }

  return (
    <div>
      <h2 className="text-[24px] font-semibold tracking-[-0.03em] mb-1">Runs</h2>
      <p className="text-[14px] mb-8" style={{ color: 'var(--text-dim)' }}>Submit a goal. The kernel assigns an agent, calls tools, returns a result.</p>

      <div className="card p-6 mb-6">
        <textarea value={goal} onChange={(e) => setGoal(e.target.value)} placeholder="What should the agent do?" rows={3}
          className="w-full rounded-xl p-4 text-[14px] resize-none outline-none placeholder:opacity-30"
          style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
          onKeyDown={(e) => { if (e.key === 'Enter' && e.metaKey) handleSubmit() }} />
        <div className="flex items-end gap-4 mt-4">
          <div>
            <label className="block text-[11px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Budget</label>
            <input type="number" step="0.1" min="0.01" value={budget} onChange={(e) => setBudget(parseFloat(e.target.value) || 0.5)}
              className="w-28 rounded-lg px-3 py-2.5 text-[13px] tabular-nums outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
          <div>
            <label className="block text-[11px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Model</label>
            <input type="text" value={model} onChange={(e) => setModel(e.target.value)} placeholder="kernel default"
              className="w-56 rounded-lg px-3 py-2.5 text-[13px] outline-none placeholder:opacity-25" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
          <div className="flex-1" />
          <button onClick={handleSubmit} disabled={loading || !goal.trim()}
            className="px-7 py-2.5 rounded-xl text-[13px] font-semibold text-white transition-all disabled:opacity-30"
            style={{ background: loading ? 'var(--text-dim)' : 'var(--accent)' }}>
            {loading ? `Running ${(elapsed / 1000).toFixed(1)}s` : 'Run Agent'}
          </button>
        </div>
        <div className="mt-4 flex flex-wrap gap-1.5">
          {ALL_TOOLS.map((tool) => {
            const active = tools.includes(tool)
            return <button key={tool} onClick={() => setTools(p => active ? p.filter(t=>t!==tool) : [...p,tool])}
              className="px-2.5 py-1 rounded-lg text-[11px] font-medium transition-all"
              style={{ background: active ? 'var(--accent-light)' : 'var(--bg)', border: `1px solid ${active ? 'var(--accent)' : 'var(--border)'}`, color: active ? 'var(--accent)' : 'var(--text-dim)' }}>
              {tool}
            </button>
          })}
        </div>
      </div>

      {result && (
        <div className="card p-6 mb-8" style={{ borderLeft: `4px solid ${result.success ? 'var(--green)' : 'var(--red)'}` }}>
          <div className="flex items-center justify-between mb-3">
            <span className="text-[11px] font-semibold px-2.5 py-1 rounded-full" style={{ background: result.success ? 'var(--green-light)' : 'var(--red-light)', color: result.success ? 'var(--green)' : 'var(--red)' }}>
              {result.success ? 'Success' : 'Failed'}
            </span>
            <div className="flex gap-4 text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>
              <span>${result.total_cost_usd.toFixed(6)}</span><span>{result.total_tokens.toLocaleString()} tok</span><span>{result.steps} steps</span><span>{(elapsed / 1000).toFixed(1)}s</span>
            </div>
          </div>
          <div className="whitespace-pre-wrap text-[14px] leading-relaxed">{result.content}</div>
          {result.step_log.length > 0 && (
            <div className="mt-4 pt-4" style={{ borderTop: '1px solid var(--border)' }}>
              <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Tool Calls</div>
              {result.step_log.map((s, i) => (
                <div key={i} className="flex gap-2 text-[12px] mono py-0.5"><span style={{ color: 'var(--text-secondary)' }}>{s.step}.</span><span style={{ color: 'var(--accent)' }}>{s.tool}</span><span className="truncate" style={{ color: 'var(--text-dim)' }}>{typeof s.args === 'string' ? s.args : JSON.stringify(s.args)}</span></div>
              ))}
            </div>
          )}
        </div>
      )}

      <h3 className="text-[16px] font-semibold mb-4">History</h3>
      <div className="card overflow-hidden">
        {history.length === 0 ? <div className="py-14 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No runs yet</div> :
        <table className="w-full text-[13px]"><thead><tr style={{ borderBottom: '1px solid var(--border)' }}>
          {['Name','Goal','Artifacts','Time'].map((h,i)=><th key={h} className={`px-5 py-3 text-[11px] font-medium uppercase tracking-[0.06em] ${i>1?'text-right':'text-left'}`} style={{color:'var(--text-dim)',background:'var(--bg)'}}>{h}</th>)}
        </tr></thead><tbody>{history.map((r,i)=><tr key={i} className="hover:bg-[var(--bg)]" style={{borderBottom:'1px solid var(--border-subtle)'}}>
          <td className="px-5 py-3 mono text-[12px] font-medium" style={{color:'var(--accent)'}}>{r.name}</td>
          <td className="px-5 py-3 truncate max-w-md" style={{color:'var(--text-secondary)'}}>{r.description}</td>
          <td className="px-5 py-3 text-right tabular-nums text-[12px]" style={{color:'var(--text-dim)'}}>{r.artifact_count}</td>
          <td className="px-5 py-3 text-right text-[12px]" style={{color:'var(--text-dim)'}}>{new Date(r.created_at_ms).toLocaleString()}</td>
        </tr>)}</tbody></table>}
      </div>
    </div>
  )
}
