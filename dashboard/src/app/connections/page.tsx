'use client'
import { useDemo } from '@/lib/demo-context'

interface Connection { name: string; type: 'mcp' | 'http'; status: 'active' | 'inactive'; tools: number; agents_using: string[]; description: string }

const DEMO: Connection[] = [
  { name: 'GitHub', type: 'mcp', status: 'active', tools: 12, agents_using: ['pr-reviewer', 'incident-responder', 'dep-scanner'], description: 'Repository access, PR management, issue tracking' },
  { name: 'Slack', type: 'mcp', status: 'active', tools: 8, agents_using: ['incident-responder'], description: 'Messaging, channel management, notifications' },
  { name: 'PostgreSQL', type: 'mcp', status: 'active', tools: 6, agents_using: [], description: 'Database queries, table management' },
  { name: 'Filesystem', type: 'mcp', status: 'active', tools: 5, agents_using: ['pr-reviewer', 'dep-scanner'], description: 'Local file read/write access' },
  { name: 'PagerDuty', type: 'http', status: 'inactive', tools: 0, agents_using: ['incident-responder'], description: 'Alert management, incident routing' },
  { name: 'Notion', type: 'mcp', status: 'inactive', tools: 0, agents_using: [], description: 'Documentation, wikis, databases' },
]

export default function ConnectionsPage() {
  const { isDemo } = useDemo()
  const connections = isDemo ? DEMO : []
  const active = connections.filter(c => c.status === 'active')

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Connections</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>
            External services your agents can access. {active.length} active connections.
          </p>
        </div>
        <button className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>+ Connect Service</button>
      </div>

      {connections.length === 0 ? (
        <div className="card py-20 text-center">
          <div className="text-[15px] font-semibold mb-1">No connections</div>
          <p className="text-[13px] mb-4" style={{ color: 'var(--text-dim)' }}>Connect GitHub, Slack, databases, and more via MCP.</p>
          <button className="px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Connect your first service</button>
        </div>
      ) : (
        <div className="grid grid-cols-2 gap-3">
          {connections.map(conn => (
            <div key={conn.name} className="card p-5 hover:shadow-md transition-shadow">
              <div className="flex items-center justify-between mb-3">
                <div className="flex items-center gap-3">
                  <div className="w-10 h-10 rounded-xl flex items-center justify-center text-[14px] font-bold"
                    style={{ background: conn.status === 'active' ? 'var(--green-light)' : 'var(--bg-raised)', color: conn.status === 'active' ? 'var(--green)' : 'var(--text-dim)' }}>
                    {conn.name[0]}
                  </div>
                  <div>
                    <div className="text-[14px] font-semibold">{conn.name}</div>
                    <div className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{conn.type.toUpperCase()} · {conn.tools} tools</div>
                  </div>
                </div>
                <span className="text-[10px] font-semibold px-2 py-[3px] rounded-full"
                  style={{ background: conn.status === 'active' ? 'var(--green-light)' : 'var(--bg-raised)', color: conn.status === 'active' ? 'var(--green)' : 'var(--text-dim)' }}>
                  {conn.status}
                </span>
              </div>

              <p className="text-[12px] mb-3" style={{ color: 'var(--text-dim)' }}>{conn.description}</p>

              {conn.agents_using.length > 0 && (
                <div>
                  <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>Used by</div>
                  <div className="flex flex-wrap gap-1">
                    {conn.agents_using.map(a => (
                      <span key={a} className="text-[10px] font-medium px-2 py-0.5 rounded-lg" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{a}</span>
                    ))}
                  </div>
                </div>
              )}
            </div>
          ))}
        </div>
      )}

      {/* How to connect */}
      <div className="card p-5 mt-6">
        <div className="text-[13px] font-semibold mb-2">Connect via CLI</div>
        <div className="rounded-xl p-4 mono text-[11px] leading-relaxed" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>
          {`clove connect github ghp_your_token\nclove connect slack xoxb-your-token\nclove connect postgres postgresql://user:pass@host/db`}
        </div>
      </div>
    </div>
  )
}
