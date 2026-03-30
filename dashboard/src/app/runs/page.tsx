'use client'
import { useEffect, useState, useRef } from 'react'
import { submitRun, getHistory } from '@/lib/api'
import type { RunResponse, HistoryEntry } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoRuns } from '@/lib/demo-data'

const ALL_TOOLS = ['read_file', 'write_file', 'exec', 'http', 'search', 'store', 'fetch', 'remember', 'recall']

const SUGGESTIONS = [
  'Research the current state of AI agent frameworks in 2026',
  'Read all TypeScript files in ./src and find potential bugs',
  'Check if https://api.example.com is responding and measure latency',
  'Analyze package.json dependencies for security issues',
  'Write a Python script that fetches weather data for New York',
]

export default function RunsPage() {
  const { isDemo } = useDemo()
  const [goal, setGoal] = useState('')
  const [budget, setBudget] = useState(0.5)
  const [model, setModel] = useState('')
  const [tools, setTools] = useState<string[]>([...ALL_TOOLS])
  const [showAdvanced, setShowAdvanced] = useState(false)
  const [loading, setLoading] = useState(false)
  const [result, setResult] = useState<RunResponse | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])
  const [elapsed, setElapsed] = useState(0)
  const timerRef = useRef<NodeJS.Timeout | null>(null)
  const resultRef = useRef<HTMLDivElement>(null)

  useEffect(() => { if (isDemo) { setHistory(demoRuns() as any); return }; getHistory().then((r) => setHistory(r.runs || [])).catch(() => {}) }, [result, isDemo])

  const handleSubmit = async () => {
    if (!goal.trim() || loading) return; setLoading(true); setResult(null); setElapsed(0)
    const start = Date.now(); timerRef.current = setInterval(() => setElapsed(Date.now() - start), 100)
    try {
      const res = await submitRun({ goal: goal.trim(), budget, model: model || undefined, tools: tools.length < ALL_TOOLS.length ? tools : undefined })
      setResult(res)
      setTimeout(() => resultRef.current?.scrollIntoView({ behavior: 'smooth' }), 100)
    }
    catch (e) { setResult({ success: false, content: `Error: ${e}`, total_cost_usd: 0, total_tokens: 0, steps: 0, chain_id: '', step_log: [] }) }
    finally { setLoading(false); if (timerRef.current) clearInterval(timerRef.current) }
  }

  return (
    <div className="max-w-[900px]">
      <h2 className="text-[24px] font-semibold tracking-[-0.03em] mb-1">Run Agent</h2>
      <p className="text-[14px] mb-6" style={{ color: 'var(--text-dim)' }}>
        Describe a task. The kernel assigns an agent with real tools — shell, filesystem, HTTP, memory.
      </p>

      {/* Input area */}
      <div className="card p-0 mb-6 overflow-hidden">
        <textarea
          value={goal}
          onChange={(e) => setGoal(e.target.value)}
          placeholder="What should the agent do?"
          rows={4}
          className="w-full p-5 text-[15px] resize-none outline-none placeholder:opacity-30 leading-relaxed"
          style={{ background: 'var(--bg-card)', color: 'var(--text)', border: 'none' }}
          onKeyDown={(e) => { if (e.key === 'Enter' && e.metaKey) handleSubmit() }}
        />

        {/* Footer bar */}
        <div className="flex items-center justify-between px-5 py-3" style={{ background: 'var(--bg)', borderTop: '1px solid var(--border)' }}>
          <div className="flex items-center gap-3">
            <button onClick={() => setShowAdvanced(!showAdvanced)} className="text-[12px] font-medium flex items-center gap-1.5 transition-colors" style={{ color: 'var(--text-dim)' }}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ transform: showAdvanced ? 'rotate(90deg)' : '', transition: '150ms' }}><polyline points="9 18 15 12 9 6"/></svg>
              Options
            </button>
            <span className="text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>
              ${budget.toFixed(2)} budget
            </span>
            <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>·</span>
            <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
              {tools.length}/{ALL_TOOLS.length} tools
            </span>
          </div>
          <div className="flex items-center gap-2">
            {loading && (
              <span className="text-[12px] tabular-nums font-medium" style={{ color: 'var(--accent)' }}>
                {(elapsed / 1000).toFixed(1)}s
              </span>
            )}
            <button onClick={handleSubmit} disabled={loading || !goal.trim()}
              className="px-5 py-2 rounded-lg text-[13px] font-semibold text-white transition-all disabled:opacity-30"
              style={{ background: loading ? 'var(--text-dim)' : 'var(--accent)' }}>
              {loading ? (
                <span className="flex items-center gap-2">
                  <svg width="14" height="14" viewBox="0 0 24 24" className="animate-spin"><circle cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="3" fill="none" strokeDasharray="30 70" strokeLinecap="round"/></svg>
                  Running
                </span>
              ) : (
                <span className="flex items-center gap-2">
                  Run
                  <kbd className="text-[9px] opacity-60 bg-white/10 px-1 rounded">⌘↵</kbd>
                </span>
              )}
            </button>
          </div>
        </div>

        {/* Advanced options */}
        {showAdvanced && (
          <div className="px-5 py-4" style={{ borderTop: '1px solid var(--border)' }}>
            <div className="flex gap-4 mb-4">
              <div>
                <label className="block text-[11px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Budget (USD)</label>
                <input type="number" step="0.1" min="0.01" value={budget} onChange={(e) => setBudget(parseFloat(e.target.value) || 0.5)}
                  className="w-28 rounded-lg px-3 py-2 text-[13px] tabular-nums outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
              </div>
              <div>
                <label className="block text-[11px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Model</label>
                <input type="text" value={model} onChange={(e) => setModel(e.target.value)} placeholder="kernel default"
                  className="w-64 rounded-lg px-3 py-2 text-[13px] outline-none placeholder:opacity-25" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
              </div>
            </div>
            <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Tools</div>
            <div className="flex flex-wrap gap-1.5">
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
        )}
      </div>

      {/* Suggestions (when empty) */}
      {!goal && !result && !loading && (
        <div className="mb-8">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Try something</div>
          <div className="flex flex-wrap gap-2">
            {SUGGESTIONS.map((s, i) => (
              <button key={i} onClick={() => setGoal(s)}
                className="px-3.5 py-2 rounded-xl text-[12px] text-left transition-all hover:shadow-sm"
                style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
                {s.length > 60 ? s.slice(0, 57) + '...' : s}
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Loading state */}
      {loading && (
        <div className="card p-6 mb-6">
          <div className="flex items-center gap-3 mb-4">
            <div className="w-2 h-2 rounded-full animate-pulse" style={{ background: 'var(--accent)' }} />
            <span className="text-[14px] font-medium">Agent working...</span>
            <span className="text-[12px] tabular-nums ml-auto" style={{ color: 'var(--text-dim)' }}>{(elapsed / 1000).toFixed(1)}s</span>
          </div>
          <div className="space-y-2">
            {[0,1,2].map(i => (
              <div key={i} className="h-3 rounded-full animate-pulse" style={{ background: 'var(--bg)', width: `${80 - i * 20}%`, animationDelay: `${i * 200}ms` }} />
            ))}
          </div>
        </div>
      )}

      {/* Result */}
      {result && (
        <div ref={resultRef} className="mb-8">
          {/* Result header */}
          <div className="card overflow-hidden">
            <div className="flex items-center justify-between px-5 py-3" style={{ background: result.success ? 'var(--green-light)' : 'var(--red-light)' }}>
              <div className="flex items-center gap-2">
                <span className="w-2 h-2 rounded-full" style={{ background: result.success ? 'var(--green)' : 'var(--red)' }} />
                <span className="text-[13px] font-semibold" style={{ color: result.success ? 'var(--green)' : 'var(--red)' }}>
                  {result.success ? 'Completed' : 'Failed'}
                </span>
              </div>
              <div className="flex gap-4 text-[12px] tabular-nums font-medium" style={{ color: result.success ? 'var(--green)' : 'var(--red)' }}>
                <span>${result.total_cost_usd.toFixed(4)}</span>
                <span>{result.total_tokens.toLocaleString()} tokens</span>
                <span>{result.steps} steps</span>
                <span>{(elapsed / 1000).toFixed(1)}s</span>
              </div>
            </div>

            {/* Content */}
            <div className="p-5">
              <div className="whitespace-pre-wrap text-[14px] leading-[1.7]">{result.content}</div>
            </div>

            {/* Tool calls timeline */}
            {result.step_log.length > 0 && (
              <div className="px-5 pb-5">
                <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Execution Timeline</div>
                <div className="space-y-0">
                  {result.step_log.map((s, i) => (
                    <div key={i} className="flex items-start gap-3 relative">
                      {/* Timeline line */}
                      <div className="flex flex-col items-center">
                        <div className="w-[22px] h-[22px] rounded-full flex items-center justify-center text-[10px] font-bold flex-shrink-0"
                          style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>
                          {s.step}
                        </div>
                        {i < result.step_log.length - 1 && <div className="w-px flex-1 min-h-[16px]" style={{ background: 'var(--border)' }} />}
                      </div>
                      <div className="pb-3 min-w-0">
                        <div className="flex items-center gap-2">
                          <span className="text-[13px] font-semibold" style={{ color: 'var(--accent)' }}>{s.tool}</span>
                        </div>
                        <div className="text-[12px] mono truncate mt-0.5" style={{ color: 'var(--text-dim)' }}>
                          {typeof s.args === 'string' ? s.args : JSON.stringify(s.args)}
                        </div>
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </div>
        </div>
      )}

      {/* History */}
      {history.length > 0 && (
        <div>
          <h3 className="text-[16px] font-semibold mb-4">Recent Runs</h3>
          <div className="grid grid-cols-2 gap-3">
            {history.slice(0, 6).map((r, i) => (
              <div key={i} className="card p-4 hover:shadow-md transition-shadow cursor-default">
                <div className="flex items-center justify-between mb-2">
                  <span className="mono text-[12px] font-medium" style={{ color: 'var(--accent)' }}>{r.name}</span>
                  <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{new Date(r.created_at_ms).toLocaleTimeString()}</span>
                </div>
                <p className="text-[13px] line-clamp-2" style={{ color: 'var(--text-secondary)' }}>{r.description}</p>
                <div className="flex items-center gap-2 mt-2">
                  {r.artifact_count > 0 && <span className="text-[10px] font-medium px-1.5 py-0.5 rounded-full" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{r.artifact_count} artifacts</span>}
                </div>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}
