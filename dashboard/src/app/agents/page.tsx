'use client'
import { useEffect, useState } from 'react'
import Link from 'next/link'
import { useDemo } from '@/lib/demo-context'
import { getAgentDefs } from '@/lib/api'

/* ── Types ── */
interface Job {
  name: string
  trigger: { type: 'cron' | 'webhook' | 'manual' | 'agent_output'; schedule?: string; source?: string }
  pipeline: string[]
  output: string
  budget: number
  enabled: boolean
  last_run?: string
}

interface Agent {
  name: string; description: string; enabled: boolean; state: 'running' | 'idle' | 'stopped'
  runtime: 'clove' | 'claude-code' | 'codex' | 'openclaw'
  model: string
  triggers: Array<{ type: string; schedule?: string; source?: string }>
  connections: string[]; tools: string[]
  budget: { per_run: number; daily_max: number; daily_spent: number }
  runs_today: number; last_run?: string; success_rate: number
  sandbox: { can_exec: boolean; can_http: boolean; can_write: boolean; allowed_domains: string[]; allowed_paths: string[] }
  mcp_servers: string[]
  jobs: Job[]
  run_history: Array<{ id: string; trigger: string; status: 'success' | 'failed'; cost: number; duration: string; time: string; steps: number }>
}

/* ── Demo data ── */
const DEMO: Agent[] = [
  {
    name: 'pr-reviewer', description: 'Reviews new PRs for security and code quality', enabled: true, state: 'running',
    runtime: 'claude-code', model: 'claude-sonnet-4',
    triggers: [{ type: 'webhook', source: 'github' }], connections: ['github'], tools: ['read_file', 'exec', 'mcp_call'],
    budget: { per_run: 0.30, daily_max: 10, daily_spent: 2.4 }, runs_today: 8, last_run: '2 min ago', success_rate: 94,
    sandbox: { can_exec: true, can_http: true, can_write: false, allowed_domains: ['api.github.com'], allowed_paths: ['/tmp'] },
    mcp_servers: ['github'],
    jobs: [
      { name: 'Review on PR open', trigger: { type: 'webhook', source: 'github pull_request.opened' }, pipeline: ['read diff', 'analyze', 'comment'], output: 'GitHub PR comment', budget: 0.30, enabled: true, last_run: '2 min ago' },
      { name: 'Nightly full scan', trigger: { type: 'cron', schedule: '0 2 * * *' }, pipeline: ['list open PRs', 'review each', 'summary report'], output: 'Slack #code-review', budget: 1.00, enabled: true, last_run: '6 hours ago' },
    ],
    run_history: [
      { id: 'r-001', trigger: 'webhook: github', status: 'success', cost: 0.28, duration: '14s', time: '2 min ago', steps: 4 },
      { id: 'r-002', trigger: 'webhook: github', status: 'success', cost: 0.31, duration: '18s', time: '25 min ago', steps: 5 },
      { id: 'r-003', trigger: 'cron', status: 'success', cost: 0.82, duration: '45s', time: '6 hours ago', steps: 12 },
      { id: 'r-004', trigger: 'webhook: github', status: 'failed', cost: 0.12, duration: '8s', time: '8 hours ago', steps: 2 },
    ],
  },
  {
    name: 'security-auditor', description: 'Scans dependencies for vulnerabilities, opens fix PRs', enabled: true, state: 'running',
    runtime: 'clove', model: 'gemini-2.5-flash',
    triggers: [{ type: 'cron', schedule: '0 6 * * *' }], connections: ['github'], tools: ['exec', 'search', 'http', 'store'],
    budget: { per_run: 0.15, daily_max: 2, daily_spent: 0.10 }, runs_today: 1, last_run: '3 hours ago', success_rate: 100,
    sandbox: { can_exec: true, can_http: true, can_write: true, allowed_domains: ['api.github.com', 'registry.npmjs.org'], allowed_paths: ['/app'] },
    mcp_servers: ['github'],
    jobs: [
      { name: 'Daily CVE scan', trigger: { type: 'cron', schedule: '0 6 * * *' }, pipeline: ['scan deps', 'check CVEs', 'open fix PRs'], output: 'Slack #security', budget: 0.15, enabled: true, last_run: '3 hours ago' },
    ],
    run_history: [
      { id: 'r-010', trigger: 'cron', status: 'success', cost: 0.10, duration: '22s', time: '3 hours ago', steps: 6 },
    ],
  },
  {
    name: 'incident-responder', description: 'Diagnoses production incidents and proposes fixes', enabled: true, state: 'idle',
    runtime: 'claude-code', model: 'claude-sonnet-4',
    triggers: [{ type: 'webhook', source: 'sentry' }], connections: ['github', 'slack'], tools: ['exec', 'read_file', 'http', 'mcp_call', 'delegate'],
    budget: { per_run: 1.50, daily_max: 15, daily_spent: 0 }, runs_today: 0, last_run: 'yesterday', success_rate: 85,
    sandbox: { can_exec: true, can_http: true, can_write: true, allowed_domains: ['api.github.com', 'api.slack.com'], allowed_paths: ['/app', '/tmp'] },
    mcp_servers: ['github', 'slack'],
    jobs: [
      { name: 'On Sentry alert', trigger: { type: 'webhook', source: 'sentry alert' }, pipeline: ['diagnose', 'find fix', 'open PR', 'notify'], output: 'GitHub PR + Slack alert', budget: 1.50, enabled: true },
    ],
    run_history: [],
  },
  {
    name: 'doc-generator', description: 'Reads codebase, writes and updates API docs after each merge', enabled: false, state: 'stopped',
    runtime: 'codex', model: 'codex',
    triggers: [{ type: 'webhook', source: 'github push' }], connections: ['github'], tools: ['read_file', 'write_file', 'search'],
    budget: { per_run: 0.10, daily_max: 1, daily_spent: 0 }, runs_today: 0, last_run: '2 days ago', success_rate: 92,
    sandbox: { can_exec: false, can_http: false, can_write: true, allowed_domains: [], allowed_paths: ['/docs'] },
    mcp_servers: ['github'],
    jobs: [
      { name: 'Update docs on merge', trigger: { type: 'webhook', source: 'github push to main' }, pipeline: ['read diffs', 'rewrite docs', 'commit'], output: 'Git commit to docs/', budget: 0.10, enabled: false },
    ],
    run_history: [],
  },
]

const STATE_STYLE: Record<string, { bg: string; fg: string; label: string }> = {
  running: { bg: 'var(--green-light)', fg: 'var(--green)', label: 'Running' },
  idle: { bg: 'var(--yellow-light)', fg: 'var(--yellow)', label: 'Idle' },
  stopped: { bg: 'var(--bg-raised)', fg: 'var(--text-dim)', label: 'Stopped' },
}

const RUNTIME_STYLE: Record<string, { color: string; label: string }> = {
  'clove': { color: 'var(--text-dim)', label: 'CLOVE' },
  'claude-code': { color: '#D4A27A', label: 'Claude Code' },
  'codex': { color: '#7C8CF7', label: 'Codex' },
  'openclaw': { color: '#6BCF8E', label: 'OpenClaw' },
}

const TRIGGER_STYLE: Record<string, { bg: string; fg: string }> = {
  cron: { bg: 'var(--blue-light)', fg: 'var(--blue)' },
  webhook: { bg: 'var(--accent-light)', fg: 'var(--accent)' },
  manual: { bg: 'var(--bg-raised)', fg: 'var(--text-dim)' },
  agent_output: { bg: 'var(--green-light)', fg: 'var(--green)' },
}

type Tab = 'config' | 'jobs' | 'runs' | 'logs'

export default function AgentsPage() {
  const { isDemo } = useDemo()
  const [agents, setAgents] = useState<Agent[]>([])
  const [selected, setSelected] = useState<Agent | null>(null)
  const [tab, setTab] = useState<Tab>('config')

  useEffect(() => {
    if (isDemo) { setAgents(DEMO); return }
    const load = () => {
      getAgentDefs().then(r => {
        const defs = (r.agents || []).map((a: any) => ({
          ...a, state: a.enabled ? 'running' as const : 'stopped' as const,
          runtime: a.action?.runtime || 'clove', model: a.action?.model || '',
          tools: a.action?.tools || [], budget: { per_run: a.budget?.per_run || 0, daily_max: a.budget?.daily_max || 0, daily_spent: a.budget?.daily_spent || 0 },
          runs_today: 0, success_rate: 100, sandbox: a.permissions || { can_exec: false, can_http: false, can_write: false, allowed_domains: [], allowed_paths: [] },
          mcp_servers: [], jobs: [], run_history: [],
        }))
        setAgents(defs)
      }).catch(() => {})
    }
    load(); const i = setInterval(load, 5000); return () => clearInterval(i)
  }, [isDemo])

  const running = agents.filter(a => a.state === 'running').length
  const totalSpend = agents.reduce((s, a) => s + a.budget.daily_spent, 0)
  const totalJobs = agents.reduce((s, a) => s + (a.jobs?.length || 0), 0)

  return (
    <div className="flex gap-0 -mx-10 -my-8 h-[calc(100vh)]">
      {/* ── Left: Agent list ── */}
      <div className="flex-1 overflow-y-auto px-8 py-6">
        <div className="flex items-center justify-between mb-6">
          <div>
            <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Agents</h2>
            <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>
              {agents.length} agents · {running} running · {totalJobs} jobs · ${totalSpend.toFixed(2)} today
            </p>
          </div>
          <Link href="/agents/new" className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>
            + New Agent
          </Link>
        </div>

        {/* Quick stats */}
        <div className="grid grid-cols-4 gap-2 mb-6">
          {[
            { label: 'Agents', value: String(agents.length), color: 'var(--text)' },
            { label: 'Running', value: String(running), color: 'var(--green)' },
            { label: 'Jobs', value: String(totalJobs), color: 'var(--blue)' },
            { label: 'Spent Today', value: `$${totalSpend.toFixed(2)}`, color: 'var(--accent)' },
          ].map(s => (
            <div key={s.label} className="card px-4 py-3">
              <div className="text-[9px] uppercase tracking-[0.08em] font-medium" style={{ color: 'var(--text-dim)' }}>{s.label}</div>
              <div className="text-[17px] font-bold tabular-nums mt-0.5" style={{ color: s.color }}>{s.value}</div>
            </div>
          ))}
        </div>

        {/* Agent list */}
        {agents.length === 0 ? (
          <div className="card py-20 text-center">
            <div className="text-[15px] font-semibold mb-1">No agents yet</div>
            <p className="text-[13px] mb-5" style={{ color: 'var(--text-dim)' }}>Create your first agent to automate tasks.</p>
            <Link href="/agents/new" className="inline-block px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Create your first agent</Link>
          </div>
        ) : (
          <div className="space-y-2">
            {agents.map(agent => {
              const ss = STATE_STYLE[agent.state]
              const rt = RUNTIME_STYLE[agent.runtime] || RUNTIME_STYLE['clove']
              const isSelected = selected?.name === agent.name
              return (
                <button key={agent.name} onClick={() => { setSelected(agent); setTab('config') }}
                  className="card w-full text-left p-4 transition-all hover:shadow-md"
                  style={{ borderColor: isSelected ? 'var(--accent)' : undefined, borderWidth: isSelected ? 2 : 1 }}>
                  <div className="flex items-center gap-4">
                    {/* Status dot */}
                    <div className="w-[40px] h-[40px] rounded-xl flex items-center justify-center flex-shrink-0" style={{ background: ss.bg }}>
                      <div className={`w-2.5 h-2.5 rounded-full ${agent.state === 'running' ? 'animate-pulse' : ''}`} style={{ background: ss.fg }} />
                    </div>

                    {/* Info */}
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-[14px] font-semibold">{agent.name}</span>
                        <span className="text-[10px] font-semibold px-1.5 py-[2px] rounded-full" style={{ background: ss.bg, color: ss.fg }}>{ss.label}</span>
                        <span className="text-[10px] font-medium px-1.5 py-[2px] rounded-full" style={{ background: `${rt.color}15`, color: rt.color }}>{rt.label}</span>
                      </div>
                      <div className="text-[12px] mt-0.5 truncate" style={{ color: 'var(--text-dim)' }}>{agent.description}</div>
                    </div>

                    {/* Jobs count */}
                    <div className="text-center flex-shrink-0">
                      <div className="text-[14px] font-semibold" style={{ color: 'var(--blue)' }}>{agent.jobs?.length || 0}</div>
                      <div className="text-[10px]" style={{ color: 'var(--text-dim)' }}>jobs</div>
                    </div>

                    {/* Triggers */}
                    <div className="flex flex-col gap-1 flex-shrink-0">
                      {agent.triggers.map((t, i) => {
                        const ts = TRIGGER_STYLE[t.type] || TRIGGER_STYLE.manual
                        return (
                          <span key={i} className="text-[10px] font-medium px-2 py-[2px] rounded-full" style={{ background: ts.bg, color: ts.fg }}>
                            {t.type === 'cron' ? t.schedule : t.source || t.type}
                          </span>
                        )
                      })}
                    </div>

                    {/* Stats */}
                    <div className="flex items-center gap-4 flex-shrink-0 text-[11px] tabular-nums" style={{ color: 'var(--text-dim)' }}>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--text)' }}>{agent.runs_today}</div><div>runs</div></div>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--accent)' }}>${agent.budget.daily_spent.toFixed(2)}</div><div>spent</div></div>
                    </div>
                  </div>
                </button>
              )
            })}
          </div>
        )}
      </div>

      {/* ── Right: Detail panel with tabs ── */}
      {selected && (
        <div className="w-[420px] overflow-y-auto border-l flex-shrink-0" style={{ background: 'var(--bg)', borderColor: 'var(--border)' }}>
          {/* Header */}
          <div className="px-6 py-4 sticky top-0 z-10" style={{ background: 'var(--bg)', borderBottom: '1px solid var(--border)' }}>
            <div className="flex items-center justify-between mb-3">
              <div className="flex items-center gap-2">
                <div className={`w-2.5 h-2.5 rounded-full ${selected.state === 'running' ? 'animate-pulse' : ''}`} style={{ background: STATE_STYLE[selected.state].fg }} />
                <h3 className="text-[15px] font-semibold">{selected.name}</h3>
              </div>
              <button onClick={() => setSelected(null)} className="text-[12px]" style={{ color: 'var(--text-dim)' }}>Close</button>
            </div>
            {/* Tabs */}
            <div className="flex gap-1">
              {(['config', 'jobs', 'runs', 'logs'] as Tab[]).map(t => (
                <button key={t} onClick={() => setTab(t)}
                  className="px-3 py-1.5 rounded-lg text-[11px] font-medium capitalize transition-all"
                  style={{ background: tab === t ? 'var(--accent-light)' : 'transparent', color: tab === t ? 'var(--accent)' : 'var(--text-dim)' }}>
                  {t}
                </button>
              ))}
            </div>
          </div>

          <div className="px-6 py-4">
            {tab === 'config' && <ConfigTab agent={selected} />}
            {tab === 'jobs' && <JobsTab agent={selected} />}
            {tab === 'runs' && <RunsTab agent={selected} />}
            {tab === 'logs' && <LogsTab agent={selected} />}
          </div>
        </div>
      )}
    </div>
  )
}

/* ── Config Tab ── */
function ConfigTab({ agent }: { agent: Agent }) {
  const rt = RUNTIME_STYLE[agent.runtime] || RUNTIME_STYLE['clove']
  return (
    <div className="space-y-5">
      <p className="text-[13px]" style={{ color: 'var(--text-secondary)' }}>{agent.description}</p>

      <DS title="Runtime & Model">
        <div className="flex items-center gap-3 px-3 py-2.5 rounded-lg" style={{ background: 'var(--bg-raised)' }}>
          <div className="w-2.5 h-2.5 rounded-full" style={{ background: rt.color }} />
          <div>
            <div className="text-[12px] font-medium">{rt.label}</div>
            <div className="text-[11px] mono" style={{ color: 'var(--text-dim)' }}>{agent.model}</div>
          </div>
        </div>
      </DS>

      <DS title="Budget">
        <div className="flex items-end justify-between mb-2">
          <span className="text-[20px] font-bold tabular-nums" style={{ color: 'var(--accent)' }}>${agent.budget.daily_spent.toFixed(2)}</span>
          <span className="text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>/ ${agent.budget.daily_max.toFixed(2)} daily</span>
        </div>
        <div className="h-[6px] rounded-full overflow-hidden" style={{ background: 'var(--bg-raised)' }}>
          <div className="h-full rounded-full transition-all" style={{ width: `${Math.min((agent.budget.daily_spent / agent.budget.daily_max) * 100, 100)}%`, background: agent.budget.daily_spent / agent.budget.daily_max > 0.8 ? 'var(--red)' : 'var(--accent)' }} />
        </div>
        <div className="flex justify-between mt-2 text-[11px]" style={{ color: 'var(--text-dim)' }}>
          <span>${agent.budget.per_run.toFixed(2)} per run</span>
          <span>{agent.runs_today} runs today</span>
        </div>
      </DS>

      <DS title="Connections">
        {agent.connections.length === 0 && agent.mcp_servers.length === 0 ? (
          <div className="text-[12px]" style={{ color: 'var(--text-dim)' }}>No connections</div>
        ) : (
          <div className="space-y-1.5">
            {agent.connections.map(c => (
              <div key={c} className="flex items-center gap-2 px-3 py-2 rounded-lg" style={{ background: 'var(--bg-raised)' }}>
                <div className="w-2 h-2 rounded-full" style={{ background: 'var(--green)' }} />
                <span className="text-[12px] font-medium capitalize">{c}</span>
                <span className="text-[10px] ml-auto" style={{ color: 'var(--text-dim)' }}>MCP</span>
              </div>
            ))}
          </div>
        )}
      </DS>

      <DS title="Tools">
        <div className="flex flex-wrap gap-1.5">
          {agent.tools.map(t => (
            <span key={t} className="text-[10px] font-medium px-2 py-1 rounded-lg" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{t}</span>
          ))}
        </div>
      </DS>

      <DS title="Sandbox">
        <div className="grid grid-cols-3 gap-2">
          {[
            { label: 'Shell', allowed: agent.sandbox.can_exec },
            { label: 'HTTP', allowed: agent.sandbox.can_http },
            { label: 'Write', allowed: agent.sandbox.can_write },
          ].map(p => (
            <div key={p.label} className="flex items-center gap-2 text-[12px]">
              <span style={{ color: p.allowed ? 'var(--green)' : 'var(--red)' }}>{p.allowed ? '✓' : '✗'}</span>
              <span style={{ color: p.allowed ? 'var(--text)' : 'var(--text-dim)' }}>{p.label}</span>
            </div>
          ))}
        </div>
      </DS>

      <div className="flex gap-2 pt-2">
        <button className="flex-1 px-4 py-2.5 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Run Now</button>
        <button className="px-4 py-2.5 rounded-lg text-[12px] font-medium" style={{ background: 'var(--bg-raised)', color: 'var(--text-secondary)' }}>Edit</button>
        <button className="px-4 py-2.5 rounded-lg text-[12px] font-medium" style={{ color: 'var(--red)' }}>Delete</button>
      </div>
    </div>
  )
}

/* ── Jobs Tab ── */
function JobsTab({ agent }: { agent: Agent }) {
  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between mb-2">
        <span className="text-[13px] font-medium">{agent.jobs.length} job{agent.jobs.length !== 1 ? 's' : ''}</span>
        <button className="text-[11px] font-semibold px-3 py-1.5 rounded-lg" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>+ Add Job</button>
      </div>

      {agent.jobs.length === 0 ? (
        <div className="py-10 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>
          No jobs. Add a job to run this agent on a schedule or trigger.
        </div>
      ) : agent.jobs.map((job, i) => {
        const ts = TRIGGER_STYLE[job.trigger.type] || TRIGGER_STYLE.manual
        return (
          <div key={i} className="card p-4">
            <div className="flex items-center justify-between mb-2">
              <span className="text-[13px] font-semibold">{job.name}</span>
              <div className="flex items-center gap-2">
                <span className="text-[10px] font-medium px-2 py-[2px] rounded-full" style={{ background: ts.bg, color: ts.fg }}>
                  {job.trigger.type === 'cron' ? job.trigger.schedule : job.trigger.source || job.trigger.type}
                </span>
                <div className="w-[26px] h-[14px] rounded-full p-[2px]" style={{ background: job.enabled ? 'var(--green)' : 'var(--border)' }}>
                  <div className="w-[10px] h-[10px] rounded-full" style={{ background: '#fff', transform: job.enabled ? 'translateX(12px)' : 'translateX(0)', transition: 'transform 150ms' }} />
                </div>
              </div>
            </div>

            {/* Pipeline steps */}
            <div className="flex items-center gap-1.5 mb-2">
              {job.pipeline.map((step, si) => (
                <div key={si} className="flex items-center gap-1.5">
                  <span className="text-[10px] px-2 py-1 rounded-md" style={{ background: 'var(--bg-raised)', color: 'var(--text-secondary)' }}>{step}</span>
                  {si < job.pipeline.length - 1 && <span className="text-[10px]" style={{ color: 'var(--text-dim)' }}>→</span>}
                </div>
              ))}
            </div>

            <div className="flex items-center gap-3 text-[10px]" style={{ color: 'var(--text-dim)' }}>
              <span>Output: {job.output}</span>
              <span>·</span>
              <span>${job.budget.toFixed(2)}/run</span>
              {job.last_run && <><span>·</span><span>Last: {job.last_run}</span></>}
            </div>
          </div>
        )
      })}
    </div>
  )
}

/* ── Runs Tab ── */
function RunsTab({ agent }: { agent: Agent }) {
  return (
    <div className="space-y-2">
      <div className="mb-2">
        <span className="text-[13px] font-medium">{agent.run_history.length} runs</span>
      </div>

      {agent.run_history.length === 0 ? (
        <div className="py-10 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No runs yet.</div>
      ) : agent.run_history.map(run => (
        <div key={run.id} className="card p-3">
          <div className="flex items-center justify-between mb-1">
            <div className="flex items-center gap-2">
              <div className="w-2 h-2 rounded-full" style={{ background: run.status === 'success' ? 'var(--green)' : 'var(--red)' }} />
              <span className="text-[12px] font-medium mono">{run.id}</span>
            </div>
            <span className="text-[10px]" style={{ color: 'var(--text-dim)' }}>{run.time}</span>
          </div>
          <div className="flex items-center gap-3 text-[10px]" style={{ color: 'var(--text-dim)' }}>
            <span>{run.trigger}</span>
            <span>·</span>
            <span>{run.steps} steps</span>
            <span>·</span>
            <span>{run.duration}</span>
            <span className="ml-auto tabular-nums font-medium" style={{ color: run.status === 'success' ? 'var(--green)' : 'var(--red)' }}>${run.cost.toFixed(2)}</span>
          </div>
        </div>
      ))}
    </div>
  )
}

/* ── Logs Tab ── */
function LogsTab({ agent }: { agent: Agent }) {
  return (
    <div>
      <div className="rounded-lg p-4 font-mono text-[11px] leading-[1.8]" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>
        <div><span style={{ color: 'var(--text-dim)' }}>[{agent.last_run || 'no runs'}]</span> Agent <span style={{ color: 'var(--text)' }}>{agent.name}</span> started</div>
        {agent.tools.slice(0, 3).map((t, i) => (
          <div key={i}><span style={{ color: 'var(--blue)' }}>[tool]</span> {t} called</div>
        ))}
        {agent.state === 'running' && (
          <div className="flex items-center gap-2 mt-2">
            <span style={{ color: 'var(--green)' }}>●</span>
            <span>Listening for triggers...</span>
          </div>
        )}
        {agent.state === 'idle' && <div style={{ color: 'var(--yellow)' }}>Agent idle — waiting for next trigger</div>}
        {agent.state === 'stopped' && <div style={{ color: 'var(--red)' }}>Agent stopped</div>}
      </div>
      <p className="text-[11px] mt-3 text-center" style={{ color: 'var(--text-dim)' }}>Live logs available when agent is running</p>
    </div>
  )
}

/* ── Detail Section helper ── */
function DS({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div>
      <div className="text-[10px] uppercase tracking-[0.06em] font-semibold mb-2" style={{ color: 'var(--text-dim)' }}>{title}</div>
      {children}
    </div>
  )
}
