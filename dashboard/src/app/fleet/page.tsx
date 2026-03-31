'use client'
import { useState } from 'react'
import { streamFleet } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'

interface FleetAgent {
  name: string
  role: string
  tools: string[]
  budget: number
  model: string
  runtime: 'clove' | 'claude-code' | 'codex' | 'openclaw'
}

const MODELS = [
  { id: 'claude-sonnet-4', label: 'Claude Sonnet 4', provider: 'anthropic' },
  { id: 'claude-haiku-4', label: 'Claude Haiku 4', provider: 'anthropic' },
  { id: 'gpt-4o', label: 'GPT-4o', provider: 'openai' },
  { id: 'gpt-4o-mini', label: 'GPT-4o Mini', provider: 'openai' },
  { id: 'codex', label: 'Codex', provider: 'openai' },
  { id: 'gemini-2.5-pro', label: 'Gemini 2.5 Pro', provider: 'google' },
]

const RUNTIMES = [
  { id: 'clove', label: 'CLOVE', description: 'Kernel RunEngine' },
  { id: 'claude-code', label: 'Claude Code', description: 'Anthropic agent runtime' },
  { id: 'codex', label: 'Codex', description: 'OpenAI agent runtime' },
  { id: 'openclaw', label: 'OpenClaw', description: 'Chat agent runtime' },
]

interface FleetEvent { type: string; data: Record<string, unknown> }

const TOOL_OPTIONS = ['read_file', 'write_file', 'exec', 'http', 'search', 'store', 'remember', 'recall', 'mcp_call', 'delegate']

const TEMPLATES = [
  {
    label: 'Research Team',
    description: '3 researchers + synthesizer',
    world: 'research',
    agents: [
      { name: 'researcher-1', role: 'Primary research on the main topic', tools: ['search', 'http', 'remember'], budget: 0.40, model: 'gpt-4o', runtime: 'clove' as const },
      { name: 'researcher-2', role: 'Competitive landscape and alternatives', tools: ['search', 'http', 'remember'], budget: 0.40, model: 'gpt-4o', runtime: 'clove' as const },
      { name: 'researcher-3', role: 'Data, numbers, and statistics', tools: ['search', 'http', 'remember'], budget: 0.40, model: 'gpt-4o-mini', runtime: 'clove' as const },
      { name: 'synthesizer', role: 'Merge all findings into one coherent report', tools: ['recall', 'write_file'], budget: 0.30, model: 'claude-sonnet-4', runtime: 'clove' as const },
    ],
  },
  {
    label: 'Code Review Team',
    description: 'Security + deps + quality + report',
    world: 'code-review',
    agents: [
      { name: 'security-scanner', role: 'Find vulnerabilities and security issues', tools: ['read_file', 'exec'], budget: 0.30, model: 'claude-sonnet-4', runtime: 'claude-code' as const },
      { name: 'dep-checker', role: 'Audit dependencies for issues', tools: ['read_file', 'exec'], budget: 0.30, model: 'gpt-4o', runtime: 'clove' as const },
      { name: 'quality-reviewer', role: 'Check code quality and patterns', tools: ['read_file', 'exec'], budget: 0.30, model: 'claude-sonnet-4', runtime: 'clove' as const },
      { name: 'reporter', role: 'Synthesize findings into health report', tools: ['write_file'], budget: 0.20, model: 'claude-haiku-4', runtime: 'clove' as const },
    ],
  },
  {
    label: 'Incident Response',
    description: 'Detect → diagnose → fix → verify',
    world: 'incident',
    agents: [
      { name: 'sentinel', role: 'Detect and characterize the anomaly', tools: ['http', 'store'], budget: 0.10, model: 'gpt-4o-mini', runtime: 'clove' as const },
      { name: 'diagnostician', role: 'Identify root cause from logs and metrics', tools: ['exec', 'read_file', 'http'], budget: 0.60, model: 'claude-sonnet-4', runtime: 'claude-code' as const },
      { name: 'fixer', role: 'Apply the fix based on diagnosis', tools: ['exec', 'write_file'], budget: 0.80, model: 'codex', runtime: 'codex' as const },
      { name: 'verifier', role: 'Confirm the fix resolved the issue', tools: ['http', 'exec'], budget: 0.30, model: 'gpt-4o', runtime: 'clove' as const },
    ],
  },
]

export default function FleetPage() {
  const { isDemo } = useDemo()
  // Step state
  const [step, setStep] = useState<'assemble' | 'mission' | 'running' | 'done'>('assemble')

  // World
  const [worldName, setWorldName] = useState('fleet-mission')

  // Fleet agents
  const [agents, setAgents] = useState<FleetAgent[]>([])

  // Mission
  const [mission, setMission] = useState('')

  // Execution
  const [events, setEvents] = useState<FleetEvent[]>([])
  const [running, setRunning] = useState(false)

  const addAgent = () => {
    setAgents([...agents, { name: `agent-${agents.length + 1}`, role: '', tools: ['search', 'http'], budget: 0.30, model: 'claude-sonnet-4', runtime: 'clove' }])
  }

  const updateAgent = (idx: number, updates: Partial<FleetAgent>) => {
    setAgents(prev => prev.map((a, i) => i === idx ? { ...a, ...updates } : a))
  }

  const removeAgent = (idx: number) => {
    setAgents(prev => prev.filter((_, i) => i !== idx))
  }

  const loadTemplate = (tpl: typeof TEMPLATES[0]) => {
    setAgents([...tpl.agents])
    setWorldName(tpl.world)
  }

  const totalBudget = agents.reduce((s, a) => s + a.budget, 0)

  const launch = () => {
    if (!mission.trim() || agents.length === 0) return
    setStep('running'); setRunning(true); setEvents([])

    // Build the fleet goal including agent roles
    const roleContext = agents.map((a, i) => `Agent ${i + 1} "${a.name}": ${a.role}`).join('\n')
    const fullGoal = `MISSION: ${mission.trim()}\n\nTEAM (${agents.length} agents in world "${worldName}"):\n${roleContext}\n\nEach agent should focus on their assigned role. Coordinate through shared state.`

    streamFleet(
      { goal: fullGoal, agents: agents.length, budget: totalBudget },
      (ev) => {
        const event = ev as unknown as FleetEvent
        setEvents(prev => [...prev, event])
        if (event.type === 'fleet_done') { setRunning(false); setStep('done') }
      },
    )
  }

  const fleetDone = events.find(e => e.type === 'fleet_done')

  // Group events by agent
  const agentLanes: Record<string, { events: FleetEvent[]; done: boolean; cost: number }> = {}
  for (const ev of events) {
    const name = String(ev.data.agent || ev.data.agent_name || '')
    if (!name) continue
    if (!agentLanes[name]) agentLanes[name] = { events: [], done: false, cost: 0 }
    agentLanes[name].events.push(ev)
    if (ev.type === 'agent_done') agentLanes[name].done = true
    if (ev.data.cost_usd) agentLanes[name].cost = ev.data.cost_usd as number
  }

  return (
    <div className="max-w-[960px]">
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Fleet</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Assemble a team, assign a mission, deploy into a world.</p>
        </div>
        {/* Step indicator */}
        <div className="flex items-center gap-1">
          {['assemble', 'mission', 'running'].map((s, i) => (
            <div key={s} className="flex items-center gap-1">
              <div className="w-6 h-6 rounded-full flex items-center justify-center text-[10px] font-bold"
                style={{
                  background: step === s || (['mission', 'running', 'done'].indexOf(step) > i - 1 && i > 0) ? 'var(--accent)' : 'var(--bg-raised)',
                  color: step === s || (['mission', 'running', 'done'].indexOf(step) > i - 1 && i > 0) ? '#fff' : 'var(--text-dim)',
                }}>
                {i + 1}
              </div>
              {i < 2 && <div className="w-8 h-[2px]" style={{ background: 'var(--border)' }} />}
            </div>
          ))}
        </div>
      </div>

      {/* ── STEP 1: ASSEMBLE ──────────────────────────────── */}
      {step === 'assemble' && (
        <>
          {/* World name */}
          <div className="card p-4 mb-4">
            <div className="flex items-center gap-4">
              <div className="flex-1">
                <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>World</div>
                <input value={worldName} onChange={e => setWorldName(e.target.value)}
                  className="w-full rounded-lg px-3 py-2 text-[13px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
              </div>
              <div className="text-[12px] tabular-nums self-end pb-2" style={{ color: 'var(--text-dim)' }}>
                {agents.length} agents · ${totalBudget.toFixed(2)} budget
              </div>
            </div>
          </div>

          {/* Templates */}
          {agents.length === 0 && (
            <div className="mb-6">
              <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Start from a template</div>
              <div className="grid grid-cols-3 gap-2">
                {TEMPLATES.map((tpl, i) => (
                  <button key={i} onClick={() => loadTemplate(tpl)} className="card p-4 text-left hover:shadow-md transition-all">
                    <div className="text-[13px] font-semibold mb-0.5">{tpl.label}</div>
                    <div className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{tpl.description}</div>
                    <div className="text-[10px] mt-2" style={{ color: 'var(--text-dim)' }}>{tpl.agents.length} agents</div>
                  </button>
                ))}
              </div>
            </div>
          )}

          {/* Agent cards */}
          {agents.length > 0 && (
            <div className="space-y-2 mb-4">
              {agents.map((agent, idx) => (
                <div key={idx} className="card p-4">
                  <div className="flex items-start gap-3">
                    <div className="w-8 h-8 rounded-lg flex items-center justify-center text-[11px] font-bold flex-shrink-0" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>
                      {idx + 1}
                    </div>
                    <div className="flex-1 space-y-2">
                      <div className="flex gap-2">
                        <input value={agent.name} onChange={e => updateAgent(idx, { name: e.target.value })} placeholder="Agent name"
                          className="flex-1 rounded-lg px-3 py-1.5 text-[13px] font-medium outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                        <input type="number" step="0.05" value={agent.budget} onChange={e => updateAgent(idx, { budget: parseFloat(e.target.value) || 0.1 })}
                          className="w-20 rounded-lg px-2 py-1.5 text-[12px] tabular-nums outline-none text-center" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--accent)' }} />
                      </div>
                      <input value={agent.role} onChange={e => updateAgent(idx, { role: e.target.value })} placeholder="What is this agent's role in the team?"
                        className="w-full rounded-lg px-3 py-1.5 text-[12px] outline-none placeholder:opacity-25" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }} />
                      <div className="flex gap-2">
                        <div className="flex-1">
                          <div className="text-[9px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Model</div>
                          <select value={agent.model} onChange={e => updateAgent(idx, { model: e.target.value })}
                            className="w-full rounded-lg px-2 py-1.5 text-[11px] outline-none appearance-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}>
                            {MODELS.map(m => <option key={m.id} value={m.id}>{m.label}</option>)}
                          </select>
                        </div>
                        <div className="flex-1">
                          <div className="text-[9px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Runtime</div>
                          <select value={agent.runtime} onChange={e => updateAgent(idx, { runtime: e.target.value as FleetAgent['runtime'] })}
                            className="w-full rounded-lg px-2 py-1.5 text-[11px] outline-none appearance-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}>
                            {RUNTIMES.map(r => <option key={r.id} value={r.id}>{r.label}</option>)}
                          </select>
                        </div>
                      </div>
                      <div className="flex flex-wrap gap-1">
                        {TOOL_OPTIONS.map(t => {
                          const on = agent.tools.includes(t)
                          return <button key={t} onClick={() => updateAgent(idx, { tools: on ? agent.tools.filter(x => x !== t) : [...agent.tools, t] })}
                            className="px-1.5 py-0.5 rounded text-[9px] font-medium"
                            style={{ background: on ? 'var(--accent-light)' : 'var(--bg)', color: on ? 'var(--accent)' : 'var(--text-dim)', border: `1px solid ${on ? 'var(--accent)' : 'var(--border)'}` }}>
                            {t}
                          </button>
                        })}
                      </div>
                    </div>
                    <button onClick={() => removeAgent(idx)} className="text-[11px] mt-1" style={{ color: 'var(--red)' }}>✕</button>
                  </div>
                </div>
              ))}
            </div>
          )}

          {/* Add + Next */}
          <div className="flex items-center justify-between">
            <button onClick={addAgent} className="text-[12px] font-medium flex items-center gap-1" style={{ color: 'var(--accent)' }}>+ Add Agent</button>
            <button onClick={() => setStep('mission')} disabled={agents.length === 0}
              className="px-5 py-2.5 rounded-lg text-[13px] font-semibold text-white disabled:opacity-30" style={{ background: 'var(--accent)' }}>
              Next: Define Mission →
            </button>
          </div>
        </>
      )}

      {/* ── STEP 2: MISSION ───────────────────────────────── */}
      {step === 'mission' && (
        <>
          {/* Team summary */}
          <div className="card p-4 mb-4">
            <div className="flex items-center justify-between mb-3">
              <div className="text-[12px] font-medium">Team in <span style={{ color: 'var(--accent)' }}>{worldName}</span></div>
              <button onClick={() => setStep('assemble')} className="text-[11px] font-medium" style={{ color: 'var(--text-dim)' }}>← Edit Team</button>
            </div>
            <div className="flex gap-2 flex-wrap">
              {agents.map((a, i) => (
                <div key={i} className="flex items-center gap-1.5 px-2.5 py-1.5 rounded-lg text-[11px]" style={{ background: 'var(--bg)', border: '1px solid var(--border)' }}>
                  <span className="w-4 h-4 rounded flex items-center justify-center text-[8px] font-bold" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{i + 1}</span>
                  <span className="font-medium">{a.name}</span>
                  <span style={{ color: 'var(--text-dim)' }}>· {a.role.slice(0, 25)}{a.role.length > 25 ? '...' : ''}</span>
                </div>
              ))}
            </div>
          </div>

          {/* Mission input */}
          <div className="card p-0 mb-6 overflow-hidden">
            <textarea value={mission} onChange={e => setMission(e.target.value)}
              placeholder="What is the mission for this team? Be specific about the goal — each agent will work on their role toward this objective."
              rows={5}
              className="w-full p-6 text-[15px] resize-none outline-none placeholder:opacity-25 leading-relaxed"
              style={{ background: 'var(--bg-card)', color: 'var(--text)', border: 'none' }}
              onKeyDown={e => { if (e.key === 'Enter' && e.metaKey) launch() }}
            />
            <div className="flex items-center justify-between px-6 py-3" style={{ background: 'var(--bg)', borderTop: '1px solid var(--border)' }}>
              <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
                {agents.length} agents · ${totalBudget.toFixed(2)} total budget · World: {worldName}
              </span>
              <button onClick={launch} disabled={!mission.trim()}
                className="px-6 py-2.5 rounded-lg text-[13px] font-semibold text-white disabled:opacity-30 flex items-center gap-2"
                style={{ background: 'var(--accent)' }}>
                Launch Fleet <kbd className="text-[9px] opacity-60 bg-white/10 px-1 rounded">⌘↵</kbd>
              </button>
            </div>
          </div>
        </>
      )}

      {/* ── STEP 3: RUNNING ───────────────────────────────── */}
      {(step === 'running' || step === 'done') && (
        <>
          {/* Progress header */}
          {!fleetDone && (
            <div className="card p-4 mb-4">
              <div className="flex items-center justify-between mb-2">
                <div className="flex items-center gap-2">
                  <div className="w-2 h-2 rounded-full animate-pulse" style={{ background: 'var(--accent)' }} />
                  <span className="text-[13px] font-medium">Fleet working in <span style={{ color: 'var(--accent)' }}>{worldName}</span></span>
                </div>
                <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{Object.keys(agentLanes).filter(k => agentLanes[k].done).length}/{agents.length} complete</span>
              </div>
              <div className="h-[4px] rounded-full overflow-hidden" style={{ background: 'var(--border)' }}>
                <div className="h-full rounded-full transition-all duration-500" style={{
                  width: `${events.some(e => e.type === 'synthesizing') ? 90 : (Object.keys(agentLanes).filter(k => agentLanes[k].done).length / Math.max(agents.length, 1)) * 80}%`,
                  background: 'var(--accent)',
                }} />
              </div>
            </div>
          )}

          {/* Result */}
          {fleetDone && (
            <div className="card overflow-hidden mb-4">
              <div className="flex items-center justify-between px-5 py-3" style={{ background: 'var(--green-light)' }}>
                <div className="flex items-center gap-2">
                  <span className="w-2 h-2 rounded-full" style={{ background: 'var(--green)' }} />
                  <span className="text-[13px] font-semibold" style={{ color: 'var(--green)' }}>Mission Complete</span>
                </div>
                <span className="text-[12px] tabular-nums font-medium" style={{ color: 'var(--green)' }}>
                  ${(fleetDone.data.total_cost_usd as number)?.toFixed(4)} · {(fleetDone.data.total_tokens as number)?.toLocaleString()} tokens
                </span>
              </div>
              <div className="grid grid-cols-4 gap-0" style={{ borderBottom: '1px solid var(--border)' }}>
                {[
                  { label: 'World', value: worldName },
                  { label: 'Agents', value: String(fleetDone.data.agent_count) },
                  { label: 'Cost', value: `$${(fleetDone.data.total_cost_usd as number)?.toFixed(4)}`, color: 'var(--accent)' },
                  { label: 'Steps', value: String(fleetDone.data.total_steps) },
                ].map((s, i) => (
                  <div key={i} className="px-5 py-3" style={{ borderRight: i < 3 ? '1px solid var(--border)' : undefined }}>
                    <div className="text-[9px] uppercase tracking-[0.06em] font-medium mb-0.5" style={{ color: 'var(--text-dim)' }}>{s.label}</div>
                    <div className="text-[15px] font-bold tabular-nums" style={{ color: s.color || 'var(--text)' }}>{s.value}</div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {/* Agent lanes */}
          <div className="space-y-2 mb-4">
            {agents.map((agent, idx) => {
              const lane = agentLanes[`agent-${idx + 1}`] || agentLanes[agent.name] || { events: [], done: false, cost: 0 }
              return (
                <div key={idx} className="card p-4">
                  <div className="flex items-center gap-3">
                    <div className="w-8 h-8 rounded-lg flex items-center justify-center text-[10px] font-bold flex-shrink-0"
                      style={{ background: lane.done ? 'var(--green-light)' : running ? 'var(--accent-light)' : 'var(--bg-raised)', color: lane.done ? 'var(--green)' : running ? 'var(--accent)' : 'var(--text-dim)' }}>
                      {lane.done ? '✓' : idx + 1}
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-[13px] font-semibold">{agent.name}</span>
                        {!lane.done && running && <div className="w-1.5 h-1.5 rounded-full animate-pulse" style={{ background: 'var(--accent)' }} />}
                        {lane.done && <span className="text-[9px] font-semibold px-1.5 py-[2px] rounded-full" style={{ background: 'var(--green-light)', color: 'var(--green)' }}>done</span>}
                      </div>
                      <div className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{agent.role}</div>
                    </div>
                    <div className="text-[11px] tabular-nums" style={{ color: 'var(--text-dim)' }}>
                      {lane.events.length > 0 && <span>{lane.events.length} events</span>}
                      {lane.cost > 0 && <span className="ml-2" style={{ color: 'var(--accent)' }}>${lane.cost.toFixed(4)}</span>}
                    </div>
                  </div>
                  {/* Recent events */}
                  {lane.events.length > 0 && (
                    <div className="mt-2 space-y-0.5 ml-11">
                      {lane.events.slice(-2).map((ev, i) => (
                        <div key={i} className="text-[10px] mono truncate" style={{ color: 'var(--text-dim)' }}>
                          {ev.data.event_type === 'tool_call' ? `${ev.data.tool}(${JSON.stringify(ev.data.args).slice(0, 40)})` : String(ev.data.event_type || ev.type)}
                        </div>
                      ))}
                    </div>
                  )}
                </div>
              )
            })}
          </div>

          {/* Actions */}
          {step === 'done' && (
            <div className="flex items-center justify-between">
              <button onClick={() => { setStep('assemble'); setEvents([]); setMission('') }} className="text-[12px] font-medium" style={{ color: 'var(--text-dim)' }}>New Fleet</button>
            </div>
          )}
        </>
      )}
    </div>
  )
}
