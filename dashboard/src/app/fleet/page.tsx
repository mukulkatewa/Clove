'use client'
import { useState } from 'react'
import { streamFleet } from '@/lib/api'

interface FleetEvent {
  type: string
  data: Record<string, unknown>
}

export default function FleetPage() {
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
      <h2 className="text-2xl font-semibold mb-1">Fleet</h2>
      <p className="text-sm mb-8" style={{ color: 'var(--text-dim)' }}>
        Run multiple agents in parallel on a single goal. Watch them work in real-time.
      </p>

      {/* Form */}
      <div
        className="border rounded-lg p-5 mb-6"
        style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}
      >
        <textarea
          value={goal}
          onChange={(e) => setGoal(e.target.value)}
          placeholder="What should the fleet work on?"
          rows={2}
          className="w-full rounded-md p-3 text-sm resize-none outline-none placeholder:opacity-30"
          style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
        />
        <div className="flex items-end gap-4 mt-3">
          <div>
            <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>Agents</label>
            <input
              type="number" min={1} max={20} value={agents}
              onChange={(e) => setAgents(parseInt(e.target.value) || 3)}
              className="w-20 rounded-md px-3 py-2 text-sm mono outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--accent)' }}
            />
          </div>
          <div>
            <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>Budget (USD)</label>
            <input
              type="number" step={0.1} min={0.01} value={budget}
              onChange={(e) => setBudget(parseFloat(e.target.value) || 1)}
              className="w-28 rounded-md px-3 py-2 text-sm mono outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--accent)' }}
            />
          </div>
          <div className="flex-1" />
          <button
            onClick={handleRun}
            disabled={running || !goal.trim()}
            className="px-6 py-2 rounded-md text-sm font-semibold disabled:opacity-30"
            style={{ background: running ? 'var(--yellow)' : 'var(--accent)', color: '#000' }}
          >
            {running ? 'Running...' : 'Launch Fleet'}
          </button>
        </div>
      </div>

      {/* Fleet result */}
      {fleetDone && (
        <div
          className="border rounded-lg p-4 mb-6 flex gap-6"
          style={{ borderColor: 'var(--green)', borderLeftWidth: 3, background: 'var(--bg-card)' }}
        >
          <Stat label="Agents" value={String(fleetDone.data.agent_count)} color="var(--accent)" />
          <Stat label="Cost" value={`$${(fleetDone.data.total_cost_usd as number)?.toFixed(4)}`} color="var(--green)" />
          <Stat label="Tokens" value={(fleetDone.data.total_tokens as number)?.toLocaleString()} color="var(--blue)" />
          <Stat label="Steps" value={String(fleetDone.data.total_steps)} color="var(--yellow)" />
        </div>
      )}

      {/* Event stream */}
      {events.length > 0 && (
        <div
          className="border rounded-lg overflow-hidden"
          style={{ borderColor: 'var(--border)' }}
        >
          <div className="p-3 text-xs font-semibold" style={{ background: 'var(--bg-card)', color: 'var(--text-dim)' }}>
            LIVE EVENTS ({events.length})
          </div>
          <div className="max-h-96 overflow-y-auto p-3 space-y-1" style={{ background: 'var(--bg)' }}>
            {events.map((ev, i) => (
              <div key={i} className="flex gap-2 text-xs mono py-0.5">
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
      <div className="text-xs" style={{ color: 'var(--text-dim)' }}>{label}</div>
      <div className="text-lg font-semibold mono" style={{ color }}>{value}</div>
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
