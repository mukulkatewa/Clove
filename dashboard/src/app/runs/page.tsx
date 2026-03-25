'use client'
import { useEffect, useState, useRef } from 'react'
import { submitRun, getHistory } from '@/lib/api'
import type { RunResponse, HistoryEntry } from '@/lib/api'

const ALL_TOOLS = [
  'read_file', 'write_file', 'exec', 'http', 'search',
  'store', 'fetch', 'remember', 'recall',
]

export default function RunsPage() {
  const [goal, setGoal] = useState('')
  const [budget, setBudget] = useState(0.5)
  const [model, setModel] = useState('')
  const [tools, setTools] = useState<string[]>([...ALL_TOOLS])
  const [loading, setLoading] = useState(false)
  const [result, setResult] = useState<RunResponse | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])
  const [elapsed, setElapsed] = useState(0)
  const timerRef = useRef<NodeJS.Timeout | null>(null)

  useEffect(() => {
    getHistory().then((r) => setHistory(r.runs || [])).catch(() => {})
  }, [result])

  const toggleTool = (tool: string) => {
    setTools((prev) =>
      prev.includes(tool) ? prev.filter((t) => t !== tool) : [...prev, tool],
    )
  }

  const handleSubmit = async () => {
    if (!goal.trim() || loading) return
    setLoading(true)
    setResult(null)
    setElapsed(0)

    const start = Date.now()
    timerRef.current = setInterval(() => setElapsed(Date.now() - start), 100)

    try {
      const res = await submitRun({
        goal: goal.trim(),
        budget,
        model: model || undefined,
        tools: tools.length < ALL_TOOLS.length ? tools : undefined,
      })
      setResult(res)
    } catch (e) {
      setResult({
        success: false,
        content: `Error: ${e}`,
        total_cost_usd: 0,
        total_tokens: 0,
        steps: 0,
        chain_id: '',
        step_log: [],
      })
    } finally {
      setLoading(false)
      if (timerRef.current) clearInterval(timerRef.current)
    }
  }

  return (
    <div>
      <h2 className="text-2xl font-semibold mb-1">Runs</h2>
      <p className="text-sm mb-8" style={{ color: 'var(--text-dim)' }}>
        Submit a goal. The kernel assigns an agent, calls tools, returns a result.
      </p>

      {/* Submit form */}
      <div
        className="border rounded-lg p-5 mb-6"
        style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}
      >
        <textarea
          value={goal}
          onChange={(e) => setGoal(e.target.value)}
          placeholder="What should the agent do?"
          rows={3}
          className="w-full rounded-md p-3 text-sm resize-none outline-none placeholder:opacity-30"
          style={{
            background: 'var(--bg)',
            border: '1px solid var(--border)',
            color: 'var(--text)',
          }}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && e.metaKey) handleSubmit()
          }}
        />

        <div className="flex items-end gap-4 mt-4">
          <div>
            <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>
              Budget (USD)
            </label>
            <input
              type="number"
              step="0.1"
              min="0.01"
              value={budget}
              onChange={(e) => setBudget(parseFloat(e.target.value) || 0.5)}
              className="w-28 rounded-md px-3 py-2 text-sm mono outline-none"
              style={{
                background: 'var(--bg)',
                border: '1px solid var(--border)',
                color: 'var(--accent)',
              }}
            />
          </div>
          <div>
            <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>
              Model
            </label>
            <input
              type="text"
              value={model}
              onChange={(e) => setModel(e.target.value)}
              placeholder="kernel default"
              className="w-56 rounded-md px-3 py-2 text-sm mono outline-none placeholder:opacity-20"
              style={{
                background: 'var(--bg)',
                border: '1px solid var(--border)',
                color: 'var(--text)',
              }}
            />
          </div>
          <div className="flex-1" />
          <button
            onClick={handleSubmit}
            disabled={loading || !goal.trim()}
            className="px-6 py-2 rounded-md text-sm font-semibold tracking-wide transition-all disabled:opacity-30"
            style={{
              background: loading ? 'var(--border)' : 'var(--accent)',
              color: '#000',
            }}
          >
            {loading ? `Running... ${(elapsed / 1000).toFixed(1)}s` : 'Run Agent'}
          </button>
        </div>

        {/* Tools */}
        <div className="mt-4 flex flex-wrap gap-2">
          {ALL_TOOLS.map((tool) => {
            const active = tools.includes(tool)
            return (
              <button
                key={tool}
                onClick={() => toggleTool(tool)}
                className="px-2.5 py-1 rounded text-xs mono transition-all"
                style={{
                  background: active ? 'var(--accent-dim)' : 'transparent',
                  border: `1px solid ${active ? 'var(--accent)' : 'var(--border)'}`,
                  color: active ? 'var(--accent)' : 'var(--text-dim)',
                  opacity: active ? 1 : 0.5,
                }}
              >
                {tool}
              </button>
            )
          })}
        </div>
      </div>

      {/* Result */}
      {result && (
        <div
          className="border rounded-lg p-5 mb-8"
          style={{
            borderColor: result.success ? 'var(--green)' : 'var(--red)',
            borderLeftWidth: 3,
            background: 'var(--bg-card)',
          }}
        >
          <div className="flex items-center justify-between mb-3">
            <span
              className="text-xs font-semibold px-2 py-0.5 rounded"
              style={{
                background: result.success ? 'var(--green)' : 'var(--red)',
                color: '#000',
              }}
            >
              {result.success ? 'SUCCESS' : 'FAILED'}
            </span>
            <div className="flex gap-4 text-xs mono" style={{ color: 'var(--text-dim)' }}>
              <span>${result.total_cost_usd.toFixed(6)}</span>
              <span>{result.total_tokens.toLocaleString()} tok</span>
              <span>{result.steps} steps</span>
              <span>{(elapsed / 1000).toFixed(1)}s</span>
            </div>
          </div>

          <div className="whitespace-pre-wrap text-sm leading-relaxed">
            {result.content}
          </div>

          {result.step_log.length > 0 && (
            <div className="mt-4 pt-3 border-t" style={{ borderColor: 'var(--border)' }}>
              <div className="text-xs font-semibold mb-2" style={{ color: 'var(--text-dim)' }}>
                TOOL CALLS
              </div>
              {result.step_log.map((s, i) => (
                <div key={i} className="flex gap-2 text-xs mono py-0.5" style={{ color: 'var(--text-dim)' }}>
                  <span style={{ color: 'var(--accent)' }}>{s.step}.</span>
                  <span style={{ color: 'var(--yellow)' }}>{s.tool}</span>
                  <span className="truncate opacity-50">
                    {typeof s.args === 'string' ? s.args : JSON.stringify(s.args)}
                  </span>
                </div>
              ))}
            </div>
          )}
        </div>
      )}

      {/* History */}
      <h3 className="text-sm font-semibold mb-3" style={{ color: 'var(--text-dim)' }}>
        RUN HISTORY
      </h3>
      <div className="border rounded-lg overflow-hidden" style={{ borderColor: 'var(--border)' }}>
        {history.length === 0 ? (
          <div className="p-8 text-center text-sm" style={{ color: 'var(--text-dim)' }}>
            No runs yet.
          </div>
        ) : (
          <table className="w-full text-sm">
            <thead>
              <tr style={{ background: 'var(--bg-card)' }}>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Name</th>
                <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Goal</th>
                <th className="text-right p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Artifacts</th>
                <th className="text-right p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Time</th>
              </tr>
            </thead>
            <tbody>
              {history.map((run, i) => (
                <tr key={i} className="border-t" style={{ borderColor: 'var(--border)' }}>
                  <td className="p-3 mono text-xs" style={{ color: 'var(--accent)' }}>{run.name}</td>
                  <td className="p-3 truncate max-w-md">{run.description}</td>
                  <td className="p-3 text-right mono text-xs" style={{ color: 'var(--text-dim)' }}>
                    {run.artifact_count}
                  </td>
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
