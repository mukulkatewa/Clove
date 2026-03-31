'use client'
import { useEffect, useState } from 'react'
import { getSchedules, createSchedule, deleteSchedule, getWebhooks, createWebhook, deleteWebhook, getMcpServers } from '@/lib/api'
import type { Schedule, Webhook, McpServer } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoSchedules, demoWebhooks, demoMcpServers } from '@/lib/demo-data'

const API = process.env.NEXT_PUBLIC_API_URL || ''
type Tab = 'providers' | 'connections' | 'schedules' | 'webhooks' | 'inference'

export default function SettingsPage() {
  const { isDemo } = useDemo()
  const [tab, setTab] = useState<Tab>('providers')
  const [schedules, setSchedules] = useState<Schedule[]>([])
  const [webhooks, setWebhooks] = useState<Webhook[]>([])
  const [mcpServers, setMcpServers] = useState<McpServer[]>([])
  const [infConfig, setInfConfig] = useState<Record<string, unknown> | null>(null)
  const [schName, setSchName] = useState(''); const [schCron, setSchCron] = useState(''); const [schGoal, setSchGoal] = useState('')
  const [whUrl, setWhUrl] = useState(''); const [whEvents, setWhEvents] = useState('run_complete,budget_exceeded')

  useEffect(() => {
    if (isDemo) { setSchedules(demoSchedules() as Schedule[]); setWebhooks(demoWebhooks() as Webhook[]); setMcpServers(demoMcpServers() as McpServer[]); setInfConfig({ enabled: true, default_model: 'anthropic/claude-sonnet-4', max_cost_usd: 25, current_cost_usd: 4.28, total_requests: 347 }); return }
    refresh(); getMcpServers().then(r => setMcpServers(r.servers || [])).catch(() => {}); fetch(`${API}/api/inference`).then(r => r.json()).then(setInfConfig).catch(() => {})
  }, [isDemo])

  const refresh = () => { getSchedules().then(r => setSchedules(r.schedules || [])).catch(() => {}); getWebhooks().then(r => setWebhooks(r.webhooks || [])).catch(() => {}) }

  return (
    <div className="max-w-[860px]">
      <h2 className="text-[22px] font-semibold tracking-[-0.03em] mb-6">Settings</h2>
      <div className="flex gap-1 mb-6" style={{ borderBottom: '1px solid var(--border)' }}>
        {(['providers', 'connections', 'schedules', 'webhooks', 'inference'] as Tab[]).map(t => (
          <button key={t} onClick={() => setTab(t)} className="px-4 py-2.5 text-[13px] font-medium capitalize -mb-px"
            style={{ color: tab === t ? 'var(--accent)' : 'var(--text-dim)', borderBottom: tab === t ? '2px solid var(--accent)' : '2px solid transparent' }}>{t}</button>
        ))}
      </div>

      {tab === 'providers' && (
        <div className="space-y-3">
          <p className="text-[13px] mb-2" style={{ color: 'var(--text-dim)' }}>LLM provider API keys. Agents use these to call different models.</p>
          {[{ name: 'OpenRouter', key: 'openrouterKey', desc: '300+ models, one key', ph: 'sk-or-v1-...', on: true },
            { name: 'Anthropic', key: 'anthropicKey', desc: 'Claude Opus, Sonnet, Haiku', ph: 'sk-ant-...', on: false },
            { name: 'OpenAI', key: 'openaiKey', desc: 'GPT-4o, o3, Codex', ph: 'sk-...', on: false },
            { name: 'Google', key: 'googleKey', desc: 'Gemini Pro, Flash', ph: 'AIza...', on: false },
          ].map(p => (
            <div key={p.name} className="card p-5">
              <div className="flex items-center gap-2 mb-1"><span className="text-[14px] font-semibold">{p.name}</span>{p.on && <span className="text-[9px] font-semibold px-1.5 py-[2px] rounded-full" style={{ background: 'var(--green-light)', color: 'var(--green)' }}>Connected</span>}</div>
              <p className="text-[12px] mb-3" style={{ color: 'var(--text-dim)' }}>{p.desc}</p>
              <input placeholder={p.ph} type="password" className="w-full rounded-lg px-3 py-2.5 text-[13px] outline-none placeholder:opacity-25" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
            </div>
          ))}
        </div>
      )}

      {tab === 'connections' && (
        <div className="space-y-3">
          <p className="text-[13px] mb-2" style={{ color: 'var(--text-dim)' }}>MCP servers. Configure in ~/.clove/mcp.yaml or via CLI.</p>
          {mcpServers.length === 0 ? <div className="card py-10 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No MCP servers</div> :
          mcpServers.map(s => (
            <div key={s.name} className="card p-4 flex items-center justify-between">
              <div className="flex items-center gap-2"><span className="w-2 h-2 rounded-full" style={{ background: s.status === 'running' ? 'var(--green)' : 'var(--text-dim)' }} /><span className="text-[13px] font-semibold capitalize">{s.name}</span><span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{s.tools_count} tools</span></div>
              <span className="text-[10px] mono" style={{ color: 'var(--text-dim)' }}>{s.command}</span>
            </div>
          ))}
          <div className="card p-4 mono text-[11px]" style={{ color: 'var(--text-dim)' }}>clove connect github ghp_token</div>
        </div>
      )}

      {tab === 'schedules' && (
        <div>
          <div className="card p-4 mb-4">
            <div className="grid grid-cols-3 gap-2 mb-2">
              <input value={schName} onChange={e => setSchName(e.target.value)} placeholder="Name" className="rounded-lg px-3 py-2 text-[13px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
              <input value={schCron} onChange={e => setSchCron(e.target.value)} placeholder="0 9 * * MON" className="rounded-lg px-3 py-2 text-[13px] mono outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
              <input value={schGoal} onChange={e => setSchGoal(e.target.value)} placeholder="Goal" className="rounded-lg px-3 py-2 text-[13px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
            </div>
            <button onClick={async () => { if (!schName || !schCron || !schGoal) return; await createSchedule({ name: schName, cron: schCron, run: { goal: schGoal, budget: 5, agents: 3 } }); setSchName(''); setSchCron(''); setSchGoal(''); refresh() }}
              className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Add</button>
          </div>
          {schedules.map(s => (
            <div key={s.name} className="card p-4 mb-2 flex items-center justify-between">
              <div className="flex items-center gap-3"><span className="w-2 h-2 rounded-full" style={{ background: s.enabled ? 'var(--green)' : 'var(--text-dim)' }} /><span className="font-medium" style={{ color: 'var(--accent)' }}>{s.name}</span><span className="text-[11px] mono" style={{ color: 'var(--text-dim)' }}>{s.cron}</span></div>
              <button onClick={() => { deleteSchedule(s.name); refresh() }} className="text-[11px]" style={{ color: 'var(--red)' }}>Delete</button>
            </div>
          ))}
        </div>
      )}

      {tab === 'webhooks' && (
        <div>
          <div className="card p-4 mb-4">
            <div className="flex gap-2 mb-2">
              <input value={whUrl} onChange={e => setWhUrl(e.target.value)} placeholder="https://hooks.slack.com/..." className="flex-1 rounded-lg px-3 py-2 text-[13px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
              <input value={whEvents} onChange={e => setWhEvents(e.target.value)} placeholder="events" className="w-48 rounded-lg px-3 py-2 text-[13px] mono outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
            </div>
            <button onClick={async () => { if (!whUrl) return; await createWebhook({ url: whUrl, events: whEvents.split(',').map(s => s.trim()) }); setWhUrl(''); refresh() }}
              className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Add</button>
          </div>
          {webhooks.map(w => (
            <div key={w.id} className="card p-4 mb-2 flex items-center justify-between">
              <div><span className="text-[12px] truncate">{w.url}</span><span className="text-[10px] ml-2 mono" style={{ color: 'var(--text-dim)' }}>{w.events.join(', ')}</span></div>
              <button onClick={() => { deleteWebhook(w.id); refresh() }} className="text-[11px]" style={{ color: 'var(--red)' }}>Delete</button>
            </div>
          ))}
        </div>
      )}

      {tab === 'inference' && infConfig && (
        <div className="space-y-3">
          <div className="grid grid-cols-3 gap-3">
            <div className="card p-4"><div className="text-[9px] uppercase tracking-[0.08em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Status</div><div className="text-[15px] font-bold" style={{ color: infConfig.enabled ? 'var(--green)' : 'var(--red)' }}>{infConfig.enabled ? 'Active' : 'Off'}</div></div>
            <div className="card p-4"><div className="text-[9px] uppercase tracking-[0.08em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Cost</div><div className="text-[15px] font-bold tabular-nums" style={{ color: 'var(--accent)' }}>${Number(infConfig.current_cost_usd || 0).toFixed(2)}</div></div>
            <div className="card p-4"><div className="text-[9px] uppercase tracking-[0.08em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Cap</div><div className="text-[15px] font-bold tabular-nums">${Number(infConfig.max_cost_usd || 0).toFixed(2)}</div></div>
          </div>
          <div className="card p-5"><div className="text-[13px] font-semibold mb-1">Default Model</div><div className="mono text-[13px]" style={{ color: 'var(--accent)' }}>{String(infConfig.default_model || infConfig.model || 'not set')}</div></div>
        </div>
      )}
    </div>
  )
}
