'use client'
import { useEffect, useState } from 'react'
import { getHealth, getCost, getHistory, getOpenClawStatus, getAudit } from '@/lib/api'
import type { HealthResponse, CostResponse, HistoryEntry, OpenClawInstance, AuditEntry } from '@/lib/api'

export default function Overview() {
  const [health, setHealth] = useState<HealthResponse | null>(null)
  const [cost, setCost] = useState<CostResponse | null>(null)
  const [history, setHistory] = useState<HistoryEntry[]>([])
  const [instances, setInstances] = useState<OpenClawInstance[]>([])
  const [audit, setAudit] = useState<AuditEntry[]>([])
  const [error, setError] = useState('')

  useEffect(() => {
    const load = () => {
      getHealth().then(setHealth).catch(() => setError('Kernel not reachable'))
      getCost().then(setCost).catch(() => {})
      getHistory().then((r) => setHistory(r.runs || [])).catch(() => {})
      getOpenClawStatus().then((r) => setInstances(r.instances || [])).catch(() => {})
      getAudit(10).then(setAudit).catch(() => {})
    }
    load()
    const interval = setInterval(load, 5000)
    return () => clearInterval(interval)
  }, [])

  if (error) {
    return (
      <div className="flex items-center justify-center h-[60vh]">
        <div className="text-center">
          <div className="text-4xl mb-4" style={{ color: 'var(--red)' }}>Offline</div>
          <p style={{ color: 'var(--text-dim)' }}>
            Kernel not reachable at localhost:8080
          </p>
          <p className="mono text-xs mt-4" style={{ color: 'var(--text-dim)' }}>
            ./clove_kernel --sandbox --privacy --api
          </p>
        </div>
      </div>
    )
  }

  const runningAgents = instances.filter((i) => i.state === 'running')

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-2xl font-semibold">Overview</h2>
          <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>
            {health ? `Kernel v${health.version} | ${health.syscall_count} syscalls | uptime ${formatUptime(health.uptime_s)}` : 'Loading...'}
          </p>
        </div>
        <div className="flex items-center gap-2">
          <span
            className="w-2 h-2 rounded-full"
            style={{ background: health ? 'var(--green)' : 'var(--red)' }}
          />
          <span className="text-sm" style={{ color: health ? 'var(--green)' : 'var(--red)' }}>
            {health ? 'Running' : 'Offline'}
          </span>
        </div>
      </div>

      {/* Stat cards */}
      <div className="grid grid-cols-4 gap-4 mb-8">
        <StatCard
          label="Total Cost"
          value={`$${(cost?.total_cost_usd ?? 0).toFixed(4)}`}
          sub={cost?.max_cost_usd ? `/ $${cost.max_cost_usd.toFixed(2)} budget` : 'no budget set'}
          color="var(--accent)"
        />
        <StatCard
          label="Active Agents"
          value={String(runningAgents.length)}
          sub={`${instances.length} total instances`}
          color="var(--green)"
        />
        <StatCard
          label="Runs"
          value={String(history.length)}
          sub="total completed"
          color="var(--blue)"
        />
        <StatCard
          label="Audit Entries"
          value={String(audit.length)}
          sub="last 10 shown"
          color="var(--yellow)"
        />
      </div>

      {/* Active agents */}
      {runningAgents.length > 0 && (
        <div className="mb-8">
          <h3 className="text-sm font-semibold mb-3" style={{ color: 'var(--text-dim)' }}>
            ACTIVE AGENTS
          </h3>
          <div className="grid grid-cols-3 gap-4">
            {runningAgents.map((inst) => (
              <div
                key={inst.id}
                className="border rounded-lg p-4"
                style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}
              >
                <div className="flex items-center justify-between mb-2">
                  <span className="font-medium">{inst.name}</span>
                  <span className="text-xs px-2 py-0.5 rounded" style={{ background: 'var(--green)', color: '#000' }}>
                    running
                  </span>
                </div>
                <div className="text-xs space-y-1" style={{ color: 'var(--text-dim)' }}>
                  <div>PID: {inst.pid}</div>
                  <div>Budget: ${inst.budget_usd.toFixed(2)}</div>
                  <div>Cost: ${inst.cost_usd.toFixed(4)}</div>
                  {inst.channels.length > 0 && <div>Channels: {inst.channels.join(', ')}</div>}
                </div>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Recent runs */}
      <div className="mb-8">
        <h3 className="text-sm font-semibold mb-3" style={{ color: 'var(--text-dim)' }}>
          RECENT RUNS
        </h3>
        <div className="border rounded-lg overflow-hidden" style={{ borderColor: 'var(--border)' }}>
          {history.length === 0 ? (
            <div className="p-8 text-center text-sm" style={{ color: 'var(--text-dim)' }}>
              No runs yet. Submit one from the Runs page.
            </div>
          ) : (
            <table className="w-full text-sm">
              <thead>
                <tr style={{ background: 'var(--bg-card)' }}>
                  <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Name</th>
                  <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Goal</th>
                  <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Chain</th>
                  <th className="text-right p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Time</th>
                </tr>
              </thead>
              <tbody>
                {history.slice(0, 10).map((run, i) => (
                  <tr key={i} className="border-t" style={{ borderColor: 'var(--border)' }}>
                    <td className="p-3 mono text-xs">{run.name}</td>
                    <td className="p-3 truncate max-w-xs">{run.description}</td>
                    <td className="p-3 mono text-xs" style={{ color: 'var(--text-dim)' }}>
                      {run.chain_id.slice(0, 16)}
                    </td>
                    <td className="p-3 text-right text-xs" style={{ color: 'var(--text-dim)' }}>
                      {new Date(run.created_at_ms).toLocaleTimeString()}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </div>

      {/* Recent audit */}
      <div>
        <h3 className="text-sm font-semibold mb-3" style={{ color: 'var(--text-dim)' }}>
          RECENT AUDIT
        </h3>
        <div className="border rounded-lg overflow-hidden" style={{ borderColor: 'var(--border)' }}>
          {audit.length === 0 ? (
            <div className="p-8 text-center text-sm" style={{ color: 'var(--text-dim)' }}>
              No audit entries yet.
            </div>
          ) : (
            <table className="w-full text-sm">
              <thead>
                <tr style={{ background: 'var(--bg-card)' }}>
                  <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Event</th>
                  <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Category</th>
                  <th className="text-left p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Agent</th>
                  <th className="text-right p-3 font-medium" style={{ color: 'var(--text-dim)' }}>Time</th>
                </tr>
              </thead>
              <tbody>
                {audit.map((entry) => (
                  <tr key={entry.id} className="border-t" style={{ borderColor: 'var(--border)' }}>
                    <td className="p-3 mono text-xs">{entry.event_type}</td>
                    <td className="p-3 text-xs">{entry.category}</td>
                    <td className="p-3 text-xs">{entry.agent_name || `#${entry.agent_id}`}</td>
                    <td className="p-3 text-right text-xs" style={{ color: 'var(--text-dim)' }}>
                      {new Date(entry.timestamp).toLocaleTimeString()}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </div>
    </div>
  )
}

function StatCard({ label, value, sub, color }: { label: string; value: string; sub: string; color: string }) {
  return (
    <div className="border rounded-lg p-4" style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}>
      <div className="text-xs mb-2" style={{ color: 'var(--text-dim)' }}>{label}</div>
      <div className="text-2xl font-semibold mono" style={{ color }}>{value}</div>
      <div className="text-xs mt-1" style={{ color: 'var(--text-dim)' }}>{sub}</div>
    </div>
  )
}

function formatUptime(s: number): string {
  if (s > 86400) return `${Math.floor(s / 86400)}d ${Math.floor((s % 86400) / 3600)}h`
  if (s > 3600) return `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m`
  if (s > 60) return `${Math.floor(s / 60)}m ${s % 60}s`
  return `${s}s`
}
