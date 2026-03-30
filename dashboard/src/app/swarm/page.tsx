'use client'
import { useEffect, useState, useRef } from 'react'
import { useDemo } from '@/lib/demo-context'

interface SwarmNode {
  id: string
  name: string
  state: 'running' | 'idle' | 'stopped'
  type: 'agent' | 'world' | 'kernel'
  cost: number
  tokens: number
  steps: number
  tools: string[]
  worldId?: string
  connections: string[] // IDs of connected nodes
}

interface SwarmEdge {
  from: string
  to: string
  type: 'ipc' | 'world' | 'delegation' | 'mcp'
  active: boolean
  label?: string
}

const DEMO_NODES: SwarmNode[] = [
  { id: 'kernel', name: 'CLOVE Kernel', state: 'running', type: 'kernel', cost: 4.28, tokens: 347000, steps: 0, tools: [], connections: ['a1', 'a2', 'a3', 'a4', 'a5', 'a6'] },
  { id: 'a1', name: 'researcher-alpha', state: 'running', type: 'agent', cost: 1.24, tokens: 82000, steps: 34, tools: ['search', 'http', 'remember'], worldId: 'w1', connections: ['kernel', 'a2', 'a3'] },
  { id: 'a2', name: 'writer-bravo', state: 'running', type: 'agent', cost: 0.87, tokens: 54000, steps: 22, tools: ['read_file', 'write_file', 'search'], worldId: 'w1', connections: ['kernel', 'a1'] },
  { id: 'a3', name: 'analyst-charlie', state: 'running', type: 'agent', cost: 0.95, tokens: 61000, steps: 28, tools: ['exec', 'read_file', 'http'], worldId: 'w1', connections: ['kernel', 'a1', 'a4'] },
  { id: 'a4', name: 'reviewer-delta', state: 'idle', type: 'agent', cost: 0.32, tokens: 18000, steps: 8, tools: ['read_file', 'exec'], connections: ['kernel', 'a3'] },
  { id: 'a5', name: 'monitor-echo', state: 'running', type: 'agent', cost: 0.45, tokens: 12000, steps: 96, tools: ['http', 'store'], connections: ['kernel'] },
  { id: 'a6', name: 'coder-foxtrot', state: 'stopped', type: 'agent', cost: 0.18, tokens: 8000, steps: 6, tools: ['read_file', 'write_file', 'exec'], connections: ['kernel'] },
  { id: 'w1', name: 'research-sandbox', state: 'running', type: 'world', cost: 3.06, tokens: 197000, steps: 84, tools: [], connections: ['a1', 'a2', 'a3'] },
]

const DEMO_EDGES: SwarmEdge[] = [
  { from: 'kernel', to: 'a1', type: 'ipc', active: true },
  { from: 'kernel', to: 'a2', type: 'ipc', active: true },
  { from: 'kernel', to: 'a3', type: 'ipc', active: true },
  { from: 'kernel', to: 'a4', type: 'ipc', active: false },
  { from: 'kernel', to: 'a5', type: 'ipc', active: true },
  { from: 'kernel', to: 'a6', type: 'ipc', active: false },
  { from: 'a1', to: 'a2', type: 'ipc', active: true, label: 'research data' },
  { from: 'a1', to: 'a3', type: 'ipc', active: true, label: 'findings' },
  { from: 'a3', to: 'a4', type: 'delegation', active: false, label: 'review' },
  { from: 'w1', to: 'a1', type: 'world', active: true },
  { from: 'w1', to: 'a2', type: 'world', active: true },
  { from: 'w1', to: 'a3', type: 'world', active: true },
]

const STATE_COLORS: Record<string, string> = {
  running: 'var(--green)',
  idle: 'var(--yellow)',
  stopped: 'var(--text-dim)',
}

export default function SwarmPage() {
  const { isDemo } = useDemo()
  const [nodes, setNodes] = useState<SwarmNode[]>([])
  const [edges, setEdges] = useState<SwarmEdge[]>([])
  const [selected, setSelected] = useState<SwarmNode | null>(null)
  const [pulse, setPulse] = useState(0)
  const svgRef = useRef<SVGSVGElement>(null)

  useEffect(() => {
    if (isDemo) { setNodes(DEMO_NODES); setEdges(DEMO_EDGES); return }
    // In live mode, poll kernel for agents + worlds
    const load = async () => {
      try {
        const agents = await fetch('http://localhost:8080/api/agents').then(r => r.json())
        const worlds = await fetch('http://localhost:8080/api/worlds').then(r => r.json())
        // Build nodes from agents and worlds
        const n: SwarmNode[] = [{ id: 'kernel', name: 'CLOVE Kernel', state: 'running', type: 'kernel', cost: 0, tokens: 0, steps: 0, tools: [], connections: [] }]
        for (const a of (agents || [])) {
          n.push({ id: `a-${a.id}`, name: a.name, state: a.state || 'idle', type: 'agent', cost: 0, tokens: 0, steps: 0, tools: [], connections: ['kernel'] })
          n[0].connections.push(`a-${a.id}`)
        }
        for (const w of (worlds?.worlds || [])) {
          n.push({ id: `w-${w.id}`, name: w.name, state: w.member_count > 0 ? 'running' : 'idle', type: 'world', cost: 0, tokens: 0, steps: 0, tools: [], connections: [] })
        }
        setNodes(n)
      } catch {}
    }
    load(); const i = setInterval(load, 3000); return () => clearInterval(i)
  }, [isDemo])

  // Pulse animation
  useEffect(() => {
    const i = setInterval(() => setPulse(p => p + 1), 2000)
    return () => clearInterval(i)
  }, [])

  // Layout nodes in a force-like circular arrangement
  const W = 800, H = 520
  const cx = W / 2, cy = H / 2
  const positions = new Map<string, { x: number; y: number }>()

  // Kernel at center
  positions.set('kernel', { x: cx, y: cy })

  // Worlds in inner ring
  const worldNodes = nodes.filter(n => n.type === 'world')
  worldNodes.forEach((w, i) => {
    const angle = (i / Math.max(worldNodes.length, 1)) * Math.PI * 2 - Math.PI / 2
    positions.set(w.id, { x: cx + Math.cos(angle) * 100, y: cy + Math.sin(angle) * 100 })
  })

  // Agents in outer ring
  const agentNodes = nodes.filter(n => n.type === 'agent')
  agentNodes.forEach((a, i) => {
    const angle = (i / Math.max(agentNodes.length, 1)) * Math.PI * 2 - Math.PI / 2
    const radius = 200
    positions.set(a.id, { x: cx + Math.cos(angle) * radius, y: cy + Math.sin(angle) * radius })
  })

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[24px] font-semibold tracking-[-0.03em]">Swarm</h2>
          <p className="text-[14px] mt-1" style={{ color: 'var(--text-dim)' }}>
            {agentNodes.filter(n => n.state === 'running').length} agents active · {worldNodes.length} worlds · {edges.filter(e => e.active).length} active connections
          </p>
        </div>
        <div className="flex gap-4 text-[12px]" style={{ color: 'var(--text-dim)' }}>
          <span className="flex items-center gap-1.5"><span className="w-2 h-2 rounded-full" style={{ background: 'var(--green)' }} /> Running</span>
          <span className="flex items-center gap-1.5"><span className="w-2 h-2 rounded-full" style={{ background: 'var(--yellow)' }} /> Idle</span>
          <span className="flex items-center gap-1.5"><span className="w-2 h-2 rounded-full" style={{ background: 'var(--text-dim)' }} /> Stopped</span>
        </div>
      </div>

      <div className="grid grid-cols-3 gap-4">
        {/* Graph */}
        <div className="col-span-2 card p-0 overflow-hidden" style={{ height: H + 40 }}>
          {nodes.length === 0 ? (
            <div className="flex items-center justify-center h-full text-[13px]" style={{ color: 'var(--text-dim)' }}>No agents running</div>
          ) : (
            <svg ref={svgRef} width="100%" height="100%" viewBox={`0 0 ${W} ${H}`} className="select-none">
              <defs>
                <filter id="glow">
                  <feGaussianBlur stdDeviation="3" result="blur" />
                  <feMerge><feMergeNode in="blur" /><feMergeNode in="SourceGraphic" /></feMerge>
                </filter>
                <marker id="arrow" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
                  <path d="M 0 0 L 10 5 L 0 10 z" fill="var(--border)" />
                </marker>
              </defs>

              {/* World regions */}
              {worldNodes.map(w => {
                const pos = positions.get(w.id)!
                const members = w.connections.map(id => positions.get(id)).filter(Boolean)
                if (members.length === 0) return null
                // Calculate bounding circle
                const allPts = [pos, ...members as { x: number; y: number }[]]
                const avgX = allPts.reduce((s, p) => s + p.x, 0) / allPts.length
                const avgY = allPts.reduce((s, p) => s + p.y, 0) / allPts.length
                const maxR = Math.max(...allPts.map(p => Math.sqrt((p.x - avgX) ** 2 + (p.y - avgY) ** 2))) + 50
                return (
                  <g key={w.id}>
                    <circle cx={avgX} cy={avgY} r={maxR} fill="var(--accent)" opacity="0.04" stroke="none" />
                    <circle cx={avgX} cy={avgY} r={maxR} fill="none" stroke="var(--accent)" strokeWidth="1" strokeDasharray="4 4" opacity="0.15" />
                    <text x={avgX} y={avgY - maxR + 14} textAnchor="middle" fill="var(--accent)" fontSize="10" fontWeight="600" opacity="0.5">{w.name}</text>
                  </g>
                )
              })}

              {/* Edges */}
              {edges.map((edge, i) => {
                const from = positions.get(edge.from)
                const to = positions.get(edge.to)
                if (!from || !to) return null
                if (edge.type === 'world') return null // World membership shown via region
                const opacity = edge.active ? 0.4 : 0.1
                const stroke = edge.type === 'delegation' ? 'var(--yellow)' : edge.type === 'mcp' ? 'var(--blue)' : 'var(--border)'
                return (
                  <g key={i}>
                    <line x1={from.x} y1={from.y} x2={to.x} y2={to.y} stroke={stroke} strokeWidth={edge.active ? 1.5 : 0.5} opacity={opacity} />
                    {edge.active && edge.label && (
                      <text x={(from.x + to.x) / 2} y={(from.y + to.y) / 2 - 6} textAnchor="middle" fill="var(--text-dim)" fontSize="9" opacity="0.6">{edge.label}</text>
                    )}
                    {/* Pulse dot on active edges */}
                    {edge.active && (
                      <circle r="3" fill={stroke} opacity={0.7}>
                        <animateMotion dur="2s" repeatCount="indefinite" begin={`${(i * 0.3) % 2}s`}>
                          <mpath xlinkHref={`#path-${i}`} />
                        </animateMotion>
                      </circle>
                    )}
                    {edge.active && <path id={`path-${i}`} d={`M${from.x},${from.y} L${to.x},${to.y}`} fill="none" stroke="none" />}
                  </g>
                )
              })}

              {/* Nodes */}
              {nodes.map(node => {
                const pos = positions.get(node.id)
                if (!pos) return null
                const isKernel = node.type === 'kernel'
                const isWorld = node.type === 'world'
                const r = isKernel ? 30 : isWorld ? 0 : 22
                const color = STATE_COLORS[node.state] || 'var(--text-dim)'
                const isSelected = selected?.id === node.id

                if (isWorld) return null // Drawn as region above

                return (
                  <g key={node.id} onClick={() => setSelected(node.id === selected?.id ? null : node)} className="cursor-pointer">
                    {/* Glow for running */}
                    {node.state === 'running' && (
                      <circle cx={pos.x} cy={pos.y} r={r + 8} fill={color} opacity={0.08 + (pulse % 2 === 0 ? 0.04 : 0)}>
                        <animate attributeName="r" values={`${r + 6};${r + 12};${r + 6}`} dur="3s" repeatCount="indefinite" />
                        <animate attributeName="opacity" values="0.08;0.15;0.08" dur="3s" repeatCount="indefinite" />
                      </circle>
                    )}

                    {/* Selection ring */}
                    {isSelected && <circle cx={pos.x} cy={pos.y} r={r + 4} fill="none" stroke="var(--accent)" strokeWidth="2" />}

                    {/* Node circle */}
                    <circle cx={pos.x} cy={pos.y} r={r}
                      fill={isKernel ? 'var(--accent)' : 'var(--bg-card)'}
                      stroke={isKernel ? 'var(--accent)' : color}
                      strokeWidth={isKernel ? 0 : 2}
                      filter={node.state === 'running' ? 'url(#glow)' : undefined}
                    />

                    {/* Icon/label inside */}
                    {isKernel ? (
                      <text x={pos.x} y={pos.y + 1} textAnchor="middle" dominantBaseline="middle" fill="white" fontSize="11" fontWeight="700">C</text>
                    ) : (
                      <text x={pos.x} y={pos.y + 1} textAnchor="middle" dominantBaseline="middle" fill={color} fontSize="9" fontWeight="600">
                        {node.name.split('-')[0].slice(0, 3).toUpperCase()}
                      </text>
                    )}

                    {/* Name label */}
                    <text x={pos.x} y={pos.y + r + 14} textAnchor="middle" fill="var(--text-secondary)" fontSize="10" fontWeight="500">
                      {node.name.length > 16 ? node.name.slice(0, 14) + '...' : node.name}
                    </text>

                    {/* Status dot */}
                    <circle cx={pos.x + r - 2} cy={pos.y - r + 2} r="4" fill={color} stroke="var(--bg-card)" strokeWidth="2" />
                  </g>
                )
              })}
            </svg>
          )}
        </div>

        {/* Details panel */}
        <div className="space-y-3">
          {selected ? (
            <>
              <div className="card p-5">
                <div className="flex items-center gap-2 mb-3">
                  <div className="w-3 h-3 rounded-full" style={{ background: STATE_COLORS[selected.state] }} />
                  <h3 className="text-[15px] font-semibold">{selected.name}</h3>
                </div>
                <div className="grid grid-cols-2 gap-3 text-[12px]">
                  <div><span style={{ color: 'var(--text-dim)' }}>State</span><div className="font-semibold capitalize">{selected.state}</div></div>
                  <div><span style={{ color: 'var(--text-dim)' }}>Type</span><div className="font-semibold capitalize">{selected.type}</div></div>
                  <div><span style={{ color: 'var(--text-dim)' }}>Cost</span><div className="font-semibold tabular-nums" style={{ color: 'var(--accent)' }}>${selected.cost.toFixed(4)}</div></div>
                  <div><span style={{ color: 'var(--text-dim)' }}>Tokens</span><div className="font-semibold tabular-nums">{selected.tokens.toLocaleString()}</div></div>
                  <div><span style={{ color: 'var(--text-dim)' }}>Steps</span><div className="font-semibold tabular-nums">{selected.steps}</div></div>
                  <div><span style={{ color: 'var(--text-dim)' }}>Connections</span><div className="font-semibold">{selected.connections.length}</div></div>
                </div>
              </div>
              {selected.tools.length > 0 && (
                <div className="card p-5">
                  <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Tools</div>
                  <div className="flex flex-wrap gap-1.5">
                    {selected.tools.map(t => <span key={t} className="text-[10px] font-medium px-2 py-0.5 rounded-lg" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{t}</span>)}
                  </div>
                </div>
              )}
              {selected.connections.length > 0 && (
                <div className="card p-5">
                  <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Connected To</div>
                  <div className="space-y-1.5">
                    {selected.connections.map(id => {
                      const target = nodes.find(n => n.id === id)
                      return target ? (
                        <div key={id} className="flex items-center gap-2 text-[12px]">
                          <div className="w-2 h-2 rounded-full" style={{ background: STATE_COLORS[target.state] }} />
                          <span className="font-medium">{target.name}</span>
                        </div>
                      ) : null
                    })}
                  </div>
                </div>
              )}
            </>
          ) : (
            <>
              <div className="card p-5">
                <div className="text-[13px] font-medium mb-3">Swarm Overview</div>
                <div className="space-y-3">
                  <Stat label="Agents" value={String(agentNodes.length)} sub={`${agentNodes.filter(n => n.state === 'running').length} running`} color="var(--green)" />
                  <Stat label="Worlds" value={String(worldNodes.length)} sub={`${worldNodes.filter(n => n.state === 'running').length} active`} color="var(--accent)" />
                  <Stat label="Connections" value={String(edges.length)} sub={`${edges.filter(e => e.active).length} active`} color="var(--blue)" />
                  <Stat label="Total Cost" value={`$${nodes.reduce((s, n) => s + n.cost, 0).toFixed(2)}`} sub="all agents" color="var(--accent)" />
                </div>
              </div>
              <div className="card p-5">
                <div className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
                  Click any node to see details. Pulsing nodes are active. Dashed circles are worlds. Lines show IPC connections.
                </div>
              </div>
            </>
          )}
        </div>
      </div>
    </div>
  )
}

function Stat({ label, value, sub, color }: { label: string; value: string; sub: string; color: string }) {
  return (
    <div className="flex items-center justify-between">
      <div><div className="text-[12px]" style={{ color: 'var(--text-dim)' }}>{label}</div><div className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{sub}</div></div>
      <div className="text-[18px] font-bold tabular-nums" style={{ color }}>{value}</div>
    </div>
  )
}
