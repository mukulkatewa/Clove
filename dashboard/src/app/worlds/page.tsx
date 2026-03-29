'use client'
import { useEffect, useState } from 'react'
import { getWorlds, createWorld, deleteWorld } from '@/lib/api'
import type { WorldSummary } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoWorlds } from '@/lib/demo-data'

export default function WorldsPage() {
  const { isDemo } = useDemo()
  const [worlds, setWorlds] = useState<WorldSummary[]>([])
  const [name, setName] = useState('')
  const [creating, setCreating] = useState(false)

  const refresh = () => { getWorlds().then(r => setWorlds(r.worlds || [])).catch(() => {}) }
  useEffect(() => { if (isDemo) { setWorlds(demoWorlds() as WorldSummary[]); return }; refresh(); const i = setInterval(refresh, 3000); return () => clearInterval(i) }, [isDemo])

  const handleCreate = async () => {
    if (!name.trim()) return; setCreating(true)
    try { await createWorld(name.trim()); setName(''); refresh() } catch {}
    setCreating(false)
  }

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[24px] font-semibold tracking-[-0.03em]">Worlds</h2>
          <p className="text-[14px] mt-1" style={{ color: 'var(--text-dim)' }}>Isolated execution environments for multi-tenant agent simulations</p>
        </div>
        <span className="text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{worlds.length} worlds</span>
      </div>

      <div className="card p-5 mb-6">
        <div className="flex gap-3">
          <input value={name} onChange={e => setName(e.target.value)} placeholder="World name"
            className="flex-1 rounded-xl px-4 py-2.5 text-[13px] outline-none placeholder:opacity-25"
            style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
            onKeyDown={e => { if (e.key === 'Enter') handleCreate() }} />
          <button onClick={handleCreate} disabled={creating || !name.trim()}
            className="px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white disabled:opacity-30"
            style={{ background: 'var(--accent)' }}>Create World</button>
        </div>
      </div>

      {worlds.length === 0 ? (
        <div className="card py-20 text-center">
          <p className="text-[14px] font-medium mb-1">No worlds yet</p>
          <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>Worlds provide isolated state, events, and agent membership</p>
        </div>
      ) : (
        <div className="grid grid-cols-2 gap-4">
          {worlds.map(w => (
            <div key={w.id} className="card p-5 group relative">
              <div className="flex items-center justify-between mb-4">
                <div className="flex items-center gap-2.5">
                  <div className="w-[8px] h-[8px] rounded-full" style={{ background: w.member_count > 0 ? 'var(--green)' : 'var(--border)' }} />
                  <span className="text-[15px] font-semibold">{w.name}</span>
                </div>
                <button onClick={() => { deleteWorld(w.id); refresh() }} className="text-[12px] font-medium opacity-0 group-hover:opacity-100 transition-opacity" style={{ color: 'var(--red)' }}>Destroy</button>
              </div>
              <div className="grid grid-cols-3 gap-4">
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>ID</div>
                  <div className="text-[13px] mono tabular-nums">{w.id}</div>
                </div>
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Members</div>
                  <div className="text-[13px] font-semibold" style={{ color: w.member_count > 0 ? 'var(--green)' : 'var(--text-dim)' }}>{w.member_count}</div>
                </div>
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>State</div>
                  <div className="text-[13px]">{Object.keys(w.metadata || {}).length} keys</div>
                </div>
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
