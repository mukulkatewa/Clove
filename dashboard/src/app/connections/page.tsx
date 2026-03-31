'use client'
import { useEffect, useState } from 'react'
import { getMcpServers, getMcpTools } from '@/lib/api'
import type { McpServer, McpTool } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoMcpServers, demoMcpTools } from '@/lib/demo-data'

interface Connection { name: string; type: string; status: string; tools: number; toolList: McpTool[]; description: string }

const SERVICE_INFO: Record<string, string> = {
  github: 'Repository access, PR management, issue tracking',
  slack: 'Messaging, channel management, notifications',
  filesystem: 'Local file read/write access',
  postgres: 'Database queries, table management',
  notion: 'Documentation, wikis, databases',
}

export default function ConnectionsPage() {
  const { isDemo } = useDemo()
  const [connections, setConnections] = useState<Connection[]>([])
  const [expanded, setExpanded] = useState<string | null>(null)

  useEffect(() => {
    if (isDemo) {
      const servers = demoMcpServers() as McpServer[]
      const tools = demoMcpTools() as McpTool[]
      setConnections(servers.map(s => ({
        name: s.name, type: 'mcp', status: s.status,
        tools: s.tools_count,
        toolList: tools.filter(t => t.server_name === s.name),
        description: SERVICE_INFO[s.name] || s.command,
      })))
      return
    }
    const load = async () => {
      try {
        const [servers, tools] = await Promise.all([getMcpServers(), getMcpTools()])
        setConnections((servers.servers || []).map(s => ({
          name: s.name, type: 'mcp', status: s.status,
          tools: s.tools_count,
          toolList: (tools.tools || []).filter(t => t.server_name === s.name),
          description: SERVICE_INFO[s.name] || s.command,
        })))
      } catch {}
    }
    load()
    const i = setInterval(load, 5000)
    return () => clearInterval(i)
  }, [isDemo])

  const active = connections.filter(c => c.status === 'running' || c.status === 'active')

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Connections</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>
            {active.length} active MCP connections · {connections.reduce((s, c) => s + c.tools, 0)} tools available
          </p>
        </div>
      </div>

      {connections.length === 0 ? (
        <div className="card py-16 text-center">
          <div className="text-[15px] font-semibold mb-1">No connections</div>
          <p className="text-[13px] mb-4" style={{ color: 'var(--text-dim)' }}>Connect services via MCP. Configure in <span className="mono">~/.clove/mcp.yaml</span></p>
          <div className="inline-block rounded-xl p-4 mono text-[11px] text-left" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>
            clove connect github ghp_your_token
          </div>
        </div>
      ) : (
        <div className="space-y-3">
          {connections.map(conn => {
            const isActive = conn.status === 'running' || conn.status === 'active'
            const isOpen = expanded === conn.name
            return (
              <div key={conn.name} className="card overflow-hidden">
                <button onClick={() => setExpanded(isOpen ? null : conn.name)} className="w-full text-left p-5 hover:bg-[var(--bg)] transition-colors">
                  <div className="flex items-center gap-4">
                    <div className="w-10 h-10 rounded-xl flex items-center justify-center text-[14px] font-bold"
                      style={{ background: isActive ? 'var(--green-light)' : 'var(--bg-raised)', color: isActive ? 'var(--green)' : 'var(--text-dim)' }}>
                      {conn.name[0].toUpperCase()}
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-[14px] font-semibold capitalize">{conn.name}</span>
                        <span className="text-[9px] font-semibold uppercase px-1.5 py-[2px] rounded-full"
                          style={{ background: isActive ? 'var(--green-light)' : 'var(--bg-raised)', color: isActive ? 'var(--green)' : 'var(--text-dim)' }}>
                          {conn.status}
                        </span>
                        <span className="text-[9px] uppercase px-1.5 py-[2px] rounded-full" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>{conn.type}</span>
                      </div>
                      <div className="text-[12px] mt-0.5" style={{ color: 'var(--text-dim)' }}>{conn.description}</div>
                    </div>
                    <div className="text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{conn.tools} tools</div>
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ color: 'var(--text-dim)', transform: isOpen ? 'rotate(180deg)' : '', transition: '150ms' }}><polyline points="6 9 12 15 18 9"/></svg>
                  </div>
                </button>

                {isOpen && conn.toolList.length > 0 && (
                  <div className="px-5 pb-5" style={{ borderTop: '1px solid var(--border)' }}>
                    <div className="text-[10px] uppercase tracking-[0.06em] font-medium mt-3 mb-2" style={{ color: 'var(--text-dim)' }}>Available Tools</div>
                    <div className="space-y-1.5">
                      {conn.toolList.map((t, i) => (
                        <div key={i} className="flex items-center gap-3 px-3 py-2 rounded-lg" style={{ background: 'var(--bg)' }}>
                          <span className="mono text-[12px] font-medium" style={{ color: 'var(--accent)' }}>{t.name}</span>
                          <span className="text-[11px] flex-1" style={{ color: 'var(--text-dim)' }}>{t.description}</span>
                          {(() => { const props = (t.input_schema as Record<string, unknown>)?.properties; return props ? <span className="text-[10px] mono" style={{ color: 'var(--text-dim)' }}>{Object.keys(props as Record<string, unknown>).join(', ')}</span> : null })()}
                        </div>
                      ))}
                    </div>
                  </div>
                )}
              </div>
            )
          })}
        </div>
      )}

      <div className="card p-5 mt-6">
        <div className="text-[13px] font-semibold mb-2">Add Connections</div>
        <div className="rounded-xl p-4 mono text-[11px] leading-relaxed" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>
{`# Connect via CLI
clove connect github ghp_your_token
clove connect slack xoxb-your-token
clove connect postgres postgresql://user:pass@host/db

# Or edit ~/.clove/mcp.yaml directly
# Then restart kernel: clove stop && clove start`}
        </div>
      </div>
    </div>
  )
}
