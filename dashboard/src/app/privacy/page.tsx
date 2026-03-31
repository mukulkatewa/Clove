'use client'
import { useState } from 'react'
import { useDemo } from '@/lib/demo-context'

interface PiiMatch { type: string; start: number; end: number; text?: string }
interface ScanResult { contains_pii: boolean; match_count: number; matches: PiiMatch[]; cleaned_text?: string }

const API = process.env.NEXT_PUBLIC_API_URL || ''

const EXAMPLES = [
  'My email is john@example.com and my phone is 555-123-4567.',
  'Credit card: 4111-1111-1111-1111, SSN: 123-45-6789',
  'Contact me at sarah.smith@company.io or call +1 (800) 555-0100',
  'API key: sk-or-v1-abc123def456 should not be logged',
  'Patient John Doe, DOB 03/15/1990, MRN 12345678',
]

export default function PrivacyPage() {
  const { isDemo } = useDemo()
  const [text, setText] = useState('')
  const [result, setResult] = useState<ScanResult | null>(null)
  const [scanning, setScanning] = useState(false)
  const [mode, setMode] = useState<'scan' | 'redact'>('scan')

  const handleScan = async () => {
    if (!text.trim()) return
    setScanning(true); setResult(null)

    if (isDemo) {
      // Fake scan
      const matches: PiiMatch[] = []
      const emailRe = /[\w.-]+@[\w.-]+\.\w+/g
      const phoneRe = /\+?[\d\s()-]{10,}/g
      const ssnRe = /\d{3}-\d{2}-\d{4}/g
      const ccRe = /\d{4}[-\s]?\d{4}[-\s]?\d{4}[-\s]?\d{4}/g
      const apiRe = /sk-[a-zA-Z0-9-]{20,}/g

      for (const [re, type] of [[emailRe, 'EMAIL'], [phoneRe, 'PHONE'], [ssnRe, 'SSN'], [ccRe, 'CREDIT_CARD'], [apiRe, 'API_KEY']] as [RegExp, string][]) {
        let m; while ((m = re.exec(text)) !== null) { matches.push({ type, start: m.index, end: m.index + m[0].length, text: m[0] }) }
      }

      let cleaned = text
      if (mode === 'redact') {
        for (const match of [...matches].reverse()) {
          cleaned = cleaned.slice(0, match.start) + `[${match.type}]` + cleaned.slice(match.end)
        }
      }

      setResult({ contains_pii: matches.length > 0, match_count: matches.length, matches, cleaned_text: mode === 'redact' ? cleaned : undefined })
      setScanning(false)
      return
    }

    try {
      const r = await fetch(`${API}/api/privacy/scan`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ text: text.trim() }) })
      const data = await r.json()
      setResult(data)
    } catch { setResult({ contains_pii: false, match_count: 0, matches: [] }) }
    setScanning(false)
  }

  const PII_COLORS: Record<string, { bg: string; fg: string }> = {
    EMAIL: { bg: 'var(--blue-light)', fg: 'var(--blue)' },
    PHONE: { bg: 'var(--green-light)', fg: 'var(--green)' },
    SSN: { bg: 'var(--red-light)', fg: 'var(--red)' },
    CREDIT_CARD: { bg: 'var(--red-light)', fg: 'var(--red)' },
    API_KEY: { bg: 'var(--yellow-light)', fg: 'var(--yellow)' },
  }

  return (
    <div className="max-w-[800px]">
      <h2 className="text-[22px] font-semibold tracking-[-0.03em] mb-1">Privacy Scanner</h2>
      <p className="text-[13px] mb-6" style={{ color: 'var(--text-dim)' }}>
        Scan text for PII before it reaches any LLM. The kernel applies this automatically — this tool lets you test manually.
      </p>

      {/* Mode toggle */}
      <div className="flex gap-1 mb-4">
        {(['scan', 'redact'] as const).map(m => (
          <button key={m} onClick={() => setMode(m)}
            className="px-4 py-2 rounded-lg text-[12px] font-medium capitalize"
            style={{ background: mode === m ? 'var(--accent-light)' : 'transparent', color: mode === m ? 'var(--accent)' : 'var(--text-dim)' }}>
            {m === 'scan' ? 'Detect PII' : 'Redact PII'}
          </button>
        ))}
      </div>

      {/* Input */}
      <div className="card p-0 mb-6 overflow-hidden">
        <textarea value={text} onChange={e => setText(e.target.value)}
          placeholder="Paste text to scan for personal information..."
          rows={6}
          className="w-full p-5 text-[14px] resize-none outline-none placeholder:opacity-25 leading-relaxed"
          style={{ background: 'var(--bg-card)', color: 'var(--text)', border: 'none' }} />
        <div className="flex items-center justify-between px-5 py-3" style={{ background: 'var(--bg)', borderTop: '1px solid var(--border)' }}>
          <span className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
            Detects: email, phone, SSN, credit card, API keys
          </span>
          <button onClick={handleScan} disabled={scanning || !text.trim()}
            className="px-5 py-2 rounded-lg text-[13px] font-semibold text-white disabled:opacity-30"
            style={{ background: 'var(--accent)' }}>
            {scanning ? 'Scanning...' : mode === 'scan' ? 'Scan' : 'Redact'}
          </button>
        </div>
      </div>

      {/* Examples */}
      {!text && !result && (
        <div className="mb-6">
          <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Examples</div>
          <div className="space-y-1.5">
            {EXAMPLES.map((ex, i) => (
              <button key={i} onClick={() => setText(ex)}
                className="w-full text-left px-4 py-2.5 rounded-xl text-[12px] transition-all hover:shadow-sm"
                style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
                {ex}
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Results */}
      {result && (
        <div className="card overflow-hidden">
          <div className="px-5 py-3 flex items-center justify-between"
            style={{ background: result.contains_pii ? 'var(--red-light)' : 'var(--green-light)' }}>
            <div className="flex items-center gap-2">
              <span className="w-2 h-2 rounded-full" style={{ background: result.contains_pii ? 'var(--red)' : 'var(--green)' }} />
              <span className="text-[13px] font-semibold" style={{ color: result.contains_pii ? 'var(--red)' : 'var(--green)' }}>
                {result.contains_pii ? `${result.match_count} PII found` : 'No PII detected'}
              </span>
            </div>
          </div>

          {result.matches.length > 0 && (
            <div className="px-5 py-4" style={{ borderBottom: '1px solid var(--border)' }}>
              <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Detections</div>
              <div className="space-y-1.5">
                {result.matches.map((m, i) => {
                  const pc = PII_COLORS[m.type] || { bg: 'var(--bg-raised)', fg: 'var(--text-dim)' }
                  return (
                    <div key={i} className="flex items-center gap-3">
                      <span className="text-[10px] font-semibold px-2 py-[3px] rounded-full" style={{ background: pc.bg, color: pc.fg }}>{m.type}</span>
                      {m.text && <span className="mono text-[12px]" style={{ color: 'var(--text-secondary)' }}>{m.text}</span>}
                      <span className="text-[10px] tabular-nums" style={{ color: 'var(--text-dim)' }}>pos {m.start}–{m.end}</span>
                    </div>
                  )
                })}
              </div>
            </div>
          )}

          {result.cleaned_text && (
            <div className="px-5 py-4">
              <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-2" style={{ color: 'var(--text-dim)' }}>Redacted Output</div>
              <div className="rounded-xl p-4 text-[13px] leading-relaxed mono" style={{ background: 'var(--bg)' }}>
                {result.cleaned_text}
              </div>
            </div>
          )}
        </div>
      )}

      {/* How it works */}
      <div className="card p-5 mt-6">
        <div className="text-[13px] font-semibold mb-2">Automatic Protection</div>
        <p className="text-[12px]" style={{ color: 'var(--text-dim)' }}>
          The kernel&apos;s privacy filter runs automatically on every LLM call. Before any prompt reaches OpenRouter, Anthropic, or OpenAI,
          the filter scans for PII and either redacts it (replaces with [TYPE]) or blocks the call entirely — depending on your privacy mode setting.
        </p>
        <div className="flex gap-3 mt-3">
          {['audit', 'redact', 'block'].map(m => (
            <div key={m} className="px-3 py-2 rounded-lg text-[11px] text-center" style={{ background: 'var(--bg)', border: '1px solid var(--border)' }}>
              <div className="font-semibold capitalize">{m}</div>
              <div style={{ color: 'var(--text-dim)' }}>{m === 'audit' ? 'Log only' : m === 'redact' ? 'Replace PII' : 'Reject prompt'}</div>
            </div>
          ))}
        </div>
      </div>
    </div>
  )
}
