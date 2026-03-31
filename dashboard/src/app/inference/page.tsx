'use client'
import { useEffect, useState } from 'react'
import { useDemo } from '@/lib/demo-context'

interface InferenceConfig {
  enabled: boolean
  default_model: string
  allowed_models: string[]
  allowed_providers: string[]
  max_cost_usd: number
  current_cost_usd: number
  total_requests: number
}

const DEMO_CONFIG: InferenceConfig = {
  enabled: true,
  default_model: 'anthropic/claude-sonnet-4',
  allowed_models: ['anthropic/claude-sonnet-4', 'anthropic/claude-haiku-4', 'openai/gpt-4o', 'openai/gpt-4o-mini', 'google/gemini-2.5-pro'],
  allowed_providers: ['anthropic', 'openai', 'google', 'openrouter'],
  max_cost_usd: 25.0,
  current_cost_usd: 4.28,
  total_requests: 347,
}

const ALL_MODELS = [
  'anthropic/claude-opus-4', 'anthropic/claude-sonnet-4', 'anthropic/claude-haiku-4',
  'openai/gpt-4o', 'openai/gpt-4o-mini', 'openai/o3', 'openai/codex',
  'google/gemini-2.5-pro', 'google/gemini-2.5-flash',
  'meta/llama-4-scout', 'meta/llama-4-maverick',
]

const API = process.env.NEXT_PUBLIC_API_URL || ''

export default function InferencePage() {
  const { isDemo } = useDemo()
  const [config, setConfig] = useState<InferenceConfig | null>(null)
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    if (isDemo) { setConfig(DEMO_CONFIG); return }
    fetch(`${API}/api/inference`).then(r => r.json()).then(setConfig).catch(() => {})
  }, [isDemo])

  const toggleModel = (model: string) => {
    if (!config) return
    const models = config.allowed_models.includes(model)
      ? config.allowed_models.filter(m => m !== model)
      : [...config.allowed_models, model]
    setConfig({ ...config, allowed_models: models })
  }

  const save = async () => {
    if (!config || isDemo) return
    setSaving(true)
    await fetch(`${API}/api/inference`, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(config) }).catch(() => {})
    setSaving(false)
  }

  if (!config) return <div className="text-[13px] py-20 text-center" style={{ color: 'var(--text-dim)' }}>Loading...</div>

  const costPct = config.max_cost_usd > 0 ? (config.current_cost_usd / config.max_cost_usd) * 100 : 0

  return (
    <div className="max-w-[800px]">
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Inference Gateway</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Control which models agents can use, cost caps, and provider access.</p>
        </div>
        <button onClick={save} disabled={saving || isDemo}
          className="px-4 py-2 rounded-lg text-[12px] font-semibold text-white disabled:opacity-30" style={{ background: 'var(--accent)' }}>
          {saving ? 'Saving...' : 'Save Changes'}
        </button>
      </div>

      {/* Status */}
      <div className="grid grid-cols-3 gap-3 mb-6">
        <div className="card p-4">
          <div className="text-[9px] uppercase tracking-[0.08em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Status</div>
          <div className="flex items-center gap-2">
            <span className="w-2 h-2 rounded-full" style={{ background: config.enabled ? 'var(--green)' : 'var(--red)' }} />
            <span className="text-[14px] font-semibold" style={{ color: config.enabled ? 'var(--green)' : 'var(--red)' }}>
              {config.enabled ? 'Active' : 'Disabled'}
            </span>
          </div>
        </div>
        <div className="card p-4">
          <div className="text-[9px] uppercase tracking-[0.08em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Requests</div>
          <div className="text-[17px] font-bold tabular-nums">{config.total_requests.toLocaleString()}</div>
        </div>
        <div className="card p-4">
          <div className="text-[9px] uppercase tracking-[0.08em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Cost</div>
          <div className="text-[17px] font-bold tabular-nums" style={{ color: 'var(--accent)' }}>${config.current_cost_usd.toFixed(2)}</div>
        </div>
      </div>

      {/* Cost cap */}
      <div className="card p-5 mb-6">
        <div className="flex items-center justify-between mb-3">
          <div className="text-[13px] font-semibold">System Cost Cap</div>
          <div className="flex items-center gap-2">
            <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>$</span>
            <input type="number" step="1" min="0" value={config.max_cost_usd}
              onChange={e => setConfig({ ...config, max_cost_usd: parseFloat(e.target.value) || 0 })}
              className="w-24 rounded-lg px-3 py-1.5 text-[13px] tabular-nums outline-none text-right"
              style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }} />
          </div>
        </div>
        <div className="h-[6px] rounded-full overflow-hidden mb-2" style={{ background: 'var(--bg)' }}>
          <div className="h-full rounded-full transition-all" style={{ width: `${Math.min(costPct, 100)}%`, background: costPct > 80 ? 'var(--red)' : costPct > 50 ? 'var(--yellow)' : 'var(--green)' }} />
        </div>
        <div className="flex justify-between text-[11px]" style={{ color: 'var(--text-dim)' }}>
          <span>${config.current_cost_usd.toFixed(2)} spent</span>
          <span>{costPct.toFixed(0)}% used</span>
        </div>
      </div>

      {/* Default model */}
      <div className="card p-5 mb-6">
        <div className="text-[13px] font-semibold mb-3">Default Model</div>
        <select value={config.default_model} onChange={e => setConfig({ ...config, default_model: e.target.value })}
          className="w-full rounded-lg px-3 py-2.5 text-[13px] outline-none" style={{ background: 'var(--bg)', border: '1px solid var(--border)', color: 'var(--text)' }}>
          {ALL_MODELS.map(m => <option key={m} value={m}>{m}</option>)}
        </select>
        <p className="text-[11px] mt-2" style={{ color: 'var(--text-dim)' }}>Used when an agent doesn&apos;t specify a model</p>
      </div>

      {/* Allowed models */}
      <div className="card p-5 mb-6">
        <div className="text-[13px] font-semibold mb-1">Allowed Models</div>
        <p className="text-[11px] mb-3" style={{ color: 'var(--text-dim)' }}>Agents can only use checked models. Uncheck to block.</p>
        <div className="space-y-1.5">
          {ALL_MODELS.map(model => {
            const on = config.allowed_models.includes(model)
            const provider = model.split('/')[0]
            return (
              <button key={model} onClick={() => toggleModel(model)}
                className="w-full flex items-center gap-3 px-3 py-2 rounded-lg text-left transition-colors hover:bg-[var(--bg)]">
                <div className="w-5 h-5 rounded-md flex items-center justify-center text-[11px] flex-shrink-0"
                  style={{ background: on ? 'var(--green-light)' : 'var(--bg)', border: `1px solid ${on ? 'var(--green)' : 'var(--border)'}`, color: on ? 'var(--green)' : 'var(--text-dim)' }}>
                  {on ? '✓' : ''}
                </div>
                <span className="text-[13px] flex-1">{model.split('/')[1]}</span>
                <span className="text-[10px] px-1.5 py-0.5 rounded capitalize" style={{ background: 'var(--bg-raised)', color: 'var(--text-dim)' }}>{provider}</span>
              </button>
            )
          })}
        </div>
      </div>

      {/* Providers */}
      <div className="card p-5">
        <div className="text-[13px] font-semibold mb-1">Allowed Providers</div>
        <p className="text-[11px] mb-3" style={{ color: 'var(--text-dim)' }}>Which LLM providers agents can route to</p>
        <div className="flex flex-wrap gap-2">
          {['anthropic', 'openai', 'google', 'openrouter', 'local'].map(p => {
            const on = config.allowed_providers.includes(p)
            return (
              <button key={p} onClick={() => setConfig({ ...config, allowed_providers: on ? config.allowed_providers.filter(x => x !== p) : [...config.allowed_providers, p] })}
                className="px-3 py-2 rounded-xl text-[12px] font-medium capitalize transition-all"
                style={{ background: on ? 'var(--green-light)' : 'var(--bg)', border: `1px solid ${on ? 'var(--green)' : 'var(--border)'}`, color: on ? 'var(--green)' : 'var(--text-dim)' }}>
                {p}
              </button>
            )
          })}
        </div>
      </div>
    </div>
  )
}
