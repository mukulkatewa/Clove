'use client'
import { useState } from 'react'
import { useDemo } from '@/lib/demo-context'

interface Provider {
  id: string
  name: string
  type: 'llm' | 'agent-runtime'
  status: 'connected' | 'available' | 'coming-soon'
  description: string
  models?: Model[]
  capabilities?: string[]
  apiKeySet: boolean
  costPer1kTokens?: { input: number; output: number }
}

interface Model {
  id: string
  name: string
  provider: string
  context: string
  speed: 'fast' | 'medium' | 'slow'
  quality: 'high' | 'medium' | 'low'
  costInput: number
  costOutput: number
  bestFor: string[]
}

const PROVIDERS: Provider[] = [
  {
    id: 'anthropic', name: 'Anthropic', type: 'agent-runtime', status: 'connected',
    description: 'Claude models + Claude Code as an agent runtime. Best for code analysis, reasoning, and complex tasks.',
    apiKeySet: true,
    capabilities: ['Code analysis', 'Multi-file editing', 'Shell access', 'Reasoning'],
    models: [
      { id: 'claude-opus-4', name: 'Claude Opus 4', provider: 'anthropic', context: '200K', speed: 'slow', quality: 'high', costInput: 15, costOutput: 75, bestFor: ['complex reasoning', 'architecture'] },
      { id: 'claude-sonnet-4', name: 'Claude Sonnet 4', provider: 'anthropic', context: '200K', speed: 'medium', quality: 'high', costInput: 3, costOutput: 15, bestFor: ['code review', 'analysis', 'general'] },
      { id: 'claude-haiku-4', name: 'Claude Haiku 4', provider: 'anthropic', context: '200K', speed: 'fast', quality: 'medium', costInput: 0.25, costOutput: 1.25, bestFor: ['summarization', 'classification', 'simple tasks'] },
    ],
  },
  {
    id: 'openai', name: 'OpenAI', type: 'agent-runtime', status: 'connected',
    description: 'GPT models + Codex as an agent runtime. Strong at code generation, function calling, and structured output.',
    apiKeySet: true,
    capabilities: ['Code generation', 'Function calling', 'Structured output', 'Web browsing'],
    models: [
      { id: 'gpt-4o', name: 'GPT-4o', provider: 'openai', context: '128K', speed: 'medium', quality: 'high', costInput: 2.5, costOutput: 10, bestFor: ['general', 'function calling', 'multimodal'] },
      { id: 'o3', name: 'o3', provider: 'openai', context: '200K', speed: 'slow', quality: 'high', costInput: 10, costOutput: 40, bestFor: ['complex reasoning', 'math', 'science'] },
      { id: 'gpt-4o-mini', name: 'GPT-4o Mini', provider: 'openai', context: '128K', speed: 'fast', quality: 'medium', costInput: 0.15, costOutput: 0.6, bestFor: ['simple tasks', 'high volume', 'classification'] },
      { id: 'codex', name: 'Codex', provider: 'openai', context: '200K', speed: 'medium', quality: 'high', costInput: 3, costOutput: 15, bestFor: ['code generation', 'refactoring', 'debugging'] },
    ],
  },
  {
    id: 'openclaw', name: 'OpenClaw', type: 'agent-runtime', status: 'connected',
    description: 'Conversational AI agent with channel integrations. Best for customer-facing tasks, Slack/Discord bots.',
    apiKeySet: true,
    capabilities: ['Slack integration', 'Discord integration', 'Telegram', 'Persistent conversations'],
    models: [
      { id: 'openclaw-default', name: 'OpenClaw Agent', provider: 'openclaw', context: '100K', speed: 'medium', quality: 'high', costInput: 2, costOutput: 8, bestFor: ['customer support', 'chat bots', 'channel management'] },
    ],
  },
  {
    id: 'google', name: 'Google', type: 'llm', status: 'available',
    description: 'Gemini models. Strong multimodal capabilities, large context windows.',
    apiKeySet: false,
    models: [
      { id: 'gemini-2.5-pro', name: 'Gemini 2.5 Pro', provider: 'google', context: '1M', speed: 'medium', quality: 'high', costInput: 1.25, costOutput: 5, bestFor: ['long context', 'multimodal', 'research'] },
      { id: 'gemini-2.5-flash', name: 'Gemini 2.5 Flash', provider: 'google', context: '1M', speed: 'fast', quality: 'medium', costInput: 0.15, costOutput: 0.6, bestFor: ['fast tasks', 'high volume'] },
    ],
  },
  {
    id: 'openrouter', name: 'OpenRouter', type: 'llm', status: 'connected',
    description: 'Unified gateway to 300+ models. Fallback provider for any model not directly connected.',
    apiKeySet: true,
    models: [
      { id: 'or-any', name: '300+ models', provider: 'openrouter', context: 'varies', speed: 'medium', quality: 'high', costInput: 0, costOutput: 0, bestFor: ['any model', 'fallback', 'experimentation'] },
    ],
  },
  {
    id: 'local', name: 'Local / Ollama', type: 'llm', status: 'available',
    description: 'Self-hosted models via Ollama. Zero cost, full privacy, runs on your hardware.',
    apiKeySet: false,
    capabilities: ['Zero cost', 'Full privacy', 'No rate limits', 'Offline'],
    models: [
      { id: 'llama-4-scout', name: 'Llama 4 Scout', provider: 'local', context: '128K', speed: 'medium', quality: 'medium', costInput: 0, costOutput: 0, bestFor: ['private data', 'high volume', 'offline'] },
    ],
  },
]

const STATUS_STYLE: Record<string, { bg: string; fg: string; label: string }> = {
  connected: { bg: 'var(--green-light)', fg: 'var(--green)', label: 'Connected' },
  available: { bg: 'var(--bg-raised)', fg: 'var(--text-dim)', label: 'Available' },
  'coming-soon': { bg: 'var(--yellow-light)', fg: 'var(--yellow)', label: 'Coming Soon' },
}

const SPEED_COLORS: Record<string, string> = { fast: 'var(--green)', medium: 'var(--yellow)', slow: 'var(--red)' }
const QUALITY_COLORS: Record<string, string> = { high: 'var(--green)', medium: 'var(--yellow)', low: 'var(--red)' }

export default function ProvidersPage() {
  const { isDemo } = useDemo()
  const [providers] = useState<Provider[]>(PROVIDERS)
  const [expanded, setExpanded] = useState<string | null>(null)
  const [view, setView] = useState<'providers' | 'models'>('providers')

  const connected = providers.filter(p => p.status === 'connected')
  const allModels = providers.flatMap(p => p.models || [])
  const runtimes = providers.filter(p => p.type === 'agent-runtime')

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Providers & Models</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>
            {connected.length} connected · {allModels.length} models available · {runtimes.length} agent runtimes
          </p>
        </div>
        <div className="flex rounded-lg overflow-hidden" style={{ border: '1px solid var(--border)' }}>
          <button onClick={() => setView('providers')} className="px-3 py-1.5 text-[11px] font-medium" style={{ background: view === 'providers' ? 'var(--accent-light)' : 'var(--bg-card)', color: view === 'providers' ? 'var(--accent)' : 'var(--text-dim)' }}>Providers</button>
          <button onClick={() => setView('models')} className="px-3 py-1.5 text-[11px] font-medium" style={{ background: view === 'models' ? 'var(--accent-light)' : 'var(--bg-card)', color: view === 'models' ? 'var(--accent)' : 'var(--text-dim)' }}>All Models</button>
        </div>
      </div>

      {/* Agent runtime callout */}
      <div className="card p-4 mb-6" style={{ borderLeft: '4px solid var(--accent)' }}>
        <div className="text-[13px] font-semibold mb-1">Agent Runtimes</div>
        <p className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
          CLOVE can orchestrate different AI runtimes in the same fleet. Use Claude Code for code tasks, Codex for generation, OpenClaw for chat — all governed by the same kernel.
        </p>
        <div className="flex gap-2 mt-3">
          {runtimes.map(r => (
            <div key={r.id} className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-[11px] font-medium" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>
              <span className="w-2 h-2 rounded-full" style={{ background: 'var(--green)' }} />
              {r.name}
            </div>
          ))}
        </div>
      </div>

      {view === 'providers' ? (
        /* ── PROVIDERS VIEW ──────────────────────────────── */
        <div className="space-y-3">
          {providers.map(provider => {
            const ss = STATUS_STYLE[provider.status]
            const isOpen = expanded === provider.id
            return (
              <div key={provider.id} className="card overflow-hidden">
                <button onClick={() => setExpanded(isOpen ? null : provider.id)} className="w-full text-left p-5 hover:bg-[var(--bg)] transition-colors">
                  <div className="flex items-center gap-4">
                    <div className="w-11 h-11 rounded-xl flex items-center justify-center text-[16px] font-bold flex-shrink-0"
                      style={{ background: provider.status === 'connected' ? 'var(--accent-light)' : 'var(--bg-raised)', color: provider.status === 'connected' ? 'var(--accent)' : 'var(--text-dim)' }}>
                      {provider.name[0]}
                    </div>
                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="text-[14px] font-semibold">{provider.name}</span>
                        <span className="text-[9px] font-semibold uppercase px-1.5 py-[2px] rounded-full" style={{ background: ss.bg, color: ss.fg }}>{ss.label}</span>
                        {provider.type === 'agent-runtime' && (
                          <span className="text-[9px] font-semibold uppercase px-1.5 py-[2px] rounded-full" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>Runtime</span>
                        )}
                      </div>
                      <div className="text-[12px] mt-0.5 truncate" style={{ color: 'var(--text-dim)' }}>{provider.description}</div>
                    </div>
                    <div className="text-[11px] tabular-nums flex-shrink-0" style={{ color: 'var(--text-dim)' }}>
                      {(provider.models || []).length} models
                    </div>
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ color: 'var(--text-dim)', transform: isOpen ? 'rotate(180deg)' : '', transition: '150ms' }}><polyline points="6 9 12 15 18 9"/></svg>
                  </div>
                </button>

                {isOpen && (
                  <div className="px-5 pb-5 space-y-4" style={{ borderTop: '1px solid var(--border)' }}>
                    {/* Capabilities */}
                    {provider.capabilities && (
                      <div className="pt-3">
                        <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Capabilities</div>
                        <div className="flex flex-wrap gap-1.5">
                          {provider.capabilities.map(c => <span key={c} className="text-[10px] font-medium px-2 py-1 rounded-lg" style={{ background: 'var(--bg)', color: 'var(--text-secondary)' }}>{c}</span>)}
                        </div>
                      </div>
                    )}

                    {/* Models */}
                    {provider.models && provider.models.length > 0 && (
                      <div>
                        <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Models</div>
                        <div className="space-y-2">
                          {provider.models.map(m => (
                            <div key={m.id} className="rounded-xl p-3.5" style={{ background: 'var(--bg)', border: '1px solid var(--border)' }}>
                              <div className="flex items-center justify-between mb-2">
                                <span className="text-[13px] font-semibold">{m.name}</span>
                                <div className="flex items-center gap-2 text-[10px]">
                                  <span className="px-1.5 py-0.5 rounded" style={{ background: 'var(--bg-card)', color: 'var(--text-dim)' }}>{m.context}</span>
                                  <span className="font-medium" style={{ color: SPEED_COLORS[m.speed] }}>●</span>
                                  <span style={{ color: 'var(--text-dim)' }}>{m.speed}</span>
                                </div>
                              </div>
                              <div className="flex items-center justify-between">
                                <div className="flex gap-1.5">
                                  {m.bestFor.map(b => <span key={b} className="text-[9px] px-1.5 py-0.5 rounded" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{b}</span>)}
                                </div>
                                {m.costInput > 0 && (
                                  <span className="text-[10px] tabular-nums" style={{ color: 'var(--text-dim)' }}>
                                    ${m.costInput}/1M in · ${m.costOutput}/1M out
                                  </span>
                                )}
                              </div>
                            </div>
                          ))}
                        </div>
                      </div>
                    )}

                    {/* Connect button */}
                    {provider.status === 'available' && (
                      <button className="px-4 py-2.5 rounded-lg text-[12px] font-semibold text-white" style={{ background: 'var(--accent)' }}>
                        Connect {provider.name}
                      </button>
                    )}
                  </div>
                )}
              </div>
            )
          })}
        </div>
      ) : (
        /* ── MODELS VIEW ─────────────────────────────────── */
        <div className="card overflow-hidden">
          <table className="w-full text-[13px]">
            <thead>
              <tr style={{ borderBottom: '1px solid var(--border)' }}>
                {['Model', 'Provider', 'Context', 'Speed', 'Quality', 'Cost (1M tokens)', 'Best For'].map(h => (
                  <th key={h} className="px-4 py-3 text-left text-[10px] font-medium uppercase tracking-[0.06em]" style={{ color: 'var(--text-dim)', background: 'var(--bg)' }}>{h}</th>
                ))}
              </tr>
            </thead>
            <tbody>
              {allModels.filter(m => m.id !== 'or-any').map(m => (
                <tr key={m.id} className="hover:bg-[var(--bg)]" style={{ borderBottom: '1px solid var(--border-subtle)' }}>
                  <td className="px-4 py-3 font-semibold">{m.name}</td>
                  <td className="px-4 py-3"><span className="text-[10px] font-medium px-1.5 py-0.5 rounded capitalize" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>{m.provider}</span></td>
                  <td className="px-4 py-3 text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{m.context}</td>
                  <td className="px-4 py-3"><span className="text-[11px] font-medium capitalize" style={{ color: SPEED_COLORS[m.speed] }}>{m.speed}</span></td>
                  <td className="px-4 py-3"><span className="text-[11px] font-medium capitalize" style={{ color: QUALITY_COLORS[m.quality] }}>{m.quality}</span></td>
                  <td className="px-4 py-3 text-[12px] tabular-nums" style={{ color: 'var(--text-dim)' }}>{m.costInput > 0 ? `$${m.costInput} / $${m.costOutput}` : 'Free'}</td>
                  <td className="px-4 py-3">
                    <div className="flex gap-1">{m.bestFor.slice(0, 2).map(b => <span key={b} className="text-[9px] px-1.5 py-0.5 rounded" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>{b}</span>)}</div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {/* Setup guide */}
      <div className="card p-5 mt-6">
        <div className="text-[13px] font-semibold mb-2">Configure via CLI</div>
        <div className="rounded-xl p-4 mono text-[11px] leading-relaxed" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>
{`# Connect providers
clove config set openrouterKey sk-or-v1-...
clove config set anthropicKey sk-ant-...
clove config set openaiKey sk-...

# Use specific models in agents
clove agent create code-reviewer --model claude-sonnet-4
clove agent create researcher --model gpt-4o
clove agent create support-bot --runtime openclaw`}
        </div>
      </div>
    </div>
  )
}
