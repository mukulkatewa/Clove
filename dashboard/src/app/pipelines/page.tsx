'use client'
import { useState } from 'react'
import { useDemo } from '@/lib/demo-context'

interface Pipeline {
  name: string; description: string; agents: number; category: string
  last_run?: string; runs: number; avg_cost: number; success_rate: number
  agent_roles: Array<{ name: string; role: string }>
}

const DEMO: Pipeline[] = [
  { name: 'Code Health Check', description: 'Security scanner + dependency checker + quality reviewer → synthesized report', agents: 4, category: 'dev',
    last_run: '2 hours ago', runs: 12, avg_cost: 0.43, success_rate: 95,
    agent_roles: [{ name: 'security-scanner', role: 'Find vulnerabilities' }, { name: 'dep-checker', role: 'Audit dependencies' }, { name: 'quality-reviewer', role: 'Review code quality' }, { name: 'synthesizer', role: 'Merge findings' }] },
  { name: 'Incident Response', description: 'Sentinel detects → diagnostician analyzes → fixer applies → verifier confirms', agents: 4, category: 'ops',
    last_run: '6 hours ago', runs: 3, avg_cost: 1.85, success_rate: 80,
    agent_roles: [{ name: 'sentinel', role: 'Detect anomaly' }, { name: 'diagnostician', role: 'Root cause analysis' }, { name: 'fixer', role: 'Apply remediation' }, { name: 'verifier', role: 'Confirm fix' }] },
  { name: 'Research Station', description: '3 researchers explore different angles → analyst synthesizes → writer produces report', agents: 5, category: 'research',
    last_run: 'yesterday', runs: 8, avg_cost: 0.92, success_rate: 98,
    agent_roles: [{ name: 'researcher-1', role: 'Primary research' }, { name: 'researcher-2', role: 'Competitive analysis' }, { name: 'researcher-3', role: 'Market data' }, { name: 'analyst', role: 'Synthesize' }, { name: 'writer', role: 'Final report' }] },
  { name: 'CI/CD Guardian', description: 'Watcher monitors PRs → tester runs suite → reviewer analyzes failures → reporter posts to Slack', agents: 4, category: 'dev',
    runs: 0, avg_cost: 0, success_rate: 0,
    agent_roles: [{ name: 'watcher', role: 'Monitor PRs' }, { name: 'tester', role: 'Run tests' }, { name: 'reviewer', role: 'Analyze failures' }, { name: 'reporter', role: 'Post results' }] },
]

const CAT_COLORS: Record<string, { bg: string; fg: string }> = {
  dev: { bg: 'var(--accent-light)', fg: 'var(--accent)' },
  ops: { bg: 'var(--red-light)', fg: 'var(--red)' },
  research: { bg: 'var(--blue-light)', fg: 'var(--blue)' },
}

export default function PipelinesPage() {
  const { isDemo } = useDemo()
  const pipelines = isDemo ? DEMO : []
  const [expanded, setExpanded] = useState<string | null>(null)

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Pipelines</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Multi-agent workflows. Each pipeline is a coordinated team of specialized agents.</p>
        </div>
        <button className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>+ New Pipeline</button>
      </div>

      {pipelines.length === 0 ? (
        <div className="card py-20 text-center">
          <div className="text-[15px] font-semibold mb-1">No pipelines</div>
          <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>Pipelines coordinate multiple agents on complex tasks.</p>
        </div>
      ) : (
        <div className="space-y-3">
          {pipelines.map(p => {
            const isOpen = expanded === p.name
            const cc = CAT_COLORS[p.category] || { bg: 'var(--bg-raised)', fg: 'var(--text-dim)' }
            return (
              <div key={p.name} className="card overflow-hidden">
                <button onClick={() => setExpanded(isOpen ? null : p.name)} className="w-full text-left p-5 hover:bg-[var(--bg)] transition-colors">
                  <div className="flex items-center gap-4">
                    {/* Pipeline visual */}
                    <div className="flex items-center flex-shrink-0">
                      {p.agent_roles.slice(0, 4).map((_, i) => (
                        <div key={i} className="w-8 h-8 rounded-full flex items-center justify-center text-[10px] font-bold border-2 -ml-2 first:ml-0"
                          style={{ background: 'var(--bg-card)', borderColor: 'var(--border)', color: 'var(--accent)', zIndex: 4 - i }}>
                          {i + 1}
                        </div>
                      ))}
                      {p.agents > 4 && <div className="w-8 h-8 rounded-full flex items-center justify-center text-[9px] -ml-2" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>+{p.agents - 4}</div>}
                    </div>

                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-[14px] font-semibold">{p.name}</span>
                        <span className="text-[9px] font-semibold uppercase px-1.5 py-[2px] rounded-full" style={{ background: cc.bg, color: cc.fg }}>{p.category}</span>
                      </div>
                      <div className="text-[12px] mt-0.5 truncate" style={{ color: 'var(--text-dim)' }}>{p.description}</div>
                    </div>

                    <div className="flex gap-5 text-[11px] tabular-nums flex-shrink-0" style={{ color: 'var(--text-dim)' }}>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--text)' }}>{p.runs}</div>runs</div>
                      <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--accent)' }}>${p.avg_cost.toFixed(2)}</div>avg</div>
                      {p.success_rate > 0 && <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--green)' }}>{p.success_rate}%</div>success</div>}
                    </div>

                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ color: 'var(--text-dim)', transform: isOpen ? 'rotate(180deg)' : '', transition: '150ms' }}><polyline points="6 9 12 15 18 9"/></svg>
                  </div>
                </button>

                {isOpen && (
                  <div className="px-5 pb-5" style={{ borderTop: '1px solid var(--border)' }}>
                    {/* Agent flow visualization */}
                    <div className="py-4">
                      <div className="flex items-center gap-0">
                        {p.agent_roles.map((agent, i) => (
                          <div key={i} className="flex items-center">
                            <div className="flex flex-col items-center">
                              <div className="w-12 h-12 rounded-xl flex items-center justify-center text-[11px] font-bold" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>
                                {i + 1}
                              </div>
                              <div className="text-[10px] font-medium mt-1.5 text-center max-w-[80px]">{agent.name}</div>
                              <div className="text-[9px] mt-0.5 text-center max-w-[80px]" style={{ color: 'var(--text-dim)' }}>{agent.role}</div>
                            </div>
                            {i < p.agent_roles.length - 1 && (
                              <div className="flex items-center mx-2 mt-[-20px]">
                                <div className="w-8 h-[2px]" style={{ background: 'var(--border)' }} />
                                <svg width="8" height="8" viewBox="0 0 8 8" style={{ color: 'var(--border)' }}><path d="M0 0L8 4L0 8" fill="currentColor" /></svg>
                              </div>
                            )}
                          </div>
                        ))}
                      </div>
                    </div>

                    <div className="flex gap-2 pt-2">
                      <button className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Launch</button>
                      <button className="px-4 py-2 rounded-lg text-[12px] font-medium" style={{ background: 'var(--bg-raised)', color: 'var(--text-secondary)' }}>Configure</button>
                    </div>
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
