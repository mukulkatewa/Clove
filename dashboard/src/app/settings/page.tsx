'use client'
import { useEffect, useState } from 'react'
import { getSchedules, createSchedule, deleteSchedule, getWebhooks, createWebhook, deleteWebhook } from '@/lib/api'
import type { Schedule, Webhook } from '@/lib/api'

export default function SettingsPage() {
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

  useEffect(() => { refresh() }, [])

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

  return (
    <div>
      <h2 className="text-2xl font-semibold mb-8">Settings</h2>

      {/* Schedules */}
      <Section title="Schedules" sub="Recurring agent runs on a cron schedule.">
        <div className="grid grid-cols-3 gap-3 mb-3">
          <input value={schName} onChange={(e) => setSchName(e.target.value)} placeholder="Name" className="input-field" />
          <input value={schCron} onChange={(e) => setSchCron(e.target.value)} placeholder="0 9 * * MON" className="input-field mono" />
          <input value={schGoal} onChange={(e) => setSchGoal(e.target.value)} placeholder="Goal" className="input-field" />
        </div>
        <div className="flex gap-3 mb-4">
          <input type="number" value={schBudget} onChange={(e) => setSchBudget(parseFloat(e.target.value) || 5)} className="input-field w-28 mono" />
          <input type="number" value={schAgents} onChange={(e) => setSchAgents(parseInt(e.target.value) || 3)} className="input-field w-20 mono" />
          <button onClick={handleCreateSchedule} className="btn-primary">Add Schedule</button>
        </div>
        {schedules.map((s) => (
          <div key={s.name} className="flex items-center justify-between py-2 border-t" style={{ borderColor: 'var(--border)' }}>
            <div>
              <span className="mono text-sm" style={{ color: 'var(--accent)' }}>{s.name}</span>
              <span className="text-xs ml-3" style={{ color: 'var(--text-dim)' }}>{s.cron}</span>
              <span className="text-xs ml-3" style={{ color: 'var(--text-dim)' }}>{s.run.goal}</span>
            </div>
            <button onClick={() => { deleteSchedule(s.name); refresh() }} className="text-xs" style={{ color: 'var(--red)' }}>Delete</button>
          </div>
        ))}
      </Section>

      {/* Webhooks */}
      <Section title="Webhooks" sub="Get notified when events happen.">
        <div className="flex gap-3 mb-4">
          <input value={whUrl} onChange={(e) => setWhUrl(e.target.value)} placeholder="https://hooks.slack.com/..." className="input-field flex-1" />
          <input value={whEvents} onChange={(e) => setWhEvents(e.target.value)} placeholder="run_complete,budget_exceeded" className="input-field w-64 mono" />
          <button onClick={handleCreateWebhook} className="btn-primary">Add Webhook</button>
        </div>
        {webhooks.map((w) => (
          <div key={w.id} className="flex items-center justify-between py-2 border-t" style={{ borderColor: 'var(--border)' }}>
            <div>
              <span className="text-sm truncate">{w.url}</span>
              <span className="text-xs ml-3 mono" style={{ color: 'var(--text-dim)' }}>{w.events.join(', ')}</span>
            </div>
            <button onClick={() => { deleteWebhook(w.id); refresh() }} className="text-xs" style={{ color: 'var(--red)' }}>Delete</button>
          </div>
        ))}
      </Section>

      <style jsx>{`
        .input-field {
          background: var(--bg);
          border: 1px solid var(--border);
          color: var(--text);
          border-radius: 0.375rem;
          padding: 0.5rem 0.75rem;
          font-size: 0.875rem;
          outline: none;
        }
        .input-field::placeholder { opacity: 0.2; }
        .btn-primary {
          background: var(--accent);
          color: #000;
          border-radius: 0.375rem;
          padding: 0.5rem 1rem;
          font-size: 0.75rem;
          font-weight: 600;
          white-space: nowrap;
        }
      `}</style>
    </div>
  )
}

function Section({ title, sub, children }: { title: string; sub: string; children: React.ReactNode }) {
  return (
    <div className="border rounded-lg p-5 mb-6" style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}>
      <h3 className="text-sm font-semibold mb-1">{title}</h3>
      <p className="text-xs mb-4" style={{ color: 'var(--text-dim)' }}>{sub}</p>
      {children}
    </div>
  )
}
