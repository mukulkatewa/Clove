'use client'
import { useState } from 'react'
import { streamFleet } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'

interface FleetEvent {
  type: string
  data: Record<string, unknown>
}

export default function FleetPage() {
  const { isDemo } = useDemo()
  const [goal, setGoal] = useState('')
  const [agents, setAgents] = useState(3)
  const [budget, setBudget] = useState(1.0)
  const [running, setRunning] = useState(false)
  const [events, setEvents] = useState<FleetEvent[]>([])
  const [done, setDone] = useState(false)

  const handleRun = () => {
    if (!goal.trim() || running) return
    setRunning(true)
    setEvents([])
    setDone(false)

    streamFleet({ goal: goal.trim(), agents, budget }, (ev) => {
      const event = ev as unknown as FleetEvent
      setEvents((prev) => [...prev, event])
      if (event.type === 'fleet_done') {
        setDone(true)
        setRunning(false)
      }
    })
  }

  const fleetDone = events.find((e) => e.type === 'fleet_done')

  return (
    <div>
      <h2 style={{ fontSize: 24, fontWeight: 600, marginBottom: 4, color: 'var(--text)' }}>Fleet</h2>
      <p style={{ fontSize: 14, color: 'var(--text-secondary)', marginBottom: 32 }}>
        Run multiple agents in parallel on a single goal. Watch them work in real-time.
      </p>

      {/* Form */}
      <div className="card" style={{ padding: 20, marginBottom: 24 }}>
        <textarea
          value={goal}
          onChange={(e) => setGoal(e.target.value)}
          placeholder="What should the fleet work on?"
          rows={2}
          className="w-full text-sm resize-none outline-none placeholder:opacity-30"
          style={{
            background: 'var(--bg)',
            border: '1px solid var(--border)',
            borderRadius: 12,
            padding: 12,
            color: 'var(--text)',
          }}
        />
        <div className="flex items-end gap-4 mt-3">
          <div>
            <label className="block" style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 6 }}>Agents</label>
            <input
              type="number" min={1} max={20} value={agents}
              onChange={(e) => setAgents(parseInt(e.target.value) || 3)}
              className="w-20 mono text-sm outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--accent)' }}
            />
          </div>
          <div>
            <label className="block" style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 6 }}>Budget (USD)</label>
            <input
              type="number" step={0.1} min={0.01} value={budget}
              onChange={(e) => setBudget(parseFloat(e.target.value) || 1)}
              className="w-28 mono text-sm outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--accent)' }}
            />
          </div>
          <div className="flex-1" />
          <button
            onClick={handleRun}
            disabled={running || !goal.trim()}
            className="text-sm font-semibold disabled:opacity-30"
            style={{
              background: running ? 'var(--yellow)' : 'var(--accent)',
              color: '#fff',
              borderRadius: 12,
              padding: '8px 24px',
            }}
          >
            {running ? 'Running...' : 'Launch Fleet'}
          </button>
        </div>
      </div>

      {/* Fleet result */}
      {fleetDone && (
        <div className="card" style={{ padding: 16, marginBottom: 24, borderLeft: '3px solid var(--green)', display: 'flex', gap: 24 }}>
          <Stat label="Agents" value={String(fleetDone.data.agent_count)} color="var(--accent)" />
          <Stat label="Cost" value={`$${(fleetDone.data.total_cost_usd as number)?.toFixed(4)}`} color="var(--green)" />
          <Stat label="Tokens" value={(fleetDone.data.total_tokens as number)?.toLocaleString()} color="var(--blue)" />
          <Stat label="Steps" value={String(fleetDone.data.total_steps)} color="var(--yellow)" />
        </div>
      )}

      {/* Event stream */}
      {events.length > 0 && (
        <div className="card" style={{ padding: 0, overflow: 'hidden' }}>
          <div style={{ padding: 12, fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', background: 'var(--bg)' }}>
            Live Events ({events.length})
          </div>
          <div className="max-h-96 overflow-y-auto space-y-1" style={{ padding: 12, background: 'var(--bg-card)' }}>
            {events.map((ev, i) => (
              <div key={i} className="flex gap-2 mono py-0.5" style={{ fontSize: 12 }}>
                <span style={{ color: eventColor(ev.type), minWidth: 100 }}>
                  {ev.type}
                </span>
                <span style={{ color: 'var(--text-dim)' }} className="truncate">
                  {formatEvent(ev)}
                </span>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  )
}

function Stat({ label, value, color }: { label: string; value: string; color: string }) {
  return (
    <div>
      <div style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)' }}>{label}</div>
      <div className="mono" style={{ fontSize: 18, fontWeight: 600, color }}>{value}</div>
    </div>
  )
}

function eventColor(type: string): string {
  if (type === 'fleet_start') return 'var(--accent)'
  if (type === 'fleet_done') return 'var(--green)'
  if (type === 'agent_done') return 'var(--green)'
  if (type === 'synthesizing') return 'var(--yellow)'
  return 'var(--text-dim)'
}

function formatEvent(ev: FleetEvent): string {
  const d = ev.data
  if (ev.type === 'fleet_start') return `${d.agent_count} agents, $${(d.total_budget_usd as number)?.toFixed(2)} budget`
  if (ev.type === 'agent_event') {
    if (d.event_type === 'tool_call') return `[${d.agent}] ${d.tool}(${JSON.stringify(d.args).slice(0, 60)})`
    if (d.event_type === 'done') return `[${d.agent}] done $${(d.cost_usd as number)?.toFixed(4)}`
    return `[${d.agent}] ${d.event_type}`
  }
  if (ev.type === 'agent_done') return `${d.agent} complete (${d.completed}/${d.total})`
  if (ev.type === 'synthesizing') return 'Merging outputs...'
  if (ev.type === 'fleet_done') return `Complete. $${(d.total_cost_usd as number)?.toFixed(4)}`
  return JSON.stringify(d).slice(0, 80)
}
