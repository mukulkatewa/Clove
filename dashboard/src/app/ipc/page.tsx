'use client'
import { useEffect, useState } from 'react'
import { getAgents, sendMessage, getMessages } from '@/lib/api'
import { useDemo } from '@/lib/demo-context'

interface Agent { id: number; name: string; state: string }
interface Message { from: number; to: number; content: string; timestamp_ms: number }

const DEMO_AGENTS: Agent[] = [
  { id: 1, name: 'researcher-alpha', state: 'running' },
  { id: 2, name: 'writer-bravo', state: 'running' },
  { id: 3, name: 'analyst-charlie', state: 'running' },
  { id: 4, name: 'reviewer-delta', state: 'idle' },
]
const DEMO_MESSAGES: Message[] = [
  { from: 1, to: 2, content: 'Research complete. Found 5 competitors with pricing data.', timestamp_ms: Date.now() - 120000 },
  { from: 1, to: 3, content: 'Market data attached: TAM $10.9B, growing 49% CAGR.', timestamp_ms: Date.now() - 90000 },
  { from: 3, to: 2, content: 'Analysis done. Key insight: compliance gap is the biggest opportunity.', timestamp_ms: Date.now() - 60000 },
  { from: 2, to: 4, content: 'Draft report ready for review. 2400 words, 3 sections.', timestamp_ms: Date.now() - 30000 },
  { from: 4, to: 2, content: 'Review complete. Minor edits on section 2. Approved.', timestamp_ms: Date.now() - 10000 },
]

export default function IpcPage() {
  const { isDemo } = useDemo()
  const [agents, setAgents] = useState<Agent[]>([])
  const [messages, setMessages] = useState<Message[]>([])
  const [selectedAgent, setSelectedAgent] = useState<number | null>(null)
  const [newMsg, setNewMsg] = useState('')
  const [toAgent, setToAgent] = useState<number>(0)

  useEffect(() => {
    if (isDemo) { setAgents(DEMO_AGENTS); setMessages(DEMO_MESSAGES); return }
    getAgents().then(a => setAgents(a || [])).catch(() => {})
    const i = setInterval(() => { getAgents().then(a => setAgents(a || [])).catch(() => {}) }, 3000)
    return () => clearInterval(i)
  }, [isDemo])

  useEffect(() => {
    if (!selectedAgent || isDemo) return
    getMessages(selectedAgent).then(r => setMessages(r.messages || [])).catch(() => {})
    const i = setInterval(() => { getMessages(selectedAgent).then(r => setMessages(r.messages || [])).catch(() => {}) }, 2000)
    return () => clearInterval(i)
  }, [selectedAgent, isDemo])

  const handleSend = async () => {
    if (!newMsg.trim() || !selectedAgent || !toAgent) return
    if (!isDemo) await sendMessage(selectedAgent, toAgent, newMsg.trim())
    setMessages(prev => [...prev, { from: selectedAgent, to: toAgent, content: newMsg.trim(), timestamp_ms: Date.now() }])
    setNewMsg('')
  }

  const agentName = (id: number) => agents.find(a => a.id === id)?.name || `#${id}`

  const displayed = selectedAgent
    ? messages.filter(m => m.from === selectedAgent || m.to === selectedAgent)
    : messages

  return (
    <div className="flex gap-4 -mx-10 -my-8 h-[calc(100vh)]">
      {/* Agent list */}
      <div className="w-[240px] flex-shrink-0 overflow-y-auto py-6 px-4" style={{ borderRight: '1px solid var(--border)' }}>
        <div className="text-[10px] uppercase tracking-[0.06em] font-semibold mb-3 px-2" style={{ color: 'var(--text-dim)' }}>Agents</div>
        {agents.length === 0 ? (
          <div className="text-[12px] px-2" style={{ color: 'var(--text-dim)' }}>No agents running</div>
        ) : agents.map(a => (
          <button key={a.id} onClick={() => setSelectedAgent(a.id === selectedAgent ? null : a.id)}
            className="w-full flex items-center gap-2.5 px-3 py-2.5 rounded-lg text-left mb-1 transition-all"
            style={{ background: selectedAgent === a.id ? 'var(--accent-light)' : 'transparent', color: selectedAgent === a.id ? 'var(--accent)' : 'var(--text-secondary)' }}>
            <div className={`w-2 h-2 rounded-full flex-shrink-0 ${a.state === 'running' ? 'animate-pulse' : ''}`}
              style={{ background: a.state === 'running' ? 'var(--green)' : 'var(--text-dim)' }} />
            <div>
              <div className="text-[12px] font-medium">{a.name}</div>
              <div className="text-[10px]" style={{ color: 'var(--text-dim)' }}>ID: {a.id}</div>
            </div>
          </button>
        ))}
        <div className="mt-4 px-2">
          <button className="w-full px-3 py-2 rounded-lg text-[11px] font-medium" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text-dim)' }}>
            Broadcast All
          </button>
        </div>
      </div>

      {/* Message feed */}
      <div className="flex-1 flex flex-col py-6 px-6">
        <div className="mb-4">
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Agent Messages</h2>
          <p className="text-[13px]" style={{ color: 'var(--text-dim)' }}>
            {selectedAgent ? `Messages for ${agentName(selectedAgent)}` : 'All inter-agent communication'} · {displayed.length} messages
          </p>
        </div>

        {/* Messages */}
        <div className="flex-1 overflow-y-auto space-y-3 mb-4">
          {displayed.length === 0 ? (
            <div className="text-center py-16 text-[13px]" style={{ color: 'var(--text-dim)' }}>No messages yet</div>
          ) : displayed.map((msg, i) => {
            const isSent = msg.from === selectedAgent
            return (
              <div key={i} className={`flex ${isSent ? 'justify-end' : 'justify-start'}`}>
                <div className="max-w-[70%]">
                  <div className={`flex items-center gap-2 mb-1 text-[10px] ${isSent ? 'justify-end' : ''}`} style={{ color: 'var(--text-dim)' }}>
                    <span className="font-medium" style={{ color: 'var(--text-secondary)' }}>{agentName(msg.from)}</span>
                    <span>→</span>
                    <span className="font-medium" style={{ color: 'var(--text-secondary)' }}>{agentName(msg.to)}</span>
                    <span className="tabular-nums">{new Date(msg.timestamp_ms).toLocaleTimeString()}</span>
                  </div>
                  <div className="rounded-xl px-4 py-3 text-[13px] leading-relaxed"
                    style={{ background: isSent ? 'var(--accent-light)' : 'var(--bg-card)', border: `1px solid ${isSent ? 'var(--accent)' : 'var(--border)'}`, color: 'var(--text)' }}>
                    {msg.content}
                  </div>
                </div>
              </div>
            )
          })}
        </div>

        {/* Send */}
        {selectedAgent && (
          <div className="card p-3">
            <div className="flex gap-2">
              <select value={toAgent} onChange={e => setToAgent(parseInt(e.target.value))}
                className="rounded-lg px-3 py-2 text-[12px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}>
                <option value={0}>Send to...</option>
                {agents.filter(a => a.id !== selectedAgent).map(a => <option key={a.id} value={a.id}>{a.name}</option>)}
              </select>
              <input value={newMsg} onChange={e => setNewMsg(e.target.value)} placeholder="Type a message..."
                className="flex-1 rounded-lg px-3 py-2 text-[13px] outline-none placeholder:opacity-25"
                style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}
                onKeyDown={e => { if (e.key === 'Enter') handleSend() }} />
              <button onClick={handleSend} disabled={!newMsg.trim() || !toAgent}
                className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white disabled:opacity-30" style={{ background: 'var(--accent)' }}>
                Send
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  )
}
