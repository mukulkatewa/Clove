'use client'
import { useState } from 'react'
import Link from 'next/link'

interface Template {
  name: string; description: string; category: 'agent' | 'pipeline'
  runtime: string; tools: string[]; trigger: string; estimated_cost: string
}

const TEMPLATES: Template[] = [
  { name: 'PR Reviewer', description: 'Reviews every PR for bugs, security issues, and code quality. Comments inline on GitHub.', category: 'agent', runtime: 'Claude Code', tools: ['read_file', 'mcp_github'], trigger: 'webhook: github PR', estimated_cost: '~$0.12/run' },
  { name: 'Security Scanner', description: 'Daily CVE scan of all dependencies. Opens fix PRs automatically.', category: 'agent', runtime: 'CLOVE', tools: ['exec', 'search', 'http'], trigger: 'cron: daily 6 AM', estimated_cost: '~$0.10/run' },
  { name: 'Research Analyst', description: 'Monitors competitors, scrapes news, and posts a daily brief to Slack.', category: 'agent', runtime: 'CLOVE', tools: ['search', 'http', 'remember'], trigger: 'cron: daily 9 AM', estimated_cost: '~$0.08/run' },
  { name: 'Incident Responder', description: 'On Sentry alert: diagnose, find fix, open PR, notify Slack.', category: 'agent', runtime: 'Claude Code', tools: ['exec', 'read_file', 'mcp_sentry', 'mcp_github'], trigger: 'webhook: sentry', estimated_cost: '~$0.25/run' },
  { name: 'Doc Generator', description: 'After each merge to main, reads diffs and rewrites API docs.', category: 'agent', runtime: 'Codex', tools: ['read_file', 'write_file', 'search'], trigger: 'webhook: github push', estimated_cost: '~$0.06/run' },
  { name: 'Full Code Review Pipeline', description: '4-step pipeline: scan deps → review code → check tests → post summary.', category: 'pipeline', runtime: 'Mixed', tools: ['exec', 'read_file', 'mcp_github'], trigger: 'webhook: github PR', estimated_cost: '~$0.38/run' },
  { name: 'Research & Brief Pipeline', description: '3-step: web research → competitive analysis → Slack brief.', category: 'pipeline', runtime: 'CLOVE', tools: ['search', 'http', 'mcp_slack'], trigger: 'cron: daily', estimated_cost: '~$0.24/run' },
  { name: 'Incident Response Pipeline', description: '4-step: detect → diagnose → fix → notify. Multi-runtime.', category: 'pipeline', runtime: 'Mixed', tools: ['exec', 'http', 'mcp_sentry', 'mcp_github', 'mcp_slack'], trigger: 'webhook: sentry', estimated_cost: '~$0.50/run' },
]

export default function TemplatesPage() {
  const [filter, setFilter] = useState<'all' | 'agent' | 'pipeline'>('all')
  const filtered = filter === 'all' ? TEMPLATES : TEMPLATES.filter(t => t.category === filter)

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Templates</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Pre-built agents and pipelines. One click to deploy.</p>
        </div>
      </div>

      {/* Filter */}
      <div className="flex gap-1 mb-6">
        {(['all', 'agent', 'pipeline'] as const).map(f => (
          <button key={f} onClick={() => setFilter(f)}
            className="px-3 py-1.5 rounded-lg text-[11px] font-medium capitalize transition-all"
            style={{ background: filter === f ? 'var(--accent-light)' : 'transparent', color: filter === f ? 'var(--accent)' : 'var(--text-dim)' }}>
            {f === 'all' ? 'All' : f + 's'}
          </button>
        ))}
      </div>

      {/* Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-3">
        {filtered.map(t => (
          <div key={t.name} className="card p-5 hover:shadow-md transition-all">
            <div className="flex items-center gap-2 mb-2">
              <span className="text-[10px] font-semibold px-1.5 py-[2px] rounded-full uppercase"
                style={{ background: t.category === 'agent' ? 'var(--accent-light)' : 'var(--blue-light)', color: t.category === 'agent' ? 'var(--accent)' : 'var(--blue)' }}>
                {t.category}
              </span>
              <span className="text-[10px] font-medium px-1.5 py-[2px] rounded-full" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>
                {t.runtime}
              </span>
            </div>
            <h3 className="text-[14px] font-semibold mb-1">{t.name}</h3>
            <p className="text-[12px] mb-3 line-clamp-2" style={{ color: 'var(--text-dim)' }}>{t.description}</p>

            <div className="flex flex-wrap gap-1 mb-3">
              {t.tools.slice(0, 4).map(tool => (
                <span key={tool} className="text-[9px] font-medium px-1.5 py-0.5 rounded mono" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>{tool}</span>
              ))}
            </div>

            <div className="flex items-center justify-between">
              <div className="text-[10px]" style={{ color: 'var(--text-dim)' }}>
                <span>{t.trigger}</span> · <span>{t.estimated_cost}</span>
              </div>
              <button className="px-3 py-1.5 rounded-lg text-[11px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Deploy</button>
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}
