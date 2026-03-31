'use client'
import { useState, useRef, useCallback } from 'react'
import { useDemo } from '@/lib/demo-context'
import { streamFleet } from '@/lib/api'

const API = process.env.NEXT_PUBLIC_API_URL || ''

// ── Types ────────────────────────────────────────────────────────────

interface PipelineNode {
  id: string
  name: string
  role: string
  tools: string[]
  budget: number
  x: number
  y: number
}

interface PipelineEdge {
  from: string
  to: string
  label?: string
}

interface Pipeline {
  name: string
  description: string
  nodes: PipelineNode[]
  edges: PipelineEdge[]
  category: string
  runs: number
  avg_cost: number
}

// ── Demo data ────────────────────────────────────────────────────────

const DEMO_PIPELINES: Pipeline[] = [
  {
    name: 'Code Health Check', description: '3 scanners + synthesizer review a codebase', category: 'dev', runs: 12, avg_cost: 0.43,
    nodes: [
      { id: 'n1', name: 'security-scanner', role: 'Find vulnerabilities', tools: ['read_file', 'exec'], budget: 0.3, x: 80, y: 100 },
      { id: 'n2', name: 'dep-checker', role: 'Audit dependencies', tools: ['read_file', 'exec'], budget: 0.3, x: 80, y: 220 },
      { id: 'n3', name: 'quality-reviewer', role: 'Code quality', tools: ['read_file', 'exec'], budget: 0.3, x: 80, y: 340 },
      { id: 'n4', name: 'synthesizer', role: 'Merge findings into report', tools: ['write_file'], budget: 0.2, x: 420, y: 220 },
    ],
    edges: [{ from: 'n1', to: 'n4', label: 'findings' }, { from: 'n2', to: 'n4', label: 'deps' }, { from: 'n3', to: 'n4', label: 'quality' }],
  },
  {
    name: 'Incident Response', description: 'Detect → diagnose → fix → verify', category: 'ops', runs: 3, avg_cost: 1.85,
    nodes: [
      { id: 'n1', name: 'sentinel', role: 'Detect anomaly', tools: ['http', 'store'], budget: 0.1, x: 60, y: 180 },
      { id: 'n2', name: 'diagnostician', role: 'Root cause analysis', tools: ['exec', 'read_file', 'http'], budget: 0.5, x: 220, y: 180 },
      { id: 'n3', name: 'fixer', role: 'Apply remediation', tools: ['exec', 'write_file'], budget: 0.8, x: 380, y: 180 },
      { id: 'n4', name: 'verifier', role: 'Confirm fix', tools: ['http', 'exec'], budget: 0.3, x: 540, y: 180 },
    ],
    edges: [{ from: 'n1', to: 'n2', label: 'alert' }, { from: 'n2', to: 'n3', label: 'diagnosis' }, { from: 'n3', to: 'n4', label: 'fix applied' }],
  },
]

const CAT_COLORS: Record<string, { bg: string; fg: string }> = {
  dev: { bg: 'var(--accent-light)', fg: 'var(--accent)' },
  ops: { bg: 'var(--red-light)', fg: 'var(--red)' },
  research: { bg: 'var(--blue-light)', fg: 'var(--blue)' },
}

const ALL_TOOLS = ['read_file', 'write_file', 'exec', 'http', 'search', 'store', 'remember', 'recall', 'mcp_call', 'delegate']

// ── Component ────────────────────────────────────────────────────────

export default function PipelinesPage() {
  const { isDemo } = useDemo()
  const [pipelines] = useState<Pipeline[]>(isDemo ? DEMO_PIPELINES : [])
  const [selected, setSelected] = useState<Pipeline | null>(null)
  const [creating, setCreating] = useState(false)

  // Editor state
  const [editorNodes, setEditorNodes] = useState<PipelineNode[]>([])
  const [editorEdges, setEditorEdges] = useState<PipelineEdge[]>([])
  const [editorName, setEditorName] = useState('')
  const [selectedNode, setSelectedNode] = useState<PipelineNode | null>(null)
  const [connecting, setConnecting] = useState<string | null>(null)
  const [dragNode, setDragNode] = useState<string | null>(null)
  const [missionGoal, setMissionGoal] = useState('')
  const [launching, setLaunching] = useState(false)
  const [launchResult, setLaunchResult] = useState<string | null>(null)
  const svgRef = useRef<SVGSVGElement>(null)

  const savePipeline = async () => {
    if (!editorName.trim() || editorNodes.length === 0) return
    const pipeline = { name: editorName, nodes: editorNodes, edges: editorEdges }
    try {
      await fetch(`${API}/api/store`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ key: `pipeline:${editorName}`, value: pipeline }) })
      setCreating(false)
    } catch {}
  }

  const launchPipeline = async () => {
    if (!missionGoal.trim() || editorNodes.length === 0) return
    setLaunching(true); setLaunchResult(null)
    const agentConfigs = editorNodes.map(n => ({
      name: n.name, role: n.role, tools: n.tools, budget: n.budget,
    }))
    try {
      const r = await fetch(`${API}/api/fleet`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ goal: missionGoal, world: editorName || 'pipeline-run', budget: editorNodes.reduce((s, n) => s + n.budget, 0), agent_configs: agentConfigs }),
      })
      const reader = r.body?.getReader()
      if (reader) {
        const decoder = new TextDecoder(); let buf = ''; let finalResult = ''
        while (true) {
          const { done, value } = await reader.read(); if (done) break
          buf += decoder.decode(value, { stream: true })
          const lines = buf.split('\n'); buf = lines.pop() || ''
          for (const line of lines) {
            if (!line.startsWith('data: ')) continue
            try { const ev = JSON.parse(line.slice(6)); if (ev.type === 'fleet_done') finalResult = `Done: $${ev.data?.total_cost_usd?.toFixed(4)} — ${ev.data?.total_tokens} tokens` } catch {}
          }
        }
        setLaunchResult(finalResult || 'Complete')
      }
    } catch (e) { setLaunchResult(`Error: ${e}`) }
    setLaunching(false)
  }

  const W = 640, H = 400

  const startCreate = () => {
    setCreating(true); setSelected(null); setEditorNodes([]); setEditorEdges([]); setEditorName(''); setSelectedNode(null)
  }

  const openPipeline = (p: Pipeline) => {
    setSelected(p); setCreating(true); setEditorNodes([...p.nodes]); setEditorEdges([...p.edges]); setEditorName(p.name); setSelectedNode(null)
  }

  const addNode = () => {
    const id = `n${editorNodes.length + 1}`
    const newNode: PipelineNode = { id, name: `agent-${editorNodes.length + 1}`, role: 'New agent', tools: ['read_file', 'exec'], budget: 0.3, x: 100 + (editorNodes.length % 3) * 180, y: 100 + Math.floor(editorNodes.length / 3) * 140 }
    setEditorNodes([...editorNodes, newNode])
    setSelectedNode(newNode)
  }

  const updateNode = (id: string, updates: Partial<PipelineNode>) => {
    setEditorNodes(prev => prev.map(n => n.id === id ? { ...n, ...updates } : n))
    if (selectedNode?.id === id) setSelectedNode({ ...selectedNode, ...updates })
  }

  const removeNode = (id: string) => {
    setEditorNodes(prev => prev.filter(n => n.id !== id))
    setEditorEdges(prev => prev.filter(e => e.from !== id && e.to !== id))
    if (selectedNode?.id === id) setSelectedNode(null)
  }

  const handleSvgClick = (e: React.MouseEvent<SVGSVGElement>) => {
    if (connecting) { setConnecting(null); return }
    setSelectedNode(null)
  }

  const handleNodeClick = (node: PipelineNode, e: React.MouseEvent) => {
    e.stopPropagation()
    if (connecting && connecting !== node.id) {
      // Complete connection
      if (!editorEdges.find(ed => ed.from === connecting && ed.to === node.id)) {
        setEditorEdges([...editorEdges, { from: connecting, to: node.id }])
      }
      setConnecting(null)
    } else {
      setSelectedNode(node)
    }
  }

  const handleNodeMouseDown = (nodeId: string, e: React.MouseEvent) => {
    if (e.button !== 0) return
    e.stopPropagation()
    setDragNode(nodeId)

    const handleMove = (me: MouseEvent) => {
      const svg = svgRef.current
      if (!svg) return
      const rect = svg.getBoundingClientRect()
      const x = ((me.clientX - rect.left) / rect.width) * W
      const y = ((me.clientY - rect.top) / rect.height) * H
      setEditorNodes(prev => prev.map(n => n.id === nodeId ? { ...n, x: Math.max(30, Math.min(W - 30, x)), y: Math.max(30, Math.min(H - 30, y)) } : n))
    }

    const handleUp = () => {
      setDragNode(null)
      window.removeEventListener('mousemove', handleMove)
      window.removeEventListener('mouseup', handleUp)
    }

    window.addEventListener('mousemove', handleMove)
    window.addEventListener('mouseup', handleUp)
  }

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Pipelines</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Visual multi-agent workflows. Drag, connect, deploy.</p>
        </div>
        <button onClick={startCreate} className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>
          + New Pipeline
        </button>
      </div>

      {creating ? (
        /* ── EDITOR ─────────────────────────────────────────── */
        <div className="flex gap-4">
          {/* Canvas */}
          <div className="flex-1">
            <div className="card p-0 overflow-hidden mb-3">
              {/* Toolbar */}
              <div className="flex items-center gap-2 px-4 py-2.5" style={{ background: 'var(--bg)', borderBottom: '1px solid var(--border)' }}>
                <input value={editorName} onChange={e => setEditorName(e.target.value)} placeholder="Pipeline name"
                  className="text-[14px] font-semibold outline-none bg-transparent flex-1" style={{ color: 'var(--text)' }} />
                <button onClick={addNode} className="px-3 py-1.5 rounded-lg text-[11px] font-medium" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>
                  + Add Agent
                </button>
                <button onClick={() => setConnecting(connecting ? null : selectedNode?.id || null)}
                  className="px-3 py-1.5 rounded-lg text-[11px] font-medium"
                  style={{ background: connecting ? 'var(--blue-light)' : 'var(--bg-card)', color: connecting ? 'var(--blue)' : 'var(--text-dim)', border: '1px solid var(--border)' }}>
                  {connecting ? 'Connecting...' : 'Connect'}
                </button>
              </div>

              {/* SVG Canvas */}
              <svg ref={svgRef} width="100%" height={H} viewBox={`0 0 ${W} ${H}`} onClick={handleSvgClick} className="select-none" style={{ background: 'var(--bg-card)' }}>
                {/* Grid dots */}
                {Array.from({ length: Math.floor(W / 40) * Math.floor(H / 40) }, (_, i) => {
                  const gx = ((i % Math.floor(W / 40)) + 1) * 40
                  const gy = (Math.floor(i / Math.floor(W / 40)) + 1) * 40
                  return <circle key={i} cx={gx} cy={gy} r="1" fill="var(--border)" opacity="0.5" />
                })}

                {/* Edges */}
                {editorEdges.map((edge, i) => {
                  const from = editorNodes.find(n => n.id === edge.from)
                  const to = editorNodes.find(n => n.id === edge.to)
                  if (!from || !to) return null
                  const mx = (from.x + to.x) / 2, my = (from.y + to.y) / 2
                  return (
                    <g key={i}>
                      <line x1={from.x} y1={from.y} x2={to.x} y2={to.y} stroke="var(--accent)" strokeWidth="2" opacity="0.4" />
                      {/* Arrow */}
                      <circle cx={mx + (to.x - from.x) * 0.15} cy={my + (to.y - from.y) * 0.15} r="4" fill="var(--accent)" opacity="0.6" />
                      {edge.label && <text x={mx} y={my - 8} textAnchor="middle" fill="var(--text-dim)" fontSize="9">{edge.label}</text>}
                    </g>
                  )
                })}

                {/* Nodes */}
                {editorNodes.map(node => {
                  const isSel = selectedNode?.id === node.id
                  const isConn = connecting === node.id
                  return (
                    <g key={node.id} onMouseDown={(e) => handleNodeMouseDown(node.id, e as unknown as React.MouseEvent)} onClick={(e) => handleNodeClick(node, e as unknown as React.MouseEvent)} className="cursor-grab active:cursor-grabbing">
                      {isSel && <rect x={node.x - 38} y={node.y - 32} width="76" height="64" rx="14" fill="none" stroke="var(--accent)" strokeWidth="2" strokeDasharray="4 2" />}
                      <rect x={node.x - 34} y={node.y - 28} width="68" height="56" rx="12" fill={isConn ? 'var(--blue-light)' : 'var(--bg-card)'} stroke={isConn ? 'var(--blue)' : isSel ? 'var(--accent)' : 'var(--border)'} strokeWidth={isSel ? 2 : 1} />
                      <text x={node.x} y={node.y - 6} textAnchor="middle" fill="var(--text)" fontSize="11" fontWeight="600">{node.name.length > 10 ? node.name.slice(0, 9) + '…' : node.name}</text>
                      <text x={node.x} y={node.y + 10} textAnchor="middle" fill="var(--text-dim)" fontSize="9">{node.role.length > 14 ? node.role.slice(0, 13) + '…' : node.role}</text>
                    </g>
                  )
                })}

                {editorNodes.length === 0 && (
                  <text x={W / 2} y={H / 2} textAnchor="middle" fill="var(--text-dim)" fontSize="13">Click &quot;+ Add Agent&quot; to start building</text>
                )}
              </svg>
            </div>

            {/* Mission + actions */}
            <div className="card p-4 mb-3">
              <input value={missionGoal} onChange={e => setMissionGoal(e.target.value)} placeholder="Mission goal for this pipeline..."
                className="w-full rounded-lg px-3 py-2 text-[13px] outline-none placeholder:opacity-25 mb-2"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
              {launchResult && <div className="text-[12px] mb-2 px-1" style={{ color: 'var(--green)' }}>{launchResult}</div>}
            </div>
            <div className="flex items-center justify-between">
              <button onClick={() => setCreating(false)} className="text-[12px] font-medium" style={{ color: 'var(--text-dim)' }}>Cancel</button>
              <div className="flex gap-2 items-center">
                <span className="text-[11px] mr-2" style={{ color: 'var(--text-dim)' }}>
                  {editorNodes.length} agents · ${editorNodes.reduce((s, n) => s + n.budget, 0).toFixed(2)} budget
                </span>
                <button onClick={savePipeline} disabled={!editorName.trim() || editorNodes.length === 0}
                  className="px-4 py-2 rounded-lg text-[12px] font-medium disabled:opacity-30"
                  style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
                  Save
                </button>
                <button onClick={launchPipeline} disabled={launching || !missionGoal.trim() || editorNodes.length === 0}
                  className="px-5 py-2 rounded-lg text-[12px] font-semibold text-white disabled:opacity-30"
                  style={{ background: 'var(--accent)' }}>
                  {launching ? 'Launching...' : 'Launch'}
                </button>
              </div>
            </div>
          </div>

          {/* Node config panel */}
          <div className="w-[280px] flex-shrink-0">
            {selectedNode ? (
              <div className="card p-5 space-y-4">
                <div className="flex items-center justify-between">
                  <h3 className="text-[13px] font-semibold">Configure Agent</h3>
                  <button onClick={() => removeNode(selectedNode.id)} className="text-[11px]" style={{ color: 'var(--red)' }}>Remove</button>
                </div>
                <div>
                  <Label>Name</Label>
                  <input value={selectedNode.name} onChange={e => updateNode(selectedNode.id, { name: e.target.value })}
                    className="w-full rounded-lg px-3 py-2 text-[13px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                </div>
                <div>
                  <Label>Role</Label>
                  <input value={selectedNode.role} onChange={e => updateNode(selectedNode.id, { role: e.target.value })}
                    className="w-full rounded-lg px-3 py-2 text-[13px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                </div>
                <div>
                  <Label>Budget (USD)</Label>
                  <input type="number" step="0.05" value={selectedNode.budget} onChange={e => updateNode(selectedNode.id, { budget: parseFloat(e.target.value) || 0.1 })}
                    className="w-full rounded-lg px-3 py-2 text-[13px] tabular-nums outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
                </div>
                <div>
                  <Label>Tools</Label>
                  <div className="flex flex-wrap gap-1">
                    {ALL_TOOLS.map(t => {
                      const on = selectedNode.tools.includes(t)
                      return <button key={t} onClick={() => updateNode(selectedNode.id, { tools: on ? selectedNode.tools.filter(x => x !== t) : [...selectedNode.tools, t] })}
                        className="px-2 py-0.5 rounded text-[10px] font-medium"
                        style={{ background: on ? 'var(--accent-light)' : 'var(--bg)', color: on ? 'var(--accent)' : 'var(--text-dim)', border: `1px solid ${on ? 'var(--accent)' : 'var(--border)'}` }}>
                        {t}
                      </button>
                    })}
                  </div>
                </div>
                <div>
                  <Label>Connect to another agent</Label>
                  <button onClick={() => setConnecting(selectedNode.id)}
                    className="w-full px-3 py-2 rounded-lg text-[12px] font-medium text-center"
                    style={{ background: connecting === selectedNode.id ? 'var(--blue-light)' : 'var(--bg)', border: '1px solid var(--border)', color: connecting === selectedNode.id ? 'var(--blue)' : 'var(--text-dim)' }}>
                    {connecting === selectedNode.id ? 'Click target node...' : 'Draw Connection →'}
                  </button>
                </div>
              </div>
            ) : (
              <div className="card p-5 text-center">
                <p className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
                  Select a node to configure it, or drag nodes to reposition.
                </p>
                <div className="mt-4 space-y-2 text-left text-[11px]" style={{ color: 'var(--text-dim)' }}>
                  <div>• <strong>Click node</strong> to select and edit</div>
                  <div>• <strong>Drag node</strong> to reposition</div>
                  <div>• <strong>Connect</strong> to draw data flow</div>
                  <div>• <strong>+ Add Agent</strong> to add nodes</div>
                </div>
              </div>
            )}
          </div>
        </div>
      ) : (
        /* ── PIPELINE LIST ──────────────────────────────────── */
        <>
          {pipelines.length === 0 ? (
            <div className="card py-20 text-center">
              <div className="text-[15px] font-semibold mb-1">No pipelines</div>
              <p className="text-[13px] mb-4" style={{ color: 'var(--text-dim)' }}>Create your first visual multi-agent workflow.</p>
              <button onClick={startCreate} className="px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--accent)' }}>Create Pipeline</button>
            </div>
          ) : (
            <div className="space-y-3">
              {pipelines.map(p => {
                const cc = CAT_COLORS[p.category] || CAT_COLORS.dev
                return (
                  <button key={p.name} onClick={() => openPipeline(p)} className="card w-full text-left p-5 hover:shadow-md transition-all">
                    <div className="flex items-center gap-4">
                      {/* Mini graph preview */}
                      <div className="w-[120px] h-[60px] flex-shrink-0 rounded-lg overflow-hidden" style={{ background: 'var(--bg)' }}>
                        <svg viewBox="0 0 120 60" className="w-full h-full">
                          {p.edges.map((e, i) => {
                            const f = p.nodes.find(n => n.id === e.from), t = p.nodes.find(n => n.id === e.to)
                            if (!f || !t) return null
                            const sx = f.x / 5.5, sy = f.y / 6.5, ex = t.x / 5.5, ey = t.y / 6.5
                            return <line key={i} x1={sx} y1={sy} x2={ex} y2={ey} stroke="var(--accent)" strokeWidth="1.5" opacity="0.3" />
                          })}
                          {p.nodes.map(n => (
                            <circle key={n.id} cx={n.x / 5.5} cy={n.y / 6.5} r="5" fill="var(--accent-light)" stroke="var(--accent)" strokeWidth="1.5" />
                          ))}
                        </svg>
                      </div>

                      <div className="flex-1 min-w-0">
                        <div className="flex items-center gap-2">
                          <span className="text-[14px] font-semibold">{p.name}</span>
                          <span className="text-[9px] font-semibold uppercase px-1.5 py-[2px] rounded-full" style={{ background: cc.bg, color: cc.fg }}>{p.category}</span>
                        </div>
                        <div className="text-[12px] mt-0.5" style={{ color: 'var(--text-dim)' }}>{p.description}</div>
                        <div className="flex gap-1.5 mt-2">
                          {p.nodes.map(n => <span key={n.id} className="text-[9px] font-medium px-1.5 py-0.5 rounded" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>{n.name}</span>)}
                        </div>
                      </div>

                      <div className="flex gap-4 text-[11px] tabular-nums flex-shrink-0" style={{ color: 'var(--text-dim)' }}>
                        <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--text)' }}>{p.nodes.length}</div>agents</div>
                        <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--text)' }}>{p.runs}</div>runs</div>
                        <div className="text-center"><div className="font-semibold text-[14px]" style={{ color: 'var(--accent)' }}>${p.avg_cost.toFixed(2)}</div>avg</div>
                      </div>
                    </div>
                  </button>
                )
              })}
            </div>
          )}
        </>
      )}
    </div>
  )
}

function Label({ children }: { children: React.ReactNode }) {
  return <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>{children}</div>
}
