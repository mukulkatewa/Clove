'use client'
import { useEffect, useState, useRef } from 'react'
import { getHealth, getCost, getAgentDefs, submitRun, streamFleet } from '@/lib/api'
import type { HealthResponse, CostResponse, RunResponse, AgentDef } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoHealth, demoCost } from '@/lib/demo-data'
import Link from 'next/link'

const SUGGESTIONS = [
  'Review my latest code changes for security issues',
  'Research the AI agent market and compile a digest',
  'Monitor api.example.com every 5 minutes',
  'Find leads for B2B SaaS companies using AI agents',
  'Audit dependencies for vulnerabilities',
  'Create a daily standup summary from Slack',
]

const DEMO_AGENTS: AgentDef[] = [
  { name: 'pr-reviewer', description: 'Reviews PRs for security', enabled: true, triggers: [{ type: 'webhook', source: 'github' }], connections: ['github'], action: { goal: 'Review PRs', tools: ['read_file', 'exec'], max_steps: 10 }, budget: { per_run: 0.30, daily_max: 10 } },
  { name: 'api-monitor', description: 'Health checks every 5min', enabled: true, triggers: [{ type: 'cron', schedule: '*/5 * * * *' }], connections: [], action: { goal: 'Check API', tools: ['http'], max_steps: 3 }, budget: { per_run: 0.05, daily_max: 2 } },
  { name: 'news-digest', description: 'AI news every morning', enabled: true, triggers: [{ type: 'cron', schedule: '0 8 * * MON-FRI' }], connections: ['slack'], action: { goal: 'News digest', tools: ['search', 'http'], max_steps: 12 }, budget: { per_run: 0.40, daily_max: 3 } },
]

export default function Home() {
  const { isDemo } = useDemo()
  const [health, setHealth] = useState<HealthResponse | null>(null)
  const [cost, setCost] = useState<CostResponse | null>(null)
  const [agents, setAgents] = useState<AgentDef[]>([])
  const [input, setInput] = useState('')
  const [running, setRunning] = useState(false)
  const [result, setResult] = useState<RunResponse | null>(null)
  const [elapsed, setElapsed] = useState(0)
  const timerRef = useRef<NodeJS.Timeout | null>(null)
  const resultRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (isDemo) {
      setHealth(demoHealth() as HealthResponse); setCost(demoCost() as CostResponse); setAgents(DEMO_AGENTS); return
    }
    const load = () => {
      getHealth().then(setHealth).catch(() => {})
      getCost().then(setCost).catch(() => {})
      getAgentDefs().then(r => setAgents(r.agents || [])).catch(() => {})
    }
    load(); const i = setInterval(load, 5000); return () => clearInterval(i)
  }, [isDemo])

  const handleRun = async () => {
    if (!input.trim() || running) return
    setRunning(true); setResult(null); setElapsed(0)
    const start = Date.now()
    timerRef.current = setInterval(() => setElapsed(Date.now() - start), 100)

    try {
      const res = await submitRun({ goal: input.trim(), budget: 0.50, max_steps: 15 })
      setResult(res)
      setTimeout(() => resultRef.current?.scrollIntoView({ behavior: 'smooth' }), 100)
    } catch (e) {
      setResult({ success: false, content: `Error: ${e}`, total_cost_usd: 0, total_tokens: 0, steps: 0, chain_id: '', step_log: [] })
    } finally {
      setRunning(false); if (timerRef.current) clearInterval(timerRef.current)
    }
  }

  const enabledAgents = agents.filter(a => a.enabled)

  return (
    <div className="max-w-[860px] mx-auto">
      {/* Header */}
      <div className="flex items-center justify-between mb-2">
        <div>
          <h1 className="text-[26px] font-semibold tracking-[-0.03em]">Clove</h1>
          <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>
            {health ? `v${health.version} · ${enabledAgents.length} agents active · $${(cost?.total_cost_usd ?? 0).toFixed(2)} spent` : 'Connecting...'}
          </p>
        </div>
        {health && (
          <div className="flex items-center gap-2 px-3 py-1.5 rounded-full" style={{ background: 'var(--green-light)' }}>
            <span className="w-[6px] h-[6px] rounded-full" style={{ background: 'var(--green)' }} />
            <span className="text-[11px] font-medium" style={{ color: 'var(--green)' }}>Running</span>
          </div>
        )}
      </div>

      {/* Main input */}
      <div className="card p-0 mt-6 mb-6 overflow-hidden">
        <textarea
          value={input}
          onChange={(e) => setInput(e.target.value)}
          placeholder="What do you want agents to do?"
          rows={3}
          className="w-full p-6 text-[16px] resize-none outline-none placeholder:opacity-30 leading-relaxed"
          style={{ background: 'var(--bg-card)', color: 'var(--text)', border: 'none' }}
          onKeyDown={(e) => { if (e.key === 'Enter' && e.metaKey) handleRun() }}
        />
        <div className="flex items-center justify-between px-6 py-3" style={{ background: 'var(--bg)', borderTop: '1px solid var(--border)' }}>
          <div className="flex items-center gap-3 text-[12px]" style={{ color: 'var(--text-dim)' }}>
            <span>$0.50 budget</span>
            <span>·</span>
            <span>15 max steps</span>
            {running && <span className="tabular-nums font-medium" style={{ color: 'var(--accent)' }}>{(elapsed / 1000).toFixed(1)}s</span>}
          </div>
          <button onClick={handleRun} disabled={running || !input.trim()}
            className="px-6 py-2.5 rounded-xl text-[13px] font-semibold text-white transition-all disabled:opacity-30 flex items-center gap-2"
            style={{ background: 'var(--accent)' }}>
            {running ? (
              <><svg width="14" height="14" viewBox="0 0 24 24" className="animate-spin"><circle cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="3" fill="none" strokeDasharray="30 70" strokeLinecap="round"/></svg>Running</>
            ) : (
              <>Run <kbd className="text-[9px] opacity-60 bg-white/10 px-1 rounded">⌘↵</kbd></>
            )}
          </button>
        </div>
      </div>

      {/* Suggestions */}
      {!input && !result && !running && (
        <div className="mb-8">
          <div className="flex flex-wrap gap-2">
            {SUGGESTIONS.map((s, i) => (
              <button key={i} onClick={() => setInput(s)}
                className="px-3.5 py-2 rounded-xl text-[12px] transition-all hover:shadow-sm"
                style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
                {s}
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Loading */}
      {running && (
        <div className="card p-5 mb-6">
          <div className="flex items-center gap-3">
            <div className="w-2 h-2 rounded-full animate-pulse" style={{ background: 'var(--accent)' }} />
            <span className="text-[14px] font-medium">Agent working...</span>
            <span className="text-[12px] tabular-nums ml-auto" style={{ color: 'var(--text-dim)' }}>{(elapsed / 1000).toFixed(1)}s</span>
          </div>
        </div>
      )}

      {/* Result */}
      {result && (
        <div ref={resultRef} className="card overflow-hidden mb-8">
          <div className="flex items-center justify-between px-5 py-3" style={{ background: result.success ? 'var(--green-light)' : 'var(--red-light)' }}>
            <div className="flex items-center gap-2">
              <span className="w-2 h-2 rounded-full" style={{ background: result.success ? 'var(--green)' : 'var(--red)' }} />
              <span className="text-[13px] font-semibold" style={{ color: result.success ? 'var(--green)' : 'var(--red)' }}>
                {result.success ? 'Done' : 'Failed'}
              </span>
            </div>
            <div className="flex gap-3 text-[11px] tabular-nums font-medium" style={{ color: result.success ? 'var(--green)' : 'var(--red)' }}>
              <span>${result.total_cost_usd.toFixed(4)}</span>
              <span>{result.total_tokens.toLocaleString()} tokens</span>
              <span>{result.steps} steps</span>
              <span>{(elapsed / 1000).toFixed(1)}s</span>
            </div>
          </div>
          <div className="p-5 text-[14px] leading-[1.7] whitespace-pre-wrap">{result.content}</div>
          {result.step_log.length > 0 && (
            <details className="group">
              <summary className="px-5 py-2.5 text-[12px] font-medium cursor-pointer select-none" style={{ color: 'var(--text-dim)', borderTop: '1px solid var(--border)' }}>
                {result.step_log.length} tool calls
              </summary>
              <div className="px-5 pb-4 space-y-1">
                {result.step_log.map((s, i) => (
                  <div key={i} className="flex items-start gap-2 text-[12px]">
                    <span className="w-5 h-5 rounded flex items-center justify-center text-[9px] font-bold flex-shrink-0" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{s.step}</span>
                    <span className="font-medium" style={{ color: 'var(--accent)' }}>{s.tool}</span>
                    <span className="mono truncate" style={{ color: 'var(--text-dim)' }}>{typeof s.args === 'string' ? s.args : JSON.stringify(s.args)}</span>
                  </div>
                ))}
              </div>
            </details>
          )}
        </div>
      )}

      {/* Active agents */}
      {enabledAgents.length > 0 && (
        <div className="mb-8">
          <div className="flex items-center justify-between mb-3">
            <span className="text-[11px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>Active Agents</span>
            <Link href="/agents" className="text-[11px] font-medium" style={{ color: 'var(--accent)' }}>View all →</Link>
          </div>
          <div className="grid grid-cols-3 gap-2">
            {enabledAgents.slice(0, 6).map(a => (
              <Link key={a.name} href={`/agents`} className="card p-3.5 hover:shadow-md transition-all">
                <div className="flex items-center gap-2 mb-1.5">
                  <div className="w-[6px] h-[6px] rounded-full animate-pulse" style={{ background: 'var(--green)' }} />
                  <span className="text-[12px] font-semibold truncate">{a.name}</span>
                </div>
                <div className="text-[11px] truncate" style={{ color: 'var(--text-dim)' }}>{a.description}</div>
                <div className="flex items-center gap-2 mt-2 text-[10px]" style={{ color: 'var(--text-dim)' }}>
                  {a.triggers.map((t, i) => (
                    <span key={i} className="px-1.5 py-0.5 rounded" style={{
                      background: t.type === 'cron' ? 'var(--blue-light)' : t.type === 'webhook' ? 'var(--accent-light)' : 'var(--bg)',
                      color: t.type === 'cron' ? 'var(--blue)' : t.type === 'webhook' ? 'var(--accent)' : 'var(--text-dim)',
                    }}>
                      {t.type === 'cron' ? t.schedule : t.type === 'webhook' ? t.source : 'manual'}
                    </span>
                  ))}
                  <span className="ml-auto tabular-nums">${a.budget.per_run}/run</span>
                </div>
              </Link>
            ))}
          </div>
        </div>
      )}

      {/* Quick stats */}
      <div className="grid grid-cols-4 gap-2">
        <div className="card px-4 py-3">
          <div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Agents</div>
          <div className="text-[18px] font-bold tabular-nums mt-0.5" style={{ color: 'var(--green)' }}>{enabledAgents.length}</div>
        </div>
        <div className="card px-4 py-3">
          <div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Cost</div>
          <div className="text-[18px] font-bold tabular-nums mt-0.5" style={{ color: 'var(--accent)' }}>${(cost?.total_cost_usd ?? 0).toFixed(2)}</div>
        </div>
        <div className="card px-4 py-3">
          <div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Requests</div>
          <div className="text-[18px] font-bold tabular-nums mt-0.5">{cost?.total_requests ?? 0}</div>
        </div>
        <div className="card px-4 py-3">
          <div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>Uptime</div>
          <div className="text-[18px] font-bold tabular-nums mt-0.5">{health ? fmtUp(health.uptime_s) : '—'}</div>
        </div>
      </div>
    </div>
  )
}

function fmtUp(s: number): string {
  if (s > 86400) return `${Math.floor(s / 86400)}d`
  if (s > 3600) return `${Math.floor(s / 3600)}h`
  if (s > 60) return `${Math.floor(s / 60)}m`
  return `${s}s`
}
