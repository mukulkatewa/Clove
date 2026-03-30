'use client'
import { useEffect, useState } from 'react'
import { getMcpServers, getMcpTools } from '@/lib/api'
import type { McpServer, McpTool } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'
import { demoMcpServers, demoMcpTools } from '@/lib/demo-data'

export default function McpPage() {
  const { isDemo } = useDemo()
  const [servers, setServers] = useState<McpServer[]>([])
  const [tools, setTools] = useState<McpTool[]>([])
  const [selectedServer, setSelectedServer] = useState<string | null>(null)

  useEffect(() => {
    if (isDemo) { setServers(demoMcpServers() as McpServer[]); setTools(demoMcpTools() as McpTool[]); return }
    getMcpServers().then(r => setServers(r.servers || [])).catch(() => {})
    getMcpTools().then(r => setTools(r.tools || [])).catch(() => {})
    const i = setInterval(() => {
      getMcpServers().then(r => setServers(r.servers || [])).catch(() => {})
    }, 5000)
    return () => clearInterval(i)
  }, [isDemo])

  const filteredTools = selectedServer ? tools.filter(t => t.server_name === selectedServer) : tools
  const running = servers.filter(s => s.status === 'running')

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[24px] font-semibold tracking-[-0.03em]">MCP Bridge</h2>
          <p className="text-[14px] mt-1" style={{ color: 'var(--text-dim)' }}>
            Connect external tools via Model Context Protocol — {running.length} server{running.length !== 1 ? 's' : ''} active, {tools.length} tools available
          </p>
        </div>
      </div>

      {/* Servers */}
      <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Servers</div>
      {servers.length === 0 ? (
        <div className="card py-14 text-center mb-8">
          <p className="text-[14px] font-medium mb-1">No MCP servers configured</p>
          <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>Add servers in <span className="mono">~/.clove/mcp.yaml</span> or via <span className="mono">clove mcp add</span></p>
        </div>
      ) : (
        <div className="grid grid-cols-2 gap-3 mb-8">
          {servers.map(s => {
            const isRunning = s.status === 'running'
            const isSelected = selectedServer === s.name
            return (
              <button
                key={s.name}
                onClick={() => setSelectedServer(isSelected ? null : s.name)}
                className="card p-5 text-left transition-all"
                style={{ borderColor: isSelected ? 'var(--accent)' : undefined, borderWidth: isSelected ? 2 : 1 }}
              >
                <div className="flex items-center justify-between mb-3">
                  <div className="flex items-center gap-2.5">
                    <div className="w-[8px] h-[8px] rounded-full" style={{ background: isRunning ? 'var(--green)' : 'var(--text-dim)' }} />
                    <span className="text-[15px] font-semibold">{s.name}</span>
                  </div>
                  <span className="text-[10px] font-semibold px-2 py-[3px] rounded-full" style={{
                    background: isRunning ? 'var(--green-light)' : 'var(--bg-raised)',
                    color: isRunning ? 'var(--green)' : 'var(--text-dim)',
                  }}>{s.status}</span>
                </div>
                <div className="text-[12px] mono truncate" style={{ color: 'var(--text-dim)' }}>{s.command}</div>
                <div className="text-[12px] mt-2" style={{ color: 'var(--text-secondary)' }}>{s.tools_count} tools</div>
              </button>
            )
          })}
        </div>
      )}

      {/* Tools */}
      <div className="flex items-center justify-between mb-3">
        <div className="text-[11px] uppercase tracking-[0.06em] font-medium" style={{ color: 'var(--text-dim)' }}>
          {selectedServer ? `Tools — ${selectedServer}` : 'All Tools'}
        </div>
        {selectedServer && (
          <button onClick={() => setSelectedServer(null)} className="text-[12px] font-medium" style={{ color: 'var(--accent)' }}>Show all</button>
        )}
      </div>

      {filteredTools.length === 0 ? (
        <div className="card py-10 text-center text-[13px]" style={{ color: 'var(--text-dim)' }}>No tools available</div>
      ) : (
        <div className="card overflow-hidden">
          <table className="w-full text-[13px]">
            <thead>
              <tr style={{ borderBottom: '1px solid var(--border)' }}>
                {['Server', 'Tool', 'Description', 'Schema'].map(h => (
                  <th key={h} className="px-5 py-3 text-left text-[11px] font-medium uppercase tracking-[0.06em]" style={{ color: 'var(--text-dim)', background: 'var(--bg)' }}>{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {filteredTools.map((t, i) => (
                <tr key={`${t.server_name}-${t.name}`} className="hover:bg-[var(--bg)]" style={{ borderBottom: '1px solid var(--border-subtle)' }}>
                  <td className="px-5 py-3">
                    <span className="text-[11px] font-semibold px-2 py-[2px] rounded" style={{ background: 'var(--bg-raised)', color: 'var(--text-secondary)' }}>
                      {t.server_name}
                    </span>
                  </td>
                  <td className="px-5 py-3 mono text-[12px] font-medium" style={{ color: 'var(--accent)' }}>{t.name}</td>
                  <td className="px-5 py-3" style={{ color: 'var(--text-secondary)' }}>{t.description}</td>
                  <td className="px-5 py-3 mono text-[11px]" style={{ color: 'var(--text-dim)' }}>
                    {Object.keys(t.input_schema?.properties || {}).join(', ') || '—'}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {/* Config hint */}
      <div className="card p-5 mt-6">
        <div className="text-[13px] font-medium mb-2">Configuration</div>
        <p className="text-[12px] mb-3" style={{ color: 'var(--text-dim)' }}>
          MCP servers are configured in <span className="mono">~/.clove/mcp.yaml</span>. Each server runs as a subprocess using stdio transport.
        </p>
        <div className="rounded-xl p-4 mono text-[11px] leading-relaxed" style={{ background: 'var(--bg)', color: 'var(--text-secondary)' }}>
{`servers:
  - name: github
    command: npx
    args: ["@modelcontextprotocol/server-github"]
    env:
      GITHUB_TOKEN: "your-token-here"
  - name: slack
    command: npx
    args: ["@modelcontextprotocol/server-slack"]
    env:
      SLACK_BOT_TOKEN: "xoxb-..."`}
        </div>
      </div>
    </div>
  )
}
