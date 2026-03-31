'use client'
import { useEffect, useState } from 'react'
import { getAgents } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'

interface Agent { id: number; name: string; state: string }
interface Perms {
  can_exec: boolean; can_read: boolean; can_write: boolean; can_think: boolean; can_spawn: boolean; can_http: boolean
  allowed_read_paths: string[]; allowed_write_paths: string[]; allowed_domains: string[]; blocked_commands: string[]
  max_exec_time_ms: number
}

const DEMO_AGENTS: Agent[] = [
  { id: 1, name: 'researcher-alpha', state: 'running' },
  { id: 2, name: 'writer-bravo', state: 'running' },
  { id: 3, name: 'fixer-charlie', state: 'running' },
]
const DEMO_PERMS: Record<number, Perms> = {
  1: { can_exec: false, can_read: true, can_write: false, can_think: true, can_spawn: false, can_http: true, allowed_read_paths: ['/tmp', '~/Documents'], allowed_write_paths: [], allowed_domains: ['api.openai.com', 'arxiv.org', 'github.com'], blocked_commands: [], max_exec_time_ms: 30000 },
  2: { can_exec: false, can_read: true, can_write: true, can_think: true, can_spawn: false, can_http: false, allowed_read_paths: ['/tmp'], allowed_write_paths: ['/tmp/output'], allowed_domains: [], blocked_commands: [], max_exec_time_ms: 30000 },
  3: { can_exec: true, can_read: true, can_write: true, can_think: true, can_spawn: true, can_http: true, allowed_read_paths: ['/app', '/tmp'], allowed_write_paths: ['/app/config', '/tmp'], allowed_domains: ['api.github.com', 'grafana.internal'], blocked_commands: ['rm -rf', 'shutdown'], max_exec_time_ms: 60000 },
}

const API = process.env.NEXT_PUBLIC_API_URL || ''
const CAPS = [
  { key: 'can_read', label: 'Read Files', desc: 'Access filesystem for reading' },
  { key: 'can_write', label: 'Write Files', desc: 'Create or modify files' },
  { key: 'can_exec', label: 'Shell Exec', desc: 'Run shell commands' },
  { key: 'can_http', label: 'HTTP', desc: 'Make network requests' },
  { key: 'can_think', label: 'LLM Access', desc: 'Call language models' },
  { key: 'can_spawn', label: 'Spawn Agents', desc: 'Create sub-agents' },
]

export default function PermissionsPage() {
  const { isDemo } = useDemo()
  const [agents, setAgents] = useState<Agent[]>([])
  const [selected, setSelected] = useState<number | null>(null)
  const [perms, setPerms] = useState<Perms | null>(null)
  const [saving, setSaving] = useState(false)
  const [newPath, setNewPath] = useState('')
  const [newDomain, setNewDomain] = useState('')

  useEffect(() => {
    if (isDemo) { setAgents(DEMO_AGENTS); return }
    getAgents().then(a => setAgents(a || [])).catch(() => {})
  }, [isDemo])

  useEffect(() => {
    if (!selected) { setPerms(null); return }
    if (isDemo) { setPerms(DEMO_PERMS[selected] || null); return }
    fetch(`${API}/api/agents/${selected}/permissions`).then(r => r.json()).then(setPerms).catch(() => {})
  }, [selected, isDemo])

  const toggleCap = (key: string) => {
    if (!perms) return
    setPerms({ ...perms, [key]: !(perms as unknown as Record<string, unknown>)[key] })
  }

  const save = async () => {
    if (!selected || !perms || isDemo) return
    setSaving(true)
    await fetch(`${API}/api/agents/${selected}/permissions`, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(perms) }).catch(() => {})
    setSaving(false)
  }

  const addPath = (type: 'read' | 'write') => {
    if (!perms || !newPath.trim()) return
    const key = type === 'read' ? 'allowed_read_paths' : 'allowed_write_paths'
    setPerms({ ...perms, [key]: [...perms[key], newPath.trim()] })
    setNewPath('')
  }

  const addDomain = () => {
    if (!perms || !newDomain.trim()) return
    setPerms({ ...perms, allowed_domains: [...perms.allowed_domains, newDomain.trim()] })
    setNewDomain('')
  }

  return (
    <div className="flex gap-0 -mx-10 -my-8 h-[calc(100vh)]">
      {/* Agent list */}
      <div className="w-[240px] flex-shrink-0 overflow-y-auto py-6 px-4" style={{ borderRight: '1px solid var(--border)' }}>
        <div className="text-[10px] uppercase tracking-[0.06em] font-semibold mb-3 px-2" style={{ color: 'var(--text-dim)' }}>Select Agent</div>
        {agents.map(a => (
          <button key={a.id} onClick={() => setSelected(a.id)}
            className="w-full flex items-center gap-2.5 px-3 py-2.5 rounded-lg text-left mb-1 transition-all"
            style={{ background: selected === a.id ? 'var(--accent-light)' : 'transparent', color: selected === a.id ? 'var(--accent)' : 'var(--text-secondary)' }}>
            <div className={`w-2 h-2 rounded-full ${a.state === 'running' ? 'animate-pulse' : ''}`} style={{ background: a.state === 'running' ? 'var(--green)' : 'var(--text-dim)' }} />
            <span className="text-[12px] font-medium">{a.name}</span>
          </button>
        ))}
      </div>

      {/* Permissions editor */}
      <div className="flex-1 overflow-y-auto py-6 px-8">
        {!selected || !perms ? (
          <div className="text-center py-20">
            <div className="text-[15px] font-semibold mb-1">Sandbox Permissions</div>
            <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>Select an agent to view and edit its sandbox permissions.</p>
          </div>
        ) : (
          <>
            <div className="flex items-center justify-between mb-6">
              <div>
                <h2 className="text-[22px] font-semibold tracking-[-0.03em]">{agents.find(a => a.id === selected)?.name}</h2>
                <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>Sandbox permissions — what this agent can and cannot do</p>
              </div>
              <button onClick={save} disabled={saving || isDemo} className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white disabled:opacity-30" style={{ background: 'var(--accent)' }}>
                {saving ? 'Saving...' : 'Save'}
              </button>
            </div>

            {/* Capabilities */}
            <div className="card p-5 mb-4">
              <div className="text-[13px] font-semibold mb-3">Capabilities</div>
              <div className="grid grid-cols-2 gap-2">
                {CAPS.map(cap => {
                  const on = (perms as unknown as Record<string, unknown>)[cap.key] as boolean
                  return (
                    <button key={cap.key} onClick={() => toggleCap(cap.key)}
                      className="flex items-center gap-3 px-4 py-3 rounded-xl text-left transition-all"
                      style={{ background: on ? 'var(--green-light)' : 'var(--bg)', border: `1px solid ${on ? 'var(--green)' : 'var(--border)'}` }}>
                      <div className="w-5 h-5 rounded-md flex items-center justify-center text-[11px]"
                        style={{ background: on ? 'var(--green)' : 'var(--border)', color: on ? '#fff' : 'var(--text-dim)' }}>
                        {on ? '✓' : ''}
                      </div>
                      <div>
                        <div className="text-[12px] font-semibold" style={{ color: on ? 'var(--green)' : 'var(--text-dim)' }}>{cap.label}</div>
                        <div className="text-[10px]" style={{ color: 'var(--text-dim)' }}>{cap.desc}</div>
                      </div>
                    </button>
                  )
                })}
              </div>
            </div>

            {/* Filesystem */}
            <div className="card p-5 mb-4">
              <div className="text-[13px] font-semibold mb-3">Filesystem ACL</div>
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Read Paths</div>
                  {perms.allowed_read_paths.map((p, i) => (
                    <div key={i} className="flex items-center gap-2 mb-1">
                      <span className="text-[12px] mono flex-1 px-2 py-1 rounded" style={{ background: 'var(--bg)', color: 'var(--green)' }}>{p}</span>
                      <button onClick={() => setPerms({ ...perms, allowed_read_paths: perms.allowed_read_paths.filter((_, j) => j !== i) })} className="text-[10px]" style={{ color: 'var(--red)' }}>✕</button>
                    </div>
                  ))}
                  <div className="flex gap-1 mt-2">
                    <input value={newPath} onChange={e => setNewPath(e.target.value)} placeholder="/path" className="flex-1 rounded-lg px-2 py-1 text-[11px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                    <button onClick={() => addPath('read')} className="px-2 py-1 rounded-lg text-[10px] font-medium" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>+</button>
                  </div>
                </div>
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Write Paths</div>
                  {perms.allowed_write_paths.map((p, i) => (
                    <div key={i} className="flex items-center gap-2 mb-1">
                      <span className="text-[12px] mono flex-1 px-2 py-1 rounded" style={{ background: 'var(--bg)', color: 'var(--yellow)' }}>{p}</span>
                      <button onClick={() => setPerms({ ...perms, allowed_write_paths: perms.allowed_write_paths.filter((_, j) => j !== i) })} className="text-[10px]" style={{ color: 'var(--red)' }}>✕</button>
                    </div>
                  ))}
                  <div className="flex gap-1 mt-2">
                    <input value={newPath} onChange={e => setNewPath(e.target.value)} placeholder="/path" className="flex-1 rounded-lg px-2 py-1 text-[11px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                    <button onClick={() => addPath('write')} className="px-2 py-1 rounded-lg text-[10px] font-medium" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>+</button>
                  </div>
                </div>
              </div>
            </div>

            {/* Network */}
            <div className="card p-5 mb-4">
              <div className="text-[13px] font-semibold mb-3">Network ACL</div>
              <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Allowed Domains</div>
              <div className="flex flex-wrap gap-1.5 mb-2">
                {perms.allowed_domains.map((d, i) => (
                  <span key={i} className="flex items-center gap-1 text-[11px] mono px-2 py-1 rounded-lg" style={{ background: 'var(--green-light)', color: 'var(--green)' }}>
                    {d}
                    <button onClick={() => setPerms({ ...perms, allowed_domains: perms.allowed_domains.filter((_, j) => j !== i) })} className="text-[9px] ml-1" style={{ color: 'var(--red)' }}>✕</button>
                  </span>
                ))}
                {perms.allowed_domains.length === 0 && <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>No restrictions (all domains allowed)</span>}
              </div>
              <div className="flex gap-1">
                <input value={newDomain} onChange={e => setNewDomain(e.target.value)} placeholder="api.example.com" className="w-48 rounded-lg px-2 py-1 text-[11px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
                  onKeyDown={e => { if (e.key === 'Enter') addDomain() }} />
                <button onClick={addDomain} className="px-2 py-1 rounded-lg text-[10px] font-medium" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>+</button>
              </div>
            </div>

            {/* Blocked commands */}
            <div className="card p-5">
              <div className="text-[13px] font-semibold mb-3">Blocked Commands</div>
              <div className="flex flex-wrap gap-1.5">
                {perms.blocked_commands.map((cmd, i) => (
                  <span key={i} className="flex items-center gap-1 text-[11px] mono px-2 py-1 rounded-lg" style={{ background: 'var(--red-light)', color: 'var(--red)' }}>
                    {cmd}
                    <button onClick={() => setPerms({ ...perms, blocked_commands: perms.blocked_commands.filter((_, j) => j !== i) })} className="text-[9px] ml-1">✕</button>
                  </span>
                ))}
                {perms.blocked_commands.length === 0 && <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>No blocked commands</span>}
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  )
}
