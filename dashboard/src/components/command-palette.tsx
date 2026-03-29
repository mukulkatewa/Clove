'use client'
import { useState, useEffect, useRef, useCallback } from 'react'
import { useRouter } from 'next/navigation'
import { useDemo } from '@/lib/demo-context'

interface Cmd { id: string; label: string; sub?: string; action: () => void; section: string }

export function CommandPalette() {
  const [open, setOpen] = useState(false)
  const [query, setQuery] = useState('')
  const [sel, setSel] = useState(0)
  const inputRef = useRef<HTMLInputElement>(null)
  const router = useRouter()
  const { isDemo, toggle } = useDemo()

  const cmds: Cmd[] = [
    { id: 'n-dash', label: 'Dashboard', sub: 'Overview', action: () => router.push('/'), section: 'Navigate' },
    { id: 'n-sand', label: 'Sandbox', sub: 'Agent permissions', action: () => router.push('/agents'), section: 'Navigate' },
    { id: 'n-world', label: 'Worlds', sub: 'Isolated environments', action: () => router.push('/worlds'), section: 'Navigate' },
    { id: 'n-runs', label: 'Runs', sub: 'Submit goals', action: () => router.push('/runs'), section: 'Navigate' },
    { id: 'n-fleet', label: 'Fleet', sub: 'Parallel agents', action: () => router.push('/fleet'), section: 'Navigate' },
    { id: 'n-oc', label: 'OpenClaw', sub: 'Sandboxed instances', action: () => router.push('/openclaw'), section: 'Navigate' },
    { id: 'n-mem', label: 'Memory', sub: 'Agent memory blocks', action: () => router.push('/memory'), section: 'Navigate' },
    { id: 'n-aud', label: 'Audit', sub: 'Event log', action: () => router.push('/audit'), section: 'Navigate' },
    { id: 'n-cost', label: 'Cost', sub: 'Spend tracking', action: () => router.push('/cost'), section: 'Navigate' },
    { id: 'n-set', label: 'Settings', sub: 'Schedules & webhooks', action: () => router.push('/settings'), section: 'Navigate' },
    { id: 'a-run', label: 'New Run', sub: 'Submit a goal', action: () => router.push('/runs'), section: 'Actions' },
    { id: 'a-fleet', label: 'Launch Fleet', sub: 'Parallel agents', action: () => router.push('/fleet'), section: 'Actions' },
    { id: 'a-spawn', label: 'Spawn OpenClaw', sub: 'New instance', action: () => router.push('/openclaw'), section: 'Actions' },
    { id: 'a-world', label: 'Create World', sub: 'New environment', action: () => router.push('/worlds'), section: 'Actions' },
    { id: 'a-demo', label: isDemo ? 'Exit Demo' : 'Demo Mode', sub: 'Toggle mock data', action: () => { toggle(); setOpen(false) }, section: 'Actions' },
  ]

  const filtered = query ? cmds.filter(c => c.label.toLowerCase().includes(query.toLowerCase()) || (c.sub||'').toLowerCase().includes(query.toLowerCase())) : cmds
  const sections = [...new Set(filtered.map(c => c.section))]

  useEffect(() => {
    const h = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key === 'k') { e.preventDefault(); setOpen(v => !v); setQuery(''); setSel(0) }
      if (e.key === 'Escape') setOpen(false)
    }
    window.addEventListener('keydown', h); return () => window.removeEventListener('keydown', h)
  }, [])

  useEffect(() => { if (open) inputRef.current?.focus() }, [open])
  useEffect(() => { setSel(0) }, [query])

  const exec = useCallback((c: Cmd) => { setOpen(false); setQuery(''); c.action() }, [])

  const onKey = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowDown') { e.preventDefault(); setSel(s => Math.min(s + 1, filtered.length - 1)) }
    if (e.key === 'ArrowUp') { e.preventDefault(); setSel(s => Math.max(s - 1, 0)) }
    if (e.key === 'Enter' && filtered[sel]) exec(filtered[sel])
  }

  if (!open) return null
  let idx = -1

  return (
    <div className="fixed inset-0 z-[100] flex items-start justify-center pt-[18vh]" onClick={() => setOpen(false)}>
      <div className="absolute inset-0" style={{ background: 'rgba(0,0,0,0.25)', backdropFilter: 'blur(4px)' }} />
      <div className="relative w-[520px] rounded-2xl overflow-hidden" style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', boxShadow: '0 20px 60px rgba(0,0,0,0.15)' }} onClick={e => e.stopPropagation()}>
        <div className="flex items-center gap-3 px-4" style={{ borderBottom: '1px solid var(--border)' }}>
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ color: 'var(--text-dim)' }}><circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/></svg>
          <input ref={inputRef} value={query} onChange={e => setQuery(e.target.value)} onKeyDown={onKey}
            placeholder="Type a command..." className="w-full py-3.5 text-[14px] bg-transparent outline-none placeholder:opacity-30" style={{ color: 'var(--text)' }} />
          <kbd className="text-[10px] px-1.5 py-0.5 rounded" style={{ background: 'var(--bg)', color: 'var(--text-dim)', border: '1px solid var(--border)' }}>esc</kbd>
        </div>
        <div className="max-h-[340px] overflow-y-auto py-2">
          {filtered.length === 0 ? <div className="py-8 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No results</div> :
          sections.map(sec => (
            <div key={sec}>
              <div className="px-4 py-1.5 text-[10px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>{sec}</div>
              {filtered.filter(c => c.section === sec).map(cmd => {
                idx++; const isSel = idx === sel; const i = idx
                return <button key={cmd.id} onClick={() => exec(cmd)} onMouseEnter={() => setSel(i)}
                  className="w-full flex items-center gap-3 px-4 py-2.5 text-left transition-colors"
                  style={{ background: isSel ? 'var(--bg)' : 'transparent' }}>
                  <span className="text-[13px] font-medium">{cmd.label}</span>
                  {cmd.sub && <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>{cmd.sub}</span>}
                </button>
              })}
            </div>
          ))}
        </div>
      </div>
    </div>
  )
}
