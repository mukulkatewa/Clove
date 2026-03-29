'use client'
import { useEffect, useState } from 'react'
import { getMemoryBlocks, searchMemory } from '@/lib/api'
import type { MemoryBlock } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoMemory } from '@/lib/demo-data'

const TYPE_COLORS: Record<string, { bg: string; fg: string }> = {
  system: { bg: 'var(--red-light)', fg: 'var(--red)' },
  core: { bg: 'var(--yellow-light)', fg: 'var(--yellow)' },
  recall: { bg: 'var(--blue-light)', fg: 'var(--blue)' },
}

export default function MemoryPage() {
  const { isDemo } = useDemo()
  const [blocks, setBlocks] = useState<MemoryBlock[]>([])
  const [search, setSearch] = useState('')
  const [filter, setFilter] = useState('all')
  const [expanded, setExpanded] = useState<string | null>(null)

  const refresh = () => { getMemoryBlocks().then(r => setBlocks(r.blocks || [])).catch(() => {}) }
  useEffect(() => { if (isDemo) { setBlocks(demoMemory() as MemoryBlock[]); return }; refresh(); const i = setInterval(refresh, 5000); return () => clearInterval(i) }, [isDemo])

  const handleSearch = async () => {
    if (!search.trim()) { refresh(); return }
    try { const r = await searchMemory(search.trim()); setBlocks(r.blocks || []) } catch {}
  }

  const filtered = filter === 'all' ? blocks : blocks.filter(b => b.type === filter)
  const counts: Record<string, number> = { all: blocks.length, system: 0, core: 0, recall: 0 }
  blocks.forEach(b => { if (b.type in counts) counts[b.type]++ })

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[24px] font-semibold tracking-[-0.03em]">Memory</h2>
          <p className="text-[14px] mt-1" style={{ color: 'var(--text-dim)' }}>Agent memory blocks — system prompts, core facts, episodic recall</p>
        </div>
        <span className="text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{blocks.length} blocks</span>
      </div>

      <div className="flex items-center gap-3 mb-6">
        <input value={search} onChange={e => setSearch(e.target.value)} placeholder="Search memory..."
          className="flex-1 rounded-xl px-4 py-2.5 text-[13px] outline-none placeholder:opacity-25"
          style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text)' }}
          onKeyDown={e => { if (e.key === 'Enter') handleSearch() }} />
        <div className="flex gap-1">
          {(['all', 'system', 'core', 'recall'] as const).map(t => (
            <button key={t} onClick={() => setFilter(t)}
              className="px-3 py-2 rounded-lg text-[12px] font-medium capitalize"
              style={{ background: filter === t ? 'var(--bg-raised)' : 'transparent', color: filter === t ? 'var(--text)' : 'var(--text-dim)' }}>
              {t} <span className="tabular-nums ml-0.5">{counts[t]}</span>
            </button>
          ))}
        </div>
      </div>

      {filtered.length === 0 ? (
        <div className="card py-16 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>
          {blocks.length === 0 ? 'No memory blocks yet' : 'No matches'}
        </div>
      ) : (
        <div className="space-y-2">
          {filtered.map(block => {
            const tc = TYPE_COLORS[block.type] || { bg: 'var(--bg-raised)', fg: 'var(--text-dim)' }
            const isOpen = expanded === block.id
            return (
              <div key={block.id} className="card overflow-hidden">
                <button onClick={() => setExpanded(isOpen ? null : block.id)}
                  className="w-full flex items-center gap-3 px-5 py-4 text-left transition-colors hover:bg-[var(--bg)]">
                  <span className="text-[13px] font-medium flex-1">{block.name}</span>
                  <span className="text-[10px] font-semibold uppercase tracking-[0.06em] px-2 py-[3px] rounded-full" style={{ background: tc.bg, color: tc.fg }}>{block.type}</span>
                  <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{block.access}</span>
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ color: 'var(--text-dim)', transform: isOpen ? 'rotate(180deg)' : '', transition: '150ms' }}><polyline points="6 9 12 15 18 9"/></svg>
                </button>
                {isOpen && (
                  <div className="px-5 pb-4">
                    <div className="rounded-xl p-4 text-[12px] leading-relaxed whitespace-pre-wrap mono" style={{ background: 'var(--bg)', color: 'var(--text-secondary)' }}>
                      {block.content || '(empty)'}
                    </div>
                    <div className="text-[11px] mt-2" style={{ color: 'var(--text-dim)' }}>ID: {block.id}</div>
                  </div>
                )}
              </div>
            )
          })}
        </div>
      )}
    </div>
  )
}
