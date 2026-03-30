'use client'
import { useState } from 'react'
import { streamFleet } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'

interface FleetEvent { type: string; data: Record<string, unknown> }

const PRESETS = [
  { label: 'Research', goal: 'Research and compare the top 5 alternatives in this space', agents: 3, budget: 1.5 },
  { label: 'Audit', goal: 'Audit this codebase for security, dependencies, and code quality', agents: 3, budget: 1.0 },
  { label: 'Compare', goal: 'Compare these options with pros, cons, and recommendation', agents: 3, budget: 1.0 },
]

export default function FleetPage() {
  const { isDemo } = useDemo()
  const [goal, setGoal] = useState('')
  const [agents, setAgents] = useState(3)
  const [budget, setBudget] = useState(1.0)
  const [running, setRunning] = useState(false)
  const [events, setEvents] = useState<FleetEvent[]>([])

  const handleRun = () => {
    if (!goal.trim() || running) return
    setRunning(true); setEvents([])
    streamFleet({ goal: goal.trim(), agents, budget }, (ev) => {
      const event = ev as unknown as FleetEvent
      setEvents((prev) => [...prev, event])
      if (event.type === 'fleet_done') setRunning(false)
    })
  }

  const fleetDone = events.find(e => e.type === 'fleet_done')
  const agentEvents = events.filter(e => e.type === 'agent_event')
  const agentsDone = events.filter(e => e.type === 'agent_done')
  const isSynthesizing = events.some(e => e.type === 'synthesizing')

  // Group events by agent
  const agentMap: Record<string, { events: FleetEvent[]; done: boolean; cost: number }> = {}
  for (const ev of events) {
    const agentName = String(ev.data.agent || ev.data.agent_name || '')
    if (!agentName) continue
    if (!agentMap[agentName]) agentMap[agentName] = { events: [], done: false, cost: 0 }
    agentMap[agentName].events.push(ev)
    if (ev.type === 'agent_done') agentMap[agentName].done = true
    if (ev.data.cost_usd) agentMap[agentName].cost = ev.data.cost_usd as number
  }

  return (
    <div className="max-w-[1000px]">
      <h2 className="text-[24px] font-semibold tracking-[-0.03em] mb-1">Fleet</h2>
      <p className="text-[14px] mb-6" style={{ color: 'var(--text-dim)' }}>
        Multiple agents work the same goal in parallel. Different perspectives, one synthesized result.
      </p>

      {/* Input */}
      <div className="card p-0 mb-6 overflow-hidden">
        <textarea value={goal} onChange={(e) => setGoal(e.target.value)}
          placeholder="What should the fleet work on?"
          rows={3}
          className="w-full p-5 text-[15px] resize-none outline-none placeholder:opacity-30 leading-relaxed"
          style={{ background: 'var(--bg-card)', color: 'var(--text)', border: 'none' }}
        />
        <div className="flex items-center justify-between px-5 py-3" style={{ background: 'var(--bg)', borderTop: '1px solid var(--border)' }}>
          <div className="flex items-center gap-4">
            <div className="flex items-center gap-2">
              <label className="text-[11px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>Agents</label>
              <div className="flex items-center gap-1">
                {[2, 3, 5, 8].map(n => (
                  <button key={n} onClick={() => setAgents(n)}
                    className="w-8 h-8 rounded-lg text-[12px] font-semibold transition-all"
                    style={{ background: agents === n ? 'var(--accent)' : 'var(--bg-card)', color: agents === n ? '#fff' : 'var(--text-dim)', border: `1px solid ${agents === n ? 'var(--accent)' : 'var(--border)'}` }}>
                    {n}
                  </button>
                ))}
              </div>
            </div>
            <div className="flex items-center gap-2">
              <label className="text-[11px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>Budget</label>
              <input type="number" step={0.5} min={0.1} value={budget}
                onChange={(e) => setBudget(parseFloat(e.target.value) || 1)}
                className="w-20 rounded-lg px-2 py-1.5 text-[13px] tabular-nums outline-none text-center"
                style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text)' }} />
            </div>
            <span className="text-[11px] tabular-nums" style={{ color: 'var(--text-dim)' }}>
              ~${(budget / agents).toFixed(2)}/agent
            </span>
          </div>
          <button onClick={handleRun} disabled={running || !goal.trim()}
            className="px-5 py-2 rounded-lg text-[13px] font-semibold text-white transition-all disabled:opacity-30 flex items-center gap-2"
            style={{ background: running ? 'var(--yellow)' : 'var(--accent)' }}>
            {running ? (
              <><svg width="14" height="14" viewBox="0 0 24 24" className="animate-spin"><circle cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="3" fill="none" strokeDasharray="30 70" strokeLinecap="round"/></svg> Running</>
            ) : (
              <>Launch Fleet</>
            )}
          </button>
        </div>
      </div>

      {/* Presets (when empty) */}
      {!goal && !running && events.length === 0 && (
        <div className="mb-8">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Quick Start</div>
          <div className="grid grid-cols-3 gap-3">
            {PRESETS.map((p, i) => (
              <button key={i} onClick={() => { setGoal(p.goal); setAgents(p.agents); setBudget(p.budget) }}
                className="card p-4 text-left hover:shadow-md transition-shadow">
                <div className="text-[13px] font-semibold mb-1">{p.label}</div>
                <p className="text-[12px] line-clamp-2" style={{ color: 'var(--text-dim)' }}>{p.goal}</p>
                <div className="text-[11px] mt-2" style={{ color: 'var(--text-dim)' }}>{p.agents} agents · ${p.budget.toFixed(2)}</div>
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Fleet progress */}
      {events.length > 0 && !fleetDone && (
        <div className="mb-6">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>
            {isSynthesizing ? 'Synthesizing results...' : `Working — ${agentsDone.length}/${agents} complete`}
          </div>

          {/* Agent lanes */}
          <div className="grid grid-cols-1 gap-2">
            {Object.entries(agentMap).map(([name, data]) => (
              <div key={name} className="card p-4">
                <div className="flex items-center justify-between mb-2">
                  <div className="flex items-center gap-2">
                    <div className={`w-2 h-2 rounded-full ${data.done ? '' : 'animate-pulse'}`}
                      style={{ background: data.done ? 'var(--green)' : 'var(--accent)' }} />
                    <span className="text-[13px] font-medium">{name}</span>
                  </div>
                  <div className="flex items-center gap-3 text-[11px]" style={{ color: 'var(--text-dim)' }}>
                    <span>{data.events.length} events</span>
                    {data.cost > 0 && <span className="tabular-nums">${data.cost.toFixed(4)}</span>}
                    {data.done && <span className="font-medium px-1.5 py-0.5 rounded-full" style={{ background: 'var(--green-light)', color: 'var(--green)' }}>done</span>}
                  </div>
                </div>
                {/* Mini event log */}
                <div className="space-y-0.5">
                  {data.events.slice(-3).map((ev, i) => (
                    <div key={i} className="text-[11px] mono truncate" style={{ color: 'var(--text-dim)' }}>
                      {ev.data.event_type === 'tool_call' ? `${ev.data.tool}(${JSON.stringify(ev.data.args).slice(0, 40)})` :
                       ev.data.event_type === 'done' ? `completed $${(ev.data.cost_usd as number)?.toFixed(4)}` :
                       String(ev.data.event_type || ev.type)}
                    </div>
                  ))}
                </div>
              </div>
            ))}
          </div>

          {/* Overall progress bar */}
          <div className="mt-3 h-[4px] rounded-full overflow-hidden" style={{ background: 'var(--border)' }}>
            <div className="h-full rounded-full transition-all duration-500"
              style={{ width: `${isSynthesizing ? 90 : (agentsDone.length / agents) * 80}%`, background: 'var(--accent)' }} />
          </div>
        </div>
      )}

      {/* Fleet result */}
      {fleetDone && (
        <div className="card overflow-hidden mb-6">
          {/* Header */}
          <div className="flex items-center justify-between px-5 py-3" style={{ background: 'var(--green-light)' }}>
            <div className="flex items-center gap-2">
              <span className="w-2 h-2 rounded-full" style={{ background: 'var(--green)' }} />
              <span className="text-[13px] font-semibold" style={{ color: 'var(--green)' }}>Fleet Complete</span>
            </div>
            <div className="flex gap-4 text-[12px] tabular-nums font-medium" style={{ color: 'var(--green)' }}>
              <span>{String(fleetDone.data.agent_count)} agents</span>
              <span>${(fleetDone.data.total_cost_usd as number)?.toFixed(4)}</span>
              <span>{(fleetDone.data.total_tokens as number)?.toLocaleString()} tokens</span>
            </div>
          </div>

          {/* Stats */}
          <div className="grid grid-cols-4 gap-0" style={{ borderBottom: '1px solid var(--border)' }}>
            {[
              { label: 'Agents', value: String(fleetDone.data.agent_count), color: 'var(--text)' },
              { label: 'Total Cost', value: `$${(fleetDone.data.total_cost_usd as number)?.toFixed(4)}`, color: 'var(--accent)' },
              { label: 'Tokens', value: (fleetDone.data.total_tokens as number)?.toLocaleString(), color: 'var(--blue)' },
              { label: 'Steps', value: String(fleetDone.data.total_steps), color: 'var(--yellow)' },
            ].map((s, i) => (
              <div key={i} className="px-5 py-4" style={{ borderRight: i < 3 ? '1px solid var(--border)' : undefined }}>
                <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>{s.label}</div>
                <div className="text-[18px] font-bold tabular-nums" style={{ color: s.color }}>{s.value}</div>
              </div>
            ))}
          </div>

          {/* Full event log (collapsed by default) */}
          <details className="group">
            <summary className="px-5 py-3 text-[12px] font-medium cursor-pointer select-none flex items-center gap-2" style={{ color: 'var(--text-dim)' }}>
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" className="transition-transform group-open:rotate-90"><polyline points="9 18 15 12 9 6"/></svg>
              {events.length} events
            </summary>
            <div className="max-h-64 overflow-y-auto px-5 pb-4 space-y-0.5">
              {events.map((ev, i) => (
                <div key={i} className="flex gap-2 text-[11px] mono py-0.5">
                  <span style={{ color: evColor(ev.type), minWidth: 90 }}>{ev.type}</span>
                  <span style={{ color: 'var(--text-dim)' }} className="truncate">{fmtEvent(ev)}</span>
                </div>
              ))}
            </div>
          </details>
        </div>
      )}
    </div>
  )
}

function evColor(t: string): string {
  if (t === 'fleet_start') return 'var(--accent)'; if (t.includes('done')) return 'var(--green)'; if (t === 'synthesizing') return 'var(--yellow)'; return 'var(--text-dim)'
}
function fmtEvent(ev: FleetEvent): string {
  const d = ev.data
  if (ev.type === 'fleet_start') return `${d.agent_count} agents, $${(d.total_budget_usd as number)?.toFixed(2)}`
  if (ev.type === 'agent_event') { if (d.event_type === 'tool_call') return `[${d.agent}] ${d.tool}(${JSON.stringify(d.args).slice(0,50)})`; if (d.event_type === 'done') return `[${d.agent}] $${(d.cost_usd as number)?.toFixed(4)}`; return `[${d.agent}] ${d.event_type}` }
  if (ev.type === 'agent_done') return `${d.agent} (${d.completed}/${d.total})`
  if (ev.type === 'synthesizing') return 'Merging...'
  if (ev.type === 'fleet_done') return `$${(d.total_cost_usd as number)?.toFixed(4)}`
  return JSON.stringify(d).slice(0, 60)
}
