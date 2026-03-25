'use client'
import { useEffect, useState } from 'react'
import { getOpenClawStatus, spawnOpenClaw, stopOpenClaw, stopAllOpenClaw } from '@/lib/api'
import type { OpenClawInstance } from '@/lib/api'

export default function OpenClawPage() {
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
    refresh()
    const interval = setInterval(refresh, 3000)
    return () => clearInterval(interval)
  }, [])

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
          <h2 className="text-2xl font-semibold">OpenClaw</h2>
          <p className="text-sm mt-1" style={{ color: 'var(--text-dim)' }}>
            Spawn OpenClaw instances inside CLOVE sandbox. Each gets its own SOUL, budget, and permissions.
          </p>
        </div>
        <div className="flex gap-2">
          {running.length > 0 && (
            <button
              onClick={handleStopAll}
              className="px-4 py-2 rounded-md text-xs font-semibold"
              style={{ border: '1px solid var(--red)', color: 'var(--red)' }}
            >
              Stop All
            </button>
          )}
          <button
            onClick={() => setShowForm(!showForm)}
            className="px-4 py-2 rounded-md text-xs font-semibold"
            style={{ background: 'var(--accent)', color: '#000' }}
          >
            + Spawn Agent
          </button>
        </div>
      </div>

      {/* Spawn form */}
      {showForm && (
        <div
          className="border rounded-lg p-5 mb-6"
          style={{ borderColor: 'var(--accent)', background: 'var(--bg-card)' }}
        >
          <div className="grid grid-cols-2 gap-4 mb-4">
            <div>
              <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>Name</label>
              <input
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="researcher"
                className="w-full rounded-md px-3 py-2 text-sm outline-none placeholder:opacity-20"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
              />
            </div>
            <div>
              <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>Budget (USD)</label>
              <input
                type="number"
                value={budget}
                onChange={(e) => setBudget(parseFloat(e.target.value) || 10)}
                className="w-full rounded-md px-3 py-2 text-sm mono outline-none"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--accent)' }}
              />
            </div>
          </div>
          <div className="mb-4">
            <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>SOUL (personality)</label>
            <textarea
              value={soul}
              onChange={(e) => setSoul(e.target.value)}
              placeholder="You are a research assistant. You find information and summarize clearly."
              rows={2}
              className="w-full rounded-md px-3 py-2 text-sm resize-none outline-none placeholder:opacity-20"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
            />
          </div>
          <div className="grid grid-cols-2 gap-4 mb-4">
            <div>
              <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>Channels (comma-separated)</label>
              <input
                value={channels}
                onChange={(e) => setChannels(e.target.value)}
                placeholder="slack, telegram"
                className="w-full rounded-md px-3 py-2 text-sm outline-none placeholder:opacity-20"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
              />
            </div>
            <div>
              <label className="block text-xs mb-1.5" style={{ color: 'var(--text-dim)' }}>Skills (comma-separated)</label>
              <input
                value={skills}
                onChange={(e) => setSkills(e.target.value)}
                placeholder="research, web, coding"
                className="w-full rounded-md px-3 py-2 text-sm outline-none placeholder:opacity-20"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
              />
            </div>
          </div>
          <button
            onClick={handleSpawn}
            disabled={spawning || !name.trim()}
            className="px-6 py-2 rounded-md text-sm font-semibold disabled:opacity-30"
            style={{ background: 'var(--accent)', color: '#000' }}
          >
            {spawning ? 'Spawning...' : 'Spawn'}
          </button>
        </div>
      )}

      {/* Instances */}
      {instances.length === 0 ? (
        <div
          className="border rounded-lg p-12 text-center"
          style={{ borderColor: 'var(--border)', background: 'var(--bg-card)' }}
        >
          <div className="text-3xl mb-2" style={{ color: 'var(--text-dim)' }}>@</div>
          <p style={{ color: 'var(--text-dim)' }}>No OpenClaw instances running.</p>
          <p className="text-xs mt-2" style={{ color: 'var(--text-dim)' }}>
            Click &quot;+ Spawn Agent&quot; to create one.
          </p>
        </div>
      ) : (
        <div className="grid grid-cols-2 gap-4">
          {instances.map((inst) => (
            <div
              key={inst.id}
              className="border rounded-lg p-5"
              style={{
                borderColor: inst.state === 'running' ? 'var(--green)' : 'var(--border)',
                borderLeftWidth: 3,
                background: 'var(--bg-card)',
              }}
            >
              <div className="flex items-center justify-between mb-3">
                <div className="flex items-center gap-2">
                  <span className="text-lg font-semibold">{inst.name}</span>
                  <span
                    className="text-xs px-2 py-0.5 rounded"
                    style={{
                      background: inst.state === 'running' ? 'var(--green)' : 'var(--red)',
                      color: '#000',
                    }}
                  >
                    {inst.state}
                  </span>
                </div>
                {inst.state === 'running' && (
                  <button
                    onClick={() => handleStop(inst.id)}
                    className="text-xs px-3 py-1 rounded"
                    style={{ border: '1px solid var(--red)', color: 'var(--red)' }}
                  >
                    Stop
                  </button>
                )}
              </div>
              <div className="grid grid-cols-2 gap-x-6 gap-y-1 text-xs" style={{ color: 'var(--text-dim)' }}>
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
              <div className="text-xs mt-2" style={{ color: 'var(--text-dim)' }}>
                Started {new Date(inst.started_at_ms).toLocaleString()}
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
