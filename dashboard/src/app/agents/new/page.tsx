'use client'
import { useState } from 'react'
import { useRouter } from 'next/navigation'

const AVAILABLE_TOOLS = ['read_file', 'write_file', 'exec', 'http', 'search', 'store', 'fetch', 'remember', 'recall', 'mcp_call', 'delegate']
const SERVICES = ['github', 'slack', 'filesystem', 'postgres', 'notion', 'google_drive']
const REGISTRY_URL = 'http://localhost:8090'

export default function NewAgentPage() {
  const router = useRouter()
  const [name, setName] = useState('')
  const [description, setDescription] = useState('')
  const [goal, setGoal] = useState('')
  const [triggerType, setTriggerType] = useState<'manual' | 'cron' | 'webhook'>('manual')
  const [cronSchedule, setCronSchedule] = useState('0 9 * * MON-FRI')
  const [webhookSource, setWebhookSource] = useState('github')
  const [tools, setTools] = useState<string[]>(['read_file', 'exec', 'http', 'search'])
  const [connections, setConnections] = useState<string[]>([])
  const [budgetPerRun, setBudgetPerRun] = useState(0.30)
  const [budgetDaily, setBudgetDaily] = useState(5.00)
  const [maxSteps, setMaxSteps] = useState(10)
  const [saving, setSaving] = useState(false)

  const toggleTool = (tool: string) => setTools(p => p.includes(tool) ? p.filter(t => t !== tool) : [...p, tool])
  const toggleConn = (conn: string) => setConnections(p => p.includes(conn) ? p.filter(c => c !== conn) : [...p, conn])

  const handleSave = async () => {
    if (!name.trim() || !goal.trim()) return
    setSaving(true)
    const agent = {
      name: name.trim().toLowerCase().replace(/\s+/g, '-'),
      description: description.trim(),
      enabled: false,
      connections,
      triggers: [
        triggerType === 'cron' ? { type: 'cron', schedule: cronSchedule } :
        triggerType === 'webhook' ? { type: 'webhook', source: webhookSource } :
        { type: 'manual' }
      ],
      action: { goal: goal.trim(), tools, max_steps: maxSteps },
      permissions: { can_exec: tools.includes('exec'), can_read: tools.includes('read_file'), can_write: tools.includes('write_file'), can_http: tools.includes('http'), allowed_domains: [], allowed_paths: [] },
      budget: { per_run: budgetPerRun, daily_max: budgetDaily, daily_spent: 0, last_reset: new Date().toISOString().slice(0, 10) },
      memory: [],
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    }
    try {
      await fetch(`${REGISTRY_URL}/agents`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(agent) })
      router.push('/agents')
    } catch {
      // Fall back: save via kernel state
      alert('Agent registry not running. Start it with: node agent-registry/dist/index.js')
    }
    setSaving(false)
  }

  return (
    <div className="max-w-[700px]">
      <h2 className="text-[24px] font-semibold tracking-[-0.03em] mb-1">New Agent</h2>
      <p className="text-[14px] mb-8" style={{ color: 'var(--text-dim)' }}>Define a persistent agent with triggers, tools, and budget.</p>

      {/* Identity */}
      <Section title="Identity">
        <div className="grid grid-cols-2 gap-4 mb-4">
          <div>
            <Label>Name</Label>
            <input value={name} onChange={e => setName(e.target.value)} placeholder="pr-reviewer"
              className="w-full rounded-xl px-4 py-2.5 text-[14px] outline-none placeholder:opacity-25"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
          <div>
            <Label>Description</Label>
            <input value={description} onChange={e => setDescription(e.target.value)} placeholder="Reviews PRs for security issues"
              className="w-full rounded-xl px-4 py-2.5 text-[14px] outline-none placeholder:opacity-25"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
        </div>
      </Section>

      {/* Goal */}
      <Section title="Goal">
        <Label>What should this agent do when triggered?</Label>
        <textarea value={goal} onChange={e => setGoal(e.target.value)}
          placeholder="When triggered, this agent should...&#10;&#10;Use {{event}} to reference the trigger payload."
          rows={4}
          className="w-full rounded-xl px-4 py-3 text-[14px] resize-none outline-none placeholder:opacity-25 leading-relaxed"
          style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
      </Section>

      {/* Trigger */}
      <Section title="Trigger">
        <Label>How is this agent activated?</Label>
        <div className="flex gap-2 mb-4">
          {(['manual', 'cron', 'webhook'] as const).map(t => (
            <button key={t} onClick={() => setTriggerType(t)}
              className="px-4 py-2 rounded-xl text-[13px] font-medium capitalize transition-all"
              style={{ background: triggerType === t ? 'var(--accent)' : 'var(--bg)', color: triggerType === t ? '#fff' : 'var(--text-dim)', border: `1px solid ${triggerType === t ? 'var(--accent)' : 'var(--border)'}` }}>
              {t}
            </button>
          ))}
        </div>

        {triggerType === 'cron' && (
          <div>
            <Label>Schedule (cron expression)</Label>
            <input value={cronSchedule} onChange={e => setCronSchedule(e.target.value)}
              className="w-64 rounded-xl px-4 py-2.5 text-[13px] mono outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
            <div className="text-[11px] mt-1.5" style={{ color: 'var(--text-dim)' }}>
              Examples: <span className="mono">*/5 * * * *</span> (every 5 min), <span className="mono">0 9 * * MON-FRI</span> (weekday mornings)
            </div>
          </div>
        )}

        {triggerType === 'webhook' && (
          <div>
            <Label>Webhook Source</Label>
            <div className="flex gap-2">
              {['github', 'slack', 'stripe', 'pagerduty', 'custom'].map(s => (
                <button key={s} onClick={() => setWebhookSource(s)}
                  className="px-3 py-1.5 rounded-lg text-[12px] font-medium capitalize transition-all"
                  style={{ background: webhookSource === s ? 'var(--accent-light)' : 'var(--bg)', border: `1px solid ${webhookSource === s ? 'var(--accent)' : 'var(--border)'}`, color: webhookSource === s ? 'var(--accent)' : 'var(--text-dim)' }}>
                  {s}
                </button>
              ))}
            </div>
          </div>
        )}
      </Section>

      {/* Tools */}
      <Section title="Tools">
        <Label>Which capabilities does this agent need?</Label>
        <div className="flex flex-wrap gap-2">
          {AVAILABLE_TOOLS.map(tool => {
            const active = tools.includes(tool)
            return <button key={tool} onClick={() => toggleTool(tool)}
              className="px-3 py-1.5 rounded-xl text-[12px] font-medium transition-all"
              style={{ background: active ? 'var(--accent-light)' : 'var(--bg)', border: `1px solid ${active ? 'var(--accent)' : 'var(--border)'}`, color: active ? 'var(--accent)' : 'var(--text-dim)' }}>
              {tool}
            </button>
          })}
        </div>
      </Section>

      {/* Connections */}
      <Section title="Connections">
        <Label>Which external services should this agent access?</Label>
        <div className="flex flex-wrap gap-2">
          {SERVICES.map(svc => {
            const active = connections.includes(svc)
            return <button key={svc} onClick={() => toggleConn(svc)}
              className="px-3 py-1.5 rounded-xl text-[12px] font-medium capitalize transition-all"
              style={{ background: active ? 'var(--green-light)' : 'var(--bg)', border: `1px solid ${active ? 'var(--green)' : 'var(--border)'}`, color: active ? 'var(--green)' : 'var(--text-dim)' }}>
              {svc}
            </button>
          })}
        </div>
      </Section>

      {/* Budget */}
      <Section title="Budget">
        <div className="grid grid-cols-3 gap-4">
          <div>
            <Label>Per Run (USD)</Label>
            <input type="number" step="0.05" min="0.01" value={budgetPerRun} onChange={e => setBudgetPerRun(parseFloat(e.target.value) || 0.3)}
              className="w-full rounded-xl px-4 py-2.5 text-[13px] tabular-nums outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
          <div>
            <Label>Daily Max (USD)</Label>
            <input type="number" step="1" min="0.1" value={budgetDaily} onChange={e => setBudgetDaily(parseFloat(e.target.value) || 5)}
              className="w-full rounded-xl px-4 py-2.5 text-[13px] tabular-nums outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
          <div>
            <Label>Max Steps</Label>
            <input type="number" min="1" max="50" value={maxSteps} onChange={e => setMaxSteps(parseInt(e.target.value) || 10)}
              className="w-full rounded-xl px-4 py-2.5 text-[13px] tabular-nums outline-none"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
        </div>
        <div className="text-[12px] mt-3" style={{ color: 'var(--text-dim)' }}>
          Estimated daily cost if triggered continuously: <span className="font-semibold tabular-nums">${(budgetPerRun * (triggerType === 'cron' ? 24 : 10)).toFixed(2)}</span>
        </div>
      </Section>

      {/* Preview */}
      <Section title="Preview">
        <div className="rounded-xl p-4 mono text-[11px] leading-relaxed overflow-x-auto" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>
          {JSON.stringify({
            name: name.trim().toLowerCase().replace(/\s+/g, '-') || 'my-agent',
            triggers: [triggerType === 'cron' ? { type: 'cron', schedule: cronSchedule } : triggerType === 'webhook' ? { type: 'webhook', source: webhookSource } : { type: 'manual' }],
            tools,
            connections,
            budget: { per_run: budgetPerRun, daily_max: budgetDaily },
          }, null, 2)}
        </div>
      </Section>

      {/* Actions */}
      <div className="flex items-center justify-between py-6">
        <button onClick={() => router.push('/agents')} className="text-[13px] font-medium" style={{ color: 'var(--text-dim)' }}>Cancel</button>
        <div className="flex gap-3">
          <button onClick={handleSave} disabled={saving || !name.trim() || !goal.trim()}
            className="px-6 py-2.5 rounded-xl text-[13px] font-semibold text-white disabled:opacity-30"
            style={{ background: 'var(--accent)' }}>
            {saving ? 'Saving...' : 'Create Agent'}
          </button>
        </div>
      </div>
    </div>
  )
}

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="card p-5 mb-4">
      <h3 className="text-[14px] font-semibold mb-3">{title}</h3>
      {children}
    </div>
  )
}

function Label({ children }: { children: React.ReactNode }) {
  return <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>{children}</div>
}
