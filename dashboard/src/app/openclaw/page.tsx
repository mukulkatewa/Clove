'use client'
import { useEffect, useState } from 'react'
import { getOpenClawStatus, spawnOpenClaw, stopOpenClaw, stopAllOpenClaw } from '@/lib/api'
import type { OpenClawInstance } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoOpenClaw } from '@/lib/demo-data'

export default function OpenClawPage() {
  const { isDemo } = useDemo()
  const [instances, setInstances] = useState<OpenClawInstance[]>([])
  const [showForm, setShowForm] = useState(false)
  const [name, setName] = useState('')
  const [soul, setSoul] = useState('')
  const [budget, setBudget] = useState(10)
  const [channels, setChannels] = useState('')
  const [skills, setSkills] = useState('')
  const [spawning, setSpawning] = useState(false)

  const refresh = () => {
    getOpenClawStatus().then((r) => setInstances(r.instances || [])).catch(() => {})
  }

  useEffect(() => {
    if (isDemo) { setInstances(demoOpenClaw() as any); return }
    refresh()
    const interval = setInterval(refresh, 3000)
    return () => clearInterval(interval)
  }, [isDemo])

  const handleSpawn = async () => {
    if (!name.trim()) return
    setSpawning(true)
    try {
      await spawnOpenClaw({
        name: name.trim(),
        soul: soul.trim() || undefined,
        budget_usd: budget,
        channels: channels ? channels.split(',').map((s) => s.trim()) : undefined,
        skills: skills ? skills.split(',').map((s) => s.trim()) : undefined,
      })
      refresh()
      setShowForm(false)
      setName('')
      setSoul('')
    } catch {}
    setSpawning(false)
  }

  const handleStop = async (id: string) => {
    await stopOpenClaw(id)
    refresh()
  }

  const handleStopAll = async () => {
    await stopAllOpenClaw()
    refresh()
  }

  const running = instances.filter((i) => i.state === 'running')

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 style={{ fontSize: 24, fontWeight: 600, color: 'var(--text)' }}>OpenClaw</h2>
          <p style={{ fontSize: 14, marginTop: 4, color: 'var(--text-secondary)' }}>
            Spawn OpenClaw instances inside CLOVE sandbox. Each gets its own SOUL, budget, and permissions.
          </p>
        </div>
        <div className="flex gap-2">
          {running.length > 0 && (
            <button
              onClick={handleStopAll}
              style={{
                padding: '8px 16px',
                borderRadius: 12,
                fontSize: 12,
                fontWeight: 600,
                border: '1px solid var(--red)',
                color: 'var(--red)',
                background: 'var(--red-light)',
              }}
            >
              Stop All
            </button>
          )}
          <button
            onClick={() => setShowForm(!showForm)}
            style={{
              padding: '8px 16px',
              borderRadius: 12,
              fontSize: 12,
              fontWeight: 600,
              background: 'var(--accent)',
              color: '#fff',
            }}
          >
            + Spawn Agent
          </button>
        </div>
      </div>

      {/* Spawn form */}
      {showForm && (
        <div className="card" style={{ padding: 20, marginBottom: 24, borderColor: 'var(--accent)' }}>
          <div className="grid grid-cols-2 gap-4 mb-4">
            <div>
              <label className="block" style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 6 }}>Name</label>
              <input
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="researcher"
                className="w-full text-sm outline-none placeholder:opacity-20"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--text)' }}
              />
            </div>
            <div>
              <label className="block" style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 6 }}>Budget (USD)</label>
              <input
                type="number"
                value={budget}
                onChange={(e) => setBudget(parseFloat(e.target.value) || 10)}
                className="w-full mono text-sm outline-none"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--accent)' }}
              />
            </div>
          </div>
          <div className="mb-4">
            <label className="block" style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 6 }}>SOUL (personality)</label>
            <textarea
              value={soul}
              onChange={(e) => setSoul(e.target.value)}
              placeholder="You are a research assistant. You find information and summarize clearly."
              rows={2}
              className="w-full text-sm resize-none outline-none placeholder:opacity-20"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--text)' }}
            />
          </div>
          <div className="grid grid-cols-2 gap-4 mb-4">
            <div>
              <label className="block" style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 6 }}>Channels (comma-separated)</label>
              <input
                value={channels}
                onChange={(e) => setChannels(e.target.value)}
                placeholder="slack, telegram"
                className="w-full text-sm outline-none placeholder:opacity-20"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--text)' }}
              />
            </div>
            <div>
              <label className="block" style={{ fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.05em', color: 'var(--text-dim)', marginBottom: 6 }}>Skills (comma-separated)</label>
              <input
                value={skills}
                onChange={(e) => setSkills(e.target.value)}
                placeholder="research, web, coding"
                className="w-full text-sm outline-none placeholder:opacity-20"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', borderRadius: 12, padding: '8px 12px', color: 'var(--text)' }}
              />
            </div>
          </div>
          <button
            onClick={handleSpawn}
            disabled={spawning || !name.trim()}
            className="text-sm font-semibold disabled:opacity-30"
            style={{ background: 'var(--accent)', color: '#fff', borderRadius: 12, padding: '8px 24px' }}
          >
            {spawning ? 'Spawning...' : 'Spawn'}
          </button>
        </div>
      )}

      {/* Instances */}
      {instances.length === 0 ? (
        <div className="card" style={{ padding: 48, textAlign: 'center' }}>
          <div style={{ fontSize: 30, marginBottom: 8, color: 'var(--text-dim)' }}>@</div>
          <p style={{ color: 'var(--text-dim)' }}>No OpenClaw instances running.</p>
          <p style={{ fontSize: 12, marginTop: 8, color: 'var(--text-dim)' }}>
            Click &quot;+ Spawn Agent&quot; to create one.
          </p>
        </div>
      ) : (
        <div className="grid grid-cols-2 gap-4">
          {instances.map((inst) => (
            <div
              key={inst.id}
              className="card"
              style={{
                padding: 20,
                borderLeft: `3px solid ${inst.state === 'running' ? 'var(--green)' : 'var(--border)'}`,
              }}
            >
              <div className="flex items-center justify-between mb-3">
                <div className="flex items-center gap-2">
                  <span style={{ fontSize: 18, fontWeight: 600, color: 'var(--text)' }}>{inst.name}</span>
                  <span
                    style={{
                      fontSize: 12,
                      padding: '2px 8px',
                      borderRadius: 8,
                      fontWeight: 500,
                      background: inst.state === 'running' ? 'var(--green-light)' : 'var(--red-light)',
                      color: inst.state === 'running' ? 'var(--green)' : 'var(--red)',
                    }}
                  >
                    {inst.state}
                  </span>
                </div>
                {inst.state === 'running' && (
                  <button
                    onClick={() => handleStop(inst.id)}
                    style={{
                      fontSize: 12,
                      padding: '4px 12px',
                      borderRadius: 12,
                      border: '1px solid var(--red)',
                      color: 'var(--red)',
                      background: 'var(--red-light)',
                    }}
                  >
                    Stop
                  </button>
                )}
              </div>
              <div className="grid grid-cols-2 gap-x-6 gap-y-1" style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                <div>ID: <span className="mono" style={{ color: 'var(--text)' }}>{inst.id}</span></div>
                <div>PID: <span className="mono" style={{ color: 'var(--text)' }}>{inst.pid}</span></div>
                <div>Port: <span className="mono" style={{ color: 'var(--text)' }}>{inst.port}</span></div>
                <div>
                  Budget: <span className="mono" style={{ color: 'var(--accent)' }}>${inst.budget_usd.toFixed(2)}</span>
                </div>
                <div>
                  Cost: <span className="mono" style={{ color: 'var(--accent)' }}>${inst.cost_usd.toFixed(4)}</span>
                </div>
                {inst.channels.length > 0 && (
                  <div>Channels: <span style={{ color: 'var(--text)' }}>{inst.channels.join(', ')}</span></div>
                )}
              </div>
              <div style={{ fontSize: 12, marginTop: 8, color: 'var(--text-dim)' }}>
                Started {new Date(inst.started_at_ms).toLocaleString()}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
