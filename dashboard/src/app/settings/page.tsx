'use client'
import { useEffect, useState } from 'react'
import { getSchedules, createSchedule, deleteSchedule, getWebhooks, createWebhook, deleteWebhook } from '@/lib/api'
import type { Schedule, Webhook } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoSchedules, demoWebhooks } from '@/lib/demo-data'

export default function SettingsPage() {
  const { isDemo } = useDemo()
  const [schedules, setSchedules] = useState<Schedule[]>([])
  const [webhooks, setWebhooks] = useState<Webhook[]>([])

  // Schedule form
  const [schName, setSchName] = useState('')
  const [schCron, setSchCron] = useState('')
  const [schGoal, setSchGoal] = useState('')
  const [schBudget, setSchBudget] = useState(5)
  const [schAgents, setSchAgents] = useState(3)

  // Webhook form
  const [whUrl, setWhUrl] = useState('')
  const [whEvents, setWhEvents] = useState('run_complete,budget_exceeded')

  const refresh = () => {
    getSchedules().then((r) => setSchedules(r.schedules || [])).catch(() => {})
    getWebhooks().then((r) => setWebhooks(r.webhooks || [])).catch(() => {})
  }

  useEffect(() => { if (isDemo) { setSchedules(demoSchedules() as any); setWebhooks(demoWebhooks() as any); return } refresh() }, [isDemo])

  const handleCreateSchedule = async () => {
    if (!schName || !schCron || !schGoal) return
    await createSchedule({
      name: schName,
      cron: schCron,
      run: { goal: schGoal, budget: schBudget, agents: schAgents },
    })
    setSchName('')
    setSchCron('')
    setSchGoal('')
    refresh()
  }

  const handleCreateWebhook = async () => {
    if (!whUrl) return
    await createWebhook({
      url: whUrl,
      events: whEvents.split(',').map((s) => s.trim()),
    })
    setWhUrl('')
    refresh()
  }

  const inputStyle: React.CSSProperties = {
    background: 'var(--bg)',
    border: '1px solid var(--border)',
    color: 'var(--text)',
    borderRadius: 12,
    padding: '8px 12px',
    fontSize: 14,
    outline: 'none',
  }

  const btnStyle: React.CSSProperties = {
    background: 'var(--accent)',
    color: '#fff',
    borderRadius: 12,
    padding: '8px 16px',
    fontSize: 12,
    fontWeight: 600,
    whiteSpace: 'nowrap',
  }

  return (
    <div>
      <h2 style={{ fontSize: 24, fontWeight: 600, marginBottom: 32, color: 'var(--text)' }}>Settings</h2>

      {/* Schedules */}
      <Section title="Schedules" sub="Recurring agent runs on a cron schedule.">
        <div className="grid grid-cols-3 gap-3 mb-3">
          <input value={schName} onChange={(e) => setSchName(e.target.value)} placeholder="Name" style={inputStyle} className="placeholder:opacity-20" />
          <input value={schCron} onChange={(e) => setSchCron(e.target.value)} placeholder="0 9 * * MON" style={inputStyle} className="mono placeholder:opacity-20" />
          <input value={schGoal} onChange={(e) => setSchGoal(e.target.value)} placeholder="Goal" style={inputStyle} className="placeholder:opacity-20" />
        </div>
        <div className="flex gap-3 mb-4">
          <input type="number" value={schBudget} onChange={(e) => setSchBudget(parseFloat(e.target.value) || 5)} style={{ ...inputStyle, width: 112 }} className="mono" />
          <input type="number" value={schAgents} onChange={(e) => setSchAgents(parseInt(e.target.value) || 3)} style={{ ...inputStyle, width: 80 }} className="mono" />
          <button onClick={handleCreateSchedule} style={btnStyle}>Add Schedule</button>
        </div>
        {schedules.map((s) => (
          <div key={s.name} className="flex items-center justify-between py-2" style={{ borderTop: '1px solid var(--border-subtle)' }}>
            <div>
              <span className="mono text-sm" style={{ color: 'var(--accent)' }}>{s.name}</span>
              <span style={{ fontSize: 12, marginLeft: 12, color: 'var(--text-dim)' }}>{s.cron}</span>
              <span style={{ fontSize: 12, marginLeft: 12, color: 'var(--text-dim)' }}>{s.run.goal}</span>
            </div>
            <button onClick={() => { deleteSchedule(s.name); refresh() }} style={{ fontSize: 12, color: 'var(--red)', background: 'var(--red-light)', padding: '2px 8px', borderRadius: 8 }}>Delete</button>
          </div>
        ))}
      </Section>

      {/* Webhooks */}
      <Section title="Webhooks" sub="Get notified when events happen.">
        <div className="flex gap-3 mb-4">
          <input value={whUrl} onChange={(e) => setWhUrl(e.target.value)} placeholder="https://hooks.slack.com/..." style={{ ...inputStyle, flex: 1 }} className="placeholder:opacity-20" />
          <input value={whEvents} onChange={(e) => setWhEvents(e.target.value)} placeholder="run_complete,budget_exceeded" style={{ ...inputStyle, width: 256 }} className="mono placeholder:opacity-20" />
          <button onClick={handleCreateWebhook} style={btnStyle}>Add Webhook</button>
        </div>
        {webhooks.map((w) => (
          <div key={w.id} className="flex items-center justify-between py-2" style={{ borderTop: '1px solid var(--border-subtle)' }}>
            <div>
              <span className="text-sm truncate" style={{ color: 'var(--text)' }}>{w.url}</span>
              <span className="mono" style={{ fontSize: 12, marginLeft: 12, color: 'var(--text-dim)' }}>{w.events.join(', ')}</span>
            </div>
            <button onClick={() => { deleteWebhook(w.id); refresh() }} style={{ fontSize: 12, color: 'var(--red)', background: 'var(--red-light)', padding: '2px 8px', borderRadius: 8 }}>Delete</button>
          </div>
        ))}
      </Section>
    </div>
  )
}

function Section({ title, sub, children }: { title: string; sub: string; children: React.ReactNode }) {
  return (
    <div className="card" style={{ padding: 20, marginBottom: 24 }}>
      <h3 style={{ fontSize: 16, fontWeight: 600, marginBottom: 4, color: 'var(--text)' }}>{title}</h3>
      <p style={{ fontSize: 12, marginBottom: 16, color: 'var(--text-dim)' }}>{sub}</p>
      {children}
    </div>
  )
}
