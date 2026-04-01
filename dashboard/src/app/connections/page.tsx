'use client'
import { useEffect, useState } from 'react'
import { useDemo } from '@/lib/demo-context'
import { SERVICES, SERVICE_CATEGORIES, type ServiceDef } from '@/lib/services'

const API = process.env.NEXT_PUBLIC_API_URL || ''

interface Connection { name: string; connected: boolean; mcp_active: boolean; tools: number }

export default function ConnectionsPage() {
  const { isDemo } = useDemo()
  const [connections, setConnections] = useState<Connection[]>([])
  const [category, setCategory] = useState('all')
  const [connecting, setConnecting] = useState<ServiceDef | null>(null)
  const [token, setToken] = useState('')
  const [saving, setSaving] = useState(false)
  const [success, setSuccess] = useState('')

  const refresh = () => {
    if (isDemo) {
      setConnections([
        { name: 'github', connected: true, mcp_active: true, tools: 12 },
        { name: 'slack', connected: true, mcp_active: true, tools: 8 },
        { name: 'filesystem', connected: true, mcp_active: true, tools: 5 },
        { name: 'postgres', connected: false, mcp_active: false, tools: 0 },
      ])
      return
    }
    fetch(`${API}/api/connections`).then(r => r.json()).then(d => setConnections(d.connections || [])).catch(() => {})
  }

  useEffect(() => { refresh() }, [isDemo])

  const isConnected = (serviceId: string) => connections.some(c => c.name === serviceId && c.connected)
  const getConnection = (serviceId: string) => connections.find(c => c.name === serviceId)

  const handleConnect = async (service: ServiceDef) => {
    if (service.auth.type === 'none') {
      // No auth needed — just configure MCP
      setSaving(true)
      await fetch(`${API}/api/connections/setup`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ service: service.id, mcp_package: service.mcp_package, extra_arg: token || '/tmp' }),
      }).catch(() => {})
      setSaving(false); setConnecting(null); setToken(''); setSuccess(service.name + ' connected!'); refresh()
      setTimeout(() => setSuccess(''), 3000)
      return
    }
    setConnecting(service); setToken('')
  }

  const handleSaveConnection = async () => {
    if (!connecting || !token.trim()) return
    setSaving(true)
    const res = await fetch(`${API}/api/connections/setup`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        service: connecting.id,
        token: token.trim(),
        mcp_package: connecting.mcp_package,
        env_var: connecting.auth.env_var,
      }),
    }).then(r => r.json()).catch(() => ({ error: 'Failed to connect' }))

    setSaving(false)
    if (res.connected) {
      setConnecting(null); setToken(''); setSuccess(connecting.name + ' connected!')
      refresh()
      setTimeout(() => setSuccess(''), 3000)
    }
  }

  const filtered = category === 'all' ? SERVICES : SERVICES.filter(s => s.category === category)
  const connectedCount = connections.filter(c => c.connected).length
  const totalTools = connections.reduce((s, c) => s + c.tools, 0)

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Connections</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>
            {connectedCount} connected · {totalTools} tools available · {SERVICES.length} services supported
          </p>
        </div>
      </div>

      {/* Success message */}
      {success && (
        <div className="card p-3 mb-4 flex items-center gap-2" style={{ background: 'var(--green-light)' }}>
          <span className="w-2 h-2 rounded-full" style={{ background: 'var(--green)' }} />
          <span className="text-[13px] font-medium" style={{ color: 'var(--green)' }}>{success}</span>
        </div>
      )}

      {/* Connection modal */}
      {connecting && (
        <div className="card p-6 mb-6" style={{ borderLeft: '4px solid var(--accent)' }}>
          <div className="flex items-center justify-between mb-4">
            <h3 className="text-[16px] font-semibold">Connect {connecting.name}</h3>
            <button onClick={() => setConnecting(null)} className="text-[12px]" style={{ color: 'var(--text-dim)' }}>Cancel</button>
          </div>

          <p className="text-[13px] mb-4" style={{ color: 'var(--text-secondary)' }}>{connecting.auth.help_text}</p>

          {connecting.auth.help_url && (
            <a href={connecting.auth.help_url} target="_blank" rel="noopener noreferrer"
              className="inline-block text-[12px] font-medium mb-4 underline" style={{ color: 'var(--accent)' }}>
              Get your {connecting.auth.label} →
            </a>
          )}

          <div className="mb-4">
            <label className="block text-[11px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>
              {connecting.auth.label}
            </label>
            <input
              type={connecting.auth.type === 'token' ? 'password' : 'text'}
              value={token}
              onChange={e => setToken(e.target.value)}
              placeholder={connecting.auth.placeholder}
              className="w-full rounded-lg px-4 py-3 text-[13px] mono outline-none placeholder:opacity-25"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
              onKeyDown={e => { if (e.key === 'Enter') handleSaveConnection() }}
            />
          </div>

          <div className="flex items-center justify-between">
            <div className="text-[11px]" style={{ color: 'var(--text-dim)' }}>
              Tools: {connecting.tools_preview.slice(0, 4).join(', ')}{connecting.tools_preview.length > 4 ? ` +${connecting.tools_preview.length - 4}` : ''}
            </div>
            <button onClick={handleSaveConnection} disabled={saving || !token.trim()}
              className="px-5 py-2.5 rounded-lg text-[13px] font-semibold text-white disabled:opacity-30"
              style={{ background: 'var(--accent)' }}>
              {saving ? 'Connecting...' : 'Connect'}
            </button>
          </div>
        </div>
      )}

      {/* Category filter */}
      <div className="flex gap-1 mb-5">
        {SERVICE_CATEGORIES.map(cat => (
          <button key={cat.id} onClick={() => setCategory(cat.id)}
            className="px-3 py-1.5 rounded-lg text-[12px] font-medium transition-all"
            style={{ background: category === cat.id ? 'var(--accent-light)' : 'transparent', color: category === cat.id ? 'var(--accent)' : 'var(--text-dim)' }}>
            {cat.label}
          </button>
        ))}
      </div>

      {/* Service grid */}
      <div className="grid grid-cols-3 gap-3">
        {filtered.map(service => {
          const connected = isConnected(service.id)
          const conn = getConnection(service.id)
          return (
            <div key={service.id} className="card p-4 hover:shadow-md transition-all relative">
              {service.popular && <div className="absolute top-3 right-3 text-[8px] font-semibold uppercase px-1.5 py-[2px] rounded-full" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>Popular</div>}

              <div className="flex items-center gap-2.5 mb-2">
                <div className="w-9 h-9 rounded-xl flex items-center justify-center text-[14px] font-bold"
                  style={{ background: connected ? 'var(--green-light)' : 'var(--bg-raised)', color: connected ? 'var(--green)' : 'var(--text-dim)' }}>
                  {service.name[0]}
                </div>
                <div>
                  <div className="text-[13px] font-semibold">{service.name}</div>
                  {connected && <div className="text-[10px] font-medium" style={{ color: 'var(--green)' }}>Connected{conn?.tools ? ` · ${conn.tools} tools` : ''}</div>}
                </div>
              </div>

              <p className="text-[11px] mb-3 line-clamp-2" style={{ color: 'var(--text-dim)' }}>{service.description}</p>

              {/* Tools preview */}
              <div className="flex flex-wrap gap-1 mb-3">
                {service.tools_preview.slice(0, 3).map(t => (
                  <span key={t} className="text-[9px] px-1.5 py-0.5 rounded" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>{t}</span>
                ))}
                {service.tools_preview.length > 3 && <span className="text-[9px]" style={{ color: 'var(--text-dim)' }}>+{service.tools_preview.length - 3}</span>}
              </div>

              {connected ? (
                <div className="flex items-center gap-2">
                  <span className="flex-1 text-[11px] font-medium" style={{ color: 'var(--green)' }}>✓ Connected</span>
                  {service.webhook?.supported && <span className="text-[9px] px-1.5 py-0.5 rounded" style={{ background: 'var(--blue-light)', color: 'var(--blue)' }}>webhooks</span>}
                </div>
              ) : (
                <button onClick={() => handleConnect(service)}
                  className="w-full py-2 rounded-lg text-[12px] font-medium transition-all"
                  style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
                  Connect
                </button>
              )}
            </div>
          )
        })}
      </div>
    </div>
  )
}
