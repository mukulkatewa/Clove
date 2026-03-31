'use client'
import { useState } from 'react'
import { useRouter } from 'next/navigation'
import { submitRun } from '@/lib/api'

const API = process.env.NEXT_PUBLIC_API_URL || ''

const EXAMPLES = [
  'Monitor my production API every 5 minutes and alert if response time exceeds 2 seconds',
  'Review new GitHub pull requests for security vulnerabilities and post comments',
  'Compile a daily AI news digest every weekday morning and post to Slack',
  'Watch for PagerDuty alerts and automatically diagnose the root cause',
  'Scan project dependencies weekly for known vulnerabilities',
  'Answer customer support questions in Slack using our knowledge base',
]

interface GeneratedConfig {
  name: string
  description: string
  triggers: Array<{ type: string; schedule?: string; source?: string }>
  connections: string[]
  tools: string[]
  goal: string
  budget_per_run: number
  budget_daily: number
  max_steps: number
  sandbox: { can_exec: boolean; can_http: boolean; can_write: boolean; allowed_domains: string[] }
}

export default function NewAgentPage() {
  const router = useRouter()
  const [description, setDescription] = useState('')
  const [generating, setGenerating] = useState(false)
  const [config, setConfig] = useState<GeneratedConfig | null>(null)
  const [editing, setEditing] = useState(false)
  const [saving, setSaving] = useState(false)
  const [error, setError] = useState('')

  const handleGenerate = async () => {
    if (!description.trim()) return
    setGenerating(true); setError(''); setConfig(null)

    try {
      const result = await submitRun({
        goal: `You are an agent configuration generator for the CLOVE Agent Fleet OS.

The user wants to create an agent that does the following:
"${description.trim()}"

Generate a JSON configuration for this agent. The JSON must have exactly these fields:
{
  "name": "kebab-case-name",
  "description": "one line description",
  "triggers": [{"type": "cron"|"webhook"|"manual", "schedule": "cron expr if cron", "source": "service name if webhook"}],
  "connections": ["github", "slack", etc - only services the agent actually needs],
  "tools": ["read_file", "write_file", "exec", "http", "search", "store", "fetch", "remember", "recall", "mcp_call", "delegate" - only tools actually needed],
  "goal": "The detailed prompt that will be sent to the agent when triggered. Use {{event}} for trigger payload.",
  "budget_per_run": 0.05 to 2.00 (reasonable for the task),
  "budget_daily": 1.00 to 20.00 (reasonable for the frequency),
  "max_steps": 3 to 20 (reasonable for complexity),
  "sandbox": {
    "can_exec": true/false,
    "can_http": true/false,
    "can_write": true/false,
    "allowed_domains": ["only domains actually needed"]
  }
}

Rules:
- Be conservative with permissions. Only enable what's needed.
- Budget should match the task complexity and frequency.
- The goal prompt should be specific and actionable, not vague.
- For cron triggers, pick a sensible schedule based on what the user described.
- For webhook triggers, identify the source service.
- Only include connections for services the agent will actually call.

Return ONLY the JSON object, no markdown, no explanation.`,
        budget: 0.10,
        max_steps: 3,
      })

      if (result.success && result.content) {
        // Parse JSON from the response
        const jsonMatch = result.content.match(/\{[\s\S]*\}/)
        if (jsonMatch) {
          const parsed = JSON.parse(jsonMatch[0]) as GeneratedConfig
          setConfig(parsed)
        } else {
          setError('Could not parse agent config from response')
        }
      } else {
        setError(result.content || 'Generation failed')
      }
    } catch (e) {
      setError(`Error: ${e}. Is the kernel running?`)
    }
    setGenerating(false)
  }

  const handleSave = async () => {
    if (!config) return
    setSaving(true)
    const agent = {
      name: config.name,
      description: config.description,
      enabled: false,
      connections: config.connections,
      triggers: config.triggers,
      action: { goal: config.goal, tools: config.tools, max_steps: config.max_steps },
      permissions: { ...config.sandbox, can_read: true, allowed_paths: [] },
      budget: { per_run: config.budget_per_run, daily_max: config.budget_daily, daily_spent: 0, last_reset: new Date().toISOString().slice(0, 10) },
      memory: [],
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    }
    try {
      await fetch(`${API}/api/agent-defs`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(agent) })
      router.push('/agents')
    } catch {
      setError('Failed to save agent. Is the kernel running? (clove start)')
    }
    setSaving(false)
  }

  const TRIGGER_COLORS: Record<string, { bg: string; fg: string }> = {
    cron: { bg: 'var(--blue-light)', fg: 'var(--blue)' },
    webhook: { bg: 'var(--accent-light)', fg: 'var(--accent)' },
    manual: { bg: 'var(--bg-raised)', fg: 'var(--text-dim)' },
  }

  return (
    <div className="max-w-[720px] mx-auto">
      <h2 className="text-[24px] font-semibold tracking-[-0.03em] mb-1">Create Agent</h2>
      <p className="text-[14px] mb-8" style={{ color: 'var(--text-dim)' }}>Describe what you want. We&apos;ll figure out the rest.</p>

      {/* Step 1: Describe */}
      {!config && (
        <>
          <div className="card p-0 mb-6 overflow-hidden">
            <textarea
              value={description}
              onChange={(e) => setDescription(e.target.value)}
              placeholder="Describe what your agent should do..."
              rows={4}
              className="w-full p-6 text-[16px] resize-none outline-none placeholder:opacity-25 leading-relaxed"
              style={{ background: 'var(--bg-card)', color: 'var(--text)', border: 'none' }}
              onKeyDown={(e) => { if (e.key === 'Enter' && e.metaKey) handleGenerate() }}
            />
            <div className="flex items-center justify-between px-6 py-3" style={{ background: 'var(--bg)', borderTop: '1px solid var(--border)' }}>
              <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
                The kernel will generate tools, triggers, budget, and permissions automatically
              </span>
              <button onClick={handleGenerate} disabled={generating || !description.trim()}
                className="px-5 py-2 rounded-lg text-[13px] font-semibold text-white transition-all disabled:opacity-30 flex items-center gap-2"
                style={{ background: 'var(--accent)' }}>
                {generating ? (
                  <><svg width="14" height="14" viewBox="0 0 24 24" className="animate-spin"><circle cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="3" fill="none" strokeDasharray="30 70" strokeLinecap="round"/></svg>Generating...</>
                ) : (
                  <>Generate Agent <kbd className="text-[9px] opacity-60 bg-white/10 px-1 rounded">⌘↵</kbd></>
                )}
              </button>
            </div>
          </div>

          {/* Examples */}
          {!description && (
            <div>
              <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Examples</div>
              <div className="grid grid-cols-2 gap-2">
                {EXAMPLES.map((ex, i) => (
                  <button key={i} onClick={() => setDescription(ex)}
                    className="card p-3.5 text-left text-[12px] leading-relaxed hover:shadow-md transition-all"
                    style={{ color: 'var(--text-secondary)' }}>
                    {ex}
                  </button>
                ))}
              </div>
            </div>
          )}
        </>
      )}

      {/* Error */}
      {error && (
        <div className="card p-4 mb-6" style={{ borderLeft: '4px solid var(--red)' }}>
          <div className="text-[13px]" style={{ color: 'var(--red)' }}>{error}</div>
          <button onClick={() => { setError(''); setConfig(null) }} className="text-[12px] mt-2 font-medium" style={{ color: 'var(--text-dim)' }}>Try again</button>
        </div>
      )}

      {/* Step 2: Review generated config */}
      {config && (
        <>
          <div className="card p-6 mb-4">
            <div className="flex items-center justify-between mb-4">
              <div>
                <h3 className="text-[18px] font-semibold">{config.name}</h3>
                <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>{config.description}</p>
              </div>
              <button onClick={() => setEditing(!editing)} className="text-[12px] font-medium px-3 py-1.5 rounded-lg" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
                {editing ? 'Preview' : 'Edit JSON'}
              </button>
            </div>

            {editing ? (
              <textarea
                value={JSON.stringify(config, null, 2)}
                onChange={(e) => { try { setConfig(JSON.parse(e.target.value)) } catch {} }}
                rows={20}
                className="w-full rounded-xl p-4 mono text-[12px] resize-none outline-none leading-relaxed"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
              />
            ) : (
              <div className="space-y-4">
                {/* Trigger */}
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Trigger</div>
                  <div className="flex gap-2">
                    {config.triggers.map((t, i) => {
                      const tc = TRIGGER_COLORS[t.type] || TRIGGER_COLORS.manual
                      return (
                        <div key={i} className="px-3 py-2 rounded-xl text-[12px] font-medium" style={{ background: tc.bg, color: tc.fg }}>
                          {t.type === 'cron' ? `⏰ ${t.schedule}` : t.type === 'webhook' ? `🔗 ${t.source} webhook` : '▶ Manual'}
                        </div>
                      )
                    })}
                  </div>
                </div>

                {/* Connections + Tools */}
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Connections</div>
                    {config.connections.length > 0 ? (
                      <div className="flex flex-wrap gap-1.5">
                        {config.connections.map(c => <span key={c} className="text-[11px] font-medium px-2 py-1 rounded-lg capitalize" style={{ background: 'var(--green-light)', color: 'var(--green)' }}>{c}</span>)}
                      </div>
                    ) : <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>None needed</span>}
                  </div>
                  <div>
                    <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Tools</div>
                    <div className="flex flex-wrap gap-1.5">
                      {config.tools.map(t => <span key={t} className="text-[10px] font-medium px-2 py-0.5 rounded-lg" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{t}</span>)}
                    </div>
                  </div>
                </div>

                {/* Budget */}
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Budget</div>
                  <div className="flex gap-4 text-[13px]">
                    <span><span className="font-semibold tabular-nums" style={{ color: 'var(--accent)' }}>${config.budget_per_run.toFixed(2)}</span> <span style={{ color: 'var(--text-dim)' }}>per run</span></span>
                    <span><span className="font-semibold tabular-nums" style={{ color: 'var(--accent)' }}>${config.budget_daily.toFixed(2)}</span> <span style={{ color: 'var(--text-dim)' }}>daily max</span></span>
                    <span><span className="font-semibold tabular-nums">{config.max_steps}</span> <span style={{ color: 'var(--text-dim)' }}>steps</span></span>
                  </div>
                </div>

                {/* Sandbox */}
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Sandbox Permissions</div>
                  <div className="flex gap-3 text-[12px]">
                    {[
                      { label: 'Shell', on: config.sandbox.can_exec },
                      { label: 'HTTP', on: config.sandbox.can_http },
                      { label: 'Write', on: config.sandbox.can_write },
                    ].map(p => (
                      <span key={p.label} className="flex items-center gap-1.5">
                        <span style={{ color: p.on ? 'var(--green)' : 'var(--red)' }}>{p.on ? '✓' : '✗'}</span>
                        <span style={{ color: p.on ? 'var(--text)' : 'var(--text-dim)' }}>{p.label}</span>
                      </span>
                    ))}
                  </div>
                  {config.sandbox.allowed_domains.length > 0 && (
                    <div className="flex flex-wrap gap-1.5 mt-2">
                      {config.sandbox.allowed_domains.map(d => <span key={d} className="text-[10px] mono px-2 py-0.5 rounded" style={{ background: 'var(--bg)', color: 'var(--green)' }}>{d}</span>)}
                    </div>
                  )}
                </div>

                {/* Goal */}
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Agent Goal</div>
                  <div className="rounded-xl p-4 text-[13px] leading-relaxed" style={{ background: 'var(--bg)', color: 'var(--text-secondary)' }}>
                    {config.goal}
                  </div>
                </div>
              </div>
            )}
          </div>

          {/* Actions */}
          <div className="flex items-center justify-between">
            <button onClick={() => { setConfig(null); setDescription('') }} className="text-[13px] font-medium" style={{ color: 'var(--text-dim)' }}>
              Start over
            </button>
            <div className="flex gap-3">
              <button onClick={() => { setConfig(null) }} className="px-4 py-2.5 rounded-lg text-[13px] font-medium" style={{ background: 'var(--bg-raised)', color: 'var(--text-secondary)' }}>
                Regenerate
              </button>
              <button onClick={handleSave} disabled={saving}
                className="px-6 py-2.5 rounded-lg text-[13px] font-semibold text-white disabled:opacity-30"
                style={{ background: 'var(--accent)' }}>
                {saving ? 'Deploying...' : 'Deploy Agent'}
              </button>
            </div>
          </div>
        </>
      )}
    </div>
  )
}
