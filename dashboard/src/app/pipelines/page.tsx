'use client'
import { useEffect, useState } from 'react'
import { getPipelineDefs, createPipelineDef, deletePipelineDef, streamPipeline, getRuntimes } from '@/lib/api'
import type { PipelineDef, PipelineStep } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'

// eslint-disable-next-line @typescript-eslint/no-explicit-any
interface StepEvent { type: string; data: Record<string, any> }

const RUNTIMES = ['clove', 'claude-code', 'codex', 'openclaw'] as const
const TOOLS = ['read_file', 'write_file', 'exec', 'http', 'search', 'store', 'remember', 'recall', 'mcp_call', 'delegate', 'edit_file']

const DEMO_PIPELINES: PipelineDef[] = [
  { name: 'pr-fixer', enabled: true, trigger: { type: 'webhook', source: 'github' }, steps: [
    { name: 'reviewer', runtime: 'clove', role: 'Review PR diff, check for issues', tools: ['mcp_call', 'search'], budget: 0.15, output_key: 'findings' },
    { name: 'fixer', runtime: 'claude-code', role: 'Fix issues found by reviewer', budget: 0.40, output_key: 'changes', input_keys: ['findings'] },
    { name: 'verifier', runtime: 'codex', role: 'Run build and tests', budget: 0.20, output_key: 'verify', input_keys: ['changes'] },
    { name: 'supervisor', runtime: 'clove', role: 'Post summary to PR', tools: ['mcp_call'], budget: 0.05, output_key: 'summary', input_keys: ['findings', 'changes', 'verify'] },
  ]},
  { name: 'security-audit', enabled: false, trigger: { type: 'cron', schedule: '0 18 * * FRI' }, steps: [
    { name: 'scanner', runtime: 'claude-code', role: 'Scan codebase for vulnerabilities', budget: 0.50, output_key: 'vulns' },
    { name: 'reporter', runtime: 'clove', role: 'Generate security report', tools: ['write_file'], budget: 0.10, output_key: 'report', input_keys: ['vulns'] },
  ]},
]

const RUNTIME_COLORS: Record<string, { bg: string; fg: string }> = {
  clove: { bg: 'var(--accent-light)', fg: 'var(--accent)' },
  'claude-code': { bg: 'var(--blue-light)', fg: 'var(--blue)' },
  codex: { bg: 'var(--green-light)', fg: 'var(--green)' },
  openclaw: { bg: 'var(--yellow-light)', fg: 'var(--yellow)' },
}

export default function PipelinesPage() {
  const { isDemo } = useDemo()
  const [pipelines, setPipelines] = useState<PipelineDef[]>([])
  const [mode, setMode] = useState<'list' | 'create' | 'run'>('list')
  const [selected, setSelected] = useState<PipelineDef | null>(null)

  // Create state
  const [newName, setNewName] = useState('')
  const [newSteps, setNewSteps] = useState<PipelineStep[]>([])

  // Run state
  const [events, setEvents] = useState<StepEvent[]>([])
  const [running, setRunning] = useState(false)

  useEffect(() => {
    if (isDemo) { setPipelines(DEMO_PIPELINES); return }
    getPipelineDefs().then(r => setPipelines(r.pipelines || [])).catch(() => {})
  }, [isDemo])

  const addStep = () => {
    setNewSteps([...newSteps, { name: `step-${newSteps.length + 1}`, runtime: 'clove', role: '', tools: ['search', 'http'], budget: 0.30, max_steps: 10, output_key: `step_${newSteps.length + 1}`, input_keys: newSteps.length > 0 ? [newSteps[newSteps.length - 1].output_key] : [] }])
  }

  const updateStep = (idx: number, updates: Partial<PipelineStep>) => {
    setNewSteps(prev => prev.map((s, i) => i === idx ? { ...s, ...updates } : s))
  }

  const removeStep = (idx: number) => { setNewSteps(prev => prev.filter((_, i) => i !== idx)) }

  const savePipeline = async () => {
    if (!newName.trim() || newSteps.length === 0) return
    const def: PipelineDef = { name: newName.trim(), steps: newSteps, enabled: false, trigger: { type: 'manual' } }
    if (!isDemo) await createPipelineDef(def)
    setPipelines(prev => [...prev, def])
    setMode('list'); setNewName(''); setNewSteps([])
  }

  const runPipeline = (pipeline: PipelineDef) => {
    setSelected(pipeline); setMode('run'); setEvents([]); setRunning(true)
    streamPipeline({ name: pipeline.name, steps: pipeline.steps, context: pipeline.context }, (ev) => {
      const event = ev as unknown as StepEvent
      setEvents(prev => [...prev, event])
      if (event.type === 'pipeline_done') setRunning(false)
    })
  }

  const pipelineDone = events.find(e => e.type === 'pipeline_done')
  const stepsDone = events.filter(e => e.type === 'step_done')

  return (
    <div className="max-w-[960px]">
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Pipelines</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Multi-runtime workflows. Chain agents together, pass context between steps.</p>
        </div>
        {mode === 'list' && (
          <button onClick={() => { setMode('create'); setNewName(''); setNewSteps([]) }}
            className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>+ New Pipeline</button>
        )}
        {mode !== 'list' && (
          <button onClick={() => setMode('list')} className="text-[12px] font-medium" style={{ color: 'var(--text-dim)' }}>← Back</button>
        )}
      </div>

      {/* ── LIST ─────────────────────────────────────────── */}
      {mode === 'list' && (
        pipelines.length === 0 ? (
          <div className="card py-16 text-center">
            <div className="text-[15px] font-semibold mb-1">No pipelines</div>
            <p className="text-[13px] mb-4" style={{ color: 'var(--text-dim)' }}>Create a pipeline to chain agents with different runtimes.</p>
            <button onClick={() => setMode('create')} className="px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Create Pipeline</button>
          </div>
        ) : (
          <div className="space-y-3">
            {pipelines.map(p => (
              <div key={p.name} className="card p-5 hover:shadow-md transition-all">
                <div className="flex items-center justify-between mb-3">
                  <div className="flex items-center gap-2">
                    <span className="text-[15px] font-semibold">{p.name}</span>
                    {p.trigger && <span className="text-[10px] font-medium px-1.5 py-[2px] rounded-full" style={{ background: p.trigger.type === 'webhook' ? 'var(--accent-light)' : p.trigger.type === 'cron' ? 'var(--blue-light)' : 'var(--bg-raised)', color: p.trigger.type === 'webhook' ? 'var(--accent)' : p.trigger.type === 'cron' ? 'var(--blue)' : 'var(--text-dim)' }}>{p.trigger.type}{p.trigger.source ? `:${p.trigger.source}` : ''}{p.trigger.schedule ? ` ${p.trigger.schedule}` : ''}</span>}
                  </div>
                  <div className="flex gap-2">
                    <button onClick={() => runPipeline(p)} className="px-3 py-1.5 rounded-lg text-[11px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Run</button>
                    <button onClick={async () => { if (!isDemo) await deletePipelineDef(p.name); setPipelines(prev => prev.filter(x => x.name !== p.name)) }} className="text-[11px]" style={{ color: 'var(--red)' }}>Delete</button>
                  </div>
                </div>

                {/* Step flow */}
                <div className="flex items-center gap-0">
                  {p.steps.map((step, i) => {
                    const rc = RUNTIME_COLORS[step.runtime] || RUNTIME_COLORS.clove
                    return (
                      <div key={i} className="flex items-center">
                        <div className="flex flex-col items-center">
                          <div className="w-10 h-10 rounded-xl flex items-center justify-center text-[10px] font-bold" style={{ background: rc.bg, color: rc.fg }}>
                            {i + 1}
                          </div>
                          <span className="text-[10px] font-medium mt-1 max-w-[70px] text-center truncate">{step.name}</span>
                          <span className="text-[8px] mt-0.5" style={{ color: 'var(--text-dim)' }}>{step.runtime}</span>
                        </div>
                        {i < p.steps.length - 1 && <div className="w-6 h-[2px] mx-1" style={{ background: 'var(--border)', marginTop: -16 }} />}
                      </div>
                    )
                  })}
                </div>
              </div>
            ))}
          </div>
        )
      )}

      {/* ── CREATE ───────────────────────────────────────── */}
      {mode === 'create' && (
        <div>
          <div className="card p-5 mb-4">
            <label className="block text-[11px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Pipeline Name</label>
            <input value={newName} onChange={e => setNewName(e.target.value)} placeholder="pr-fixer"
              className="w-full rounded-lg px-3 py-2.5 text-[14px] outline-none placeholder:opacity-25"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>

          {/* Steps */}
          <div className="space-y-3 mb-4">
            {newSteps.map((step, idx) => {
              const rc = RUNTIME_COLORS[step.runtime] || RUNTIME_COLORS.clove
              return (
                <div key={idx} className="card p-4" style={{ borderLeft: `4px solid ${rc.fg}` }}>
                  <div className="flex items-center justify-between mb-3">
                    <div className="flex items-center gap-2">
                      <span className="w-6 h-6 rounded flex items-center justify-center text-[10px] font-bold" style={{ background: rc.bg, color: rc.fg }}>{idx + 1}</span>
                      <input value={step.name} onChange={e => updateStep(idx, { name: e.target.value })}
                        className="text-[13px] font-semibold outline-none bg-transparent" style={{ color: 'var(--text)' }} />
                    </div>
                    <button onClick={() => removeStep(idx)} className="text-[10px]" style={{ color: 'var(--red)' }}>Remove</button>
                  </div>
                  <div className="grid grid-cols-3 gap-3 mb-3">
                    <div>
                      <label className="block text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Runtime</label>
                      <select value={step.runtime} onChange={e => updateStep(idx, { runtime: e.target.value as PipelineStep['runtime'] })}
                        className="w-full rounded-lg px-2 py-1.5 text-[12px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}>
                        {RUNTIMES.map(r => <option key={r} value={r}>{r}</option>)}
                      </select>
                    </div>
                    <div>
                      <label className="block text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Budget</label>
                      <input type="number" step="0.05" value={step.budget} onChange={e => updateStep(idx, { budget: parseFloat(e.target.value) || 0.1 })}
                        className="w-full rounded-lg px-2 py-1.5 text-[12px] tabular-nums outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                    </div>
                    <div>
                      <label className="block text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Output Key</label>
                      <input value={step.output_key} onChange={e => updateStep(idx, { output_key: e.target.value })}
                        className="w-full rounded-lg px-2 py-1.5 text-[12px] mono outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                    </div>
                  </div>
                  <label className="block text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Role / Goal</label>
                  <textarea value={step.role} onChange={e => updateStep(idx, { role: e.target.value })} rows={2}
                    className="w-full rounded-lg px-3 py-2 text-[12px] resize-none outline-none placeholder:opacity-25"
                    style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} placeholder="What should this step do?" />
                  {idx > 0 && (
                    <div className="mt-2 text-[10px]" style={{ color: 'var(--text-dim)' }}>
                      Receives context from: <span className="font-medium" style={{ color: 'var(--accent)' }}>{step.input_keys?.join(', ') || 'none'}</span>
                    </div>
                  )}
                </div>
              )
            })}
          </div>

          <div className="flex items-center justify-between">
            <button onClick={addStep} className="text-[12px] font-medium flex items-center gap-1" style={{ color: 'var(--accent)' }}>+ Add Step</button>
            <div className="flex gap-2 items-center">
              <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{newSteps.length} steps · ${newSteps.reduce((s, st) => s + st.budget, 0).toFixed(2)} budget</span>
              <button onClick={savePipeline} disabled={!newName.trim() || newSteps.length === 0}
                className="px-5 py-2.5 rounded-lg text-[12px] font-semibold text-white disabled:opacity-30" style={{ background: 'var(--accent)' }}>Save Pipeline</button>
            </div>
          </div>
        </div>
      )}

      {/* ── RUN ──────────────────────────────────────────── */}
      {mode === 'run' && selected && (
        <div>
          {/* Pipeline header */}
          <div className="card p-4 mb-4">
            <div className="flex items-center justify-between">
              <span className="text-[14px] font-semibold">{selected.name}</span>
              {running ? (
                <div className="flex items-center gap-2">
                  <div className="w-2 h-2 rounded-full animate-pulse" style={{ background: 'var(--accent)' }} />
                  <span className="text-[12px] font-medium" style={{ color: 'var(--accent)' }}>Running</span>
                </div>
              ) : pipelineDone ? (
                <span className="text-[12px] font-semibold" style={{ color: pipelineDone.data.success ? 'var(--green)' : 'var(--red)' }}>
                  {pipelineDone.data.success ? 'Complete' : 'Failed'} — ${Number(pipelineDone.data.total_cost_usd || 0).toFixed(4)}
                </span>
              ) : null}
            </div>
            {!running && events.length === 0 && <div className="text-[12px] mt-1" style={{ color: 'var(--text-dim)' }}>Waiting to start...</div>}
          </div>

          {/* Step cards */}
          <div className="space-y-2">
            {selected.steps.map((step, idx) => {
              const rc = RUNTIME_COLORS[step.runtime] || RUNTIME_COLORS.clove
              const stepDone = stepsDone.find(e => Number(e.data.step) === idx + 1)
              const stepStarted = events.find(e => e.type === 'step_start' && Number(e.data.step) === idx + 1)
              const isActive = stepStarted && !stepDone
              const stepEvents = events.filter(e => e.type === 'step_event' && Number(e.data.step) === idx + 1)

              return (
                <div key={idx} className="card p-4" style={{ borderLeft: `4px solid ${stepDone ? (stepDone.data.success ? 'var(--green)' : 'var(--red)') : isActive ? rc.fg : 'var(--border)'}` }}>
                  <div className="flex items-center justify-between mb-2">
                    <div className="flex items-center gap-2.5">
                      <span className="w-7 h-7 rounded-lg flex items-center justify-center text-[11px] font-bold" style={{
                        background: stepDone ? (stepDone.data.success ? 'var(--green-light)' : 'var(--red-light)') : isActive ? rc.bg : 'var(--bg-raised)',
                        color: stepDone ? (stepDone.data.success ? 'var(--green)' : 'var(--red)') : isActive ? rc.fg : 'var(--text-dim)',
                      }}>
                        {stepDone ? (stepDone.data.success ? '✓' : '✗') : idx + 1}
                      </span>
                      <div>
                        <span className="text-[13px] font-semibold">{step.name}</span>
                        <span className="text-[11px] ml-2 font-medium px-1.5 py-0.5 rounded" style={{ background: rc.bg, color: rc.fg }}>{step.runtime}</span>
                      </div>
                      {isActive && <div className="w-1.5 h-1.5 rounded-full animate-pulse ml-1" style={{ background: rc.fg }} />}
                    </div>
                    {stepDone && (
                      <span className="text-[11px] tabular-nums" style={{ color: 'var(--text-dim)' }}>${Number(stepDone.data.cost_usd || 0).toFixed(4)}</span>
                    )}
                  </div>
                  <div className="text-[12px]" style={{ color: 'var(--text-dim)' }}>{step.role.slice(0, 100)}</div>

                  {/* Step output preview */}
                  {stepDone && stepDone.data.output_preview && (
                    <div className="mt-2 rounded-lg p-3 text-[11px] leading-relaxed" style={{ background: 'var(--bg)', color: 'var(--text-secondary)' }}>
                      {String(stepDone.data.output_preview).slice(0, 200)}
                    </div>
                  )}

                  {/* Live tool events */}
                  {isActive && stepEvents.length > 0 && (
                    <div className="mt-2 space-y-0.5">
                      {stepEvents.slice(-3).map((ev, i) => (
                        <div key={i} className="text-[10px] mono" style={{ color: 'var(--text-dim)' }}>
                          {ev.data.event_type === 'tool_call' ? `${ev.data.tool}(${JSON.stringify(ev.data.args || '').slice(0, 40)})` : String(ev.data.event_type)}
                        </div>
                      ))}
                    </div>
                  )}

                  {/* Context flow indicator */}
                  {step.input_keys && step.input_keys.length > 0 && (
                    <div className="mt-2 text-[10px]" style={{ color: 'var(--text-dim)' }}>
                      ← reads: {step.input_keys.map(k => <span key={String(k)} className="font-medium mx-0.5" style={{ color: 'var(--accent)' }}>{String(k)}</span>)}
                    </div>
                  )}
                </div>
              )
            })}
          </div>

          {/* Result */}
          {pipelineDone && (
            <div className="card p-5 mt-4" style={{ background: pipelineDone.data.success ? 'var(--green-light)' : 'var(--red-light)' }}>
              <div className="flex items-center justify-between">
                <span className="text-[13px] font-semibold" style={{ color: pipelineDone.data.success ? 'var(--green)' : 'var(--red)' }}>
                  {pipelineDone.data.success ? 'Pipeline Complete' : 'Pipeline Failed'}
                </span>
                <div className="flex gap-3 text-[11px] tabular-nums font-medium" style={{ color: pipelineDone.data.success ? 'var(--green)' : 'var(--red)' }}>
                  <span>{String(pipelineDone.data.steps_completed)}/{String(pipelineDone.data.steps_total)} steps</span>
                  <span>${Number(pipelineDone.data.total_cost_usd || 0).toFixed(4)}</span>
                  <span>{Number(pipelineDone.data.total_tokens || 0).toLocaleString()} tokens</span>
                </div>
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
