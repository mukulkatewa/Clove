'use client'
import { useEffect, useState } from 'react'
import { useDemo } from '@/lib/demo-context'
import { getAudit, getCost } from '@/lib/api'
import type { AuditEntry, CostResponse } from '@/lib/api'
import { demoAudit, demoCost } from '@/lib/demo-data'

export default function GovernancePage() {
  const { isDemo } = useDemo()
  const [audit, setAudit] = useState<AuditEntry[]>([])
  const [cost, setCost] = useState<CostResponse | null>(null)

  useEffect(() => {
    if (isDemo) { setAudit(demoAudit() as AuditEntry[]); setCost(demoCost() as CostResponse); return }
    getAudit(500).then(setAudit).catch(() => {})
    getCost().then(setCost).catch(() => {})
  }, [isDemo])

  const total = audit.length
  const failures = audit.filter(e => !e.success).length
  const successRate = total > 0 ? ((total - failures) / total * 100) : 100
  const categories: Record<string, number> = {}
  const agentActivity: Record<string, { runs: number; failures: number }> = {}
  const denials: AuditEntry[] = []

  audit.forEach(e => {
    categories[e.category] = (categories[e.category] || 0) + 1
    const aName = e.agent_name || `#${e.agent_id}`
    if (!agentActivity[aName]) agentActivity[aName] = { runs: 0, failures: 0 }
    agentActivity[aName].runs++
    if (!e.success) { agentActivity[aName].failures++; if (e.category === 'SECURITY') denials.push(e) }
  })

  const piiScans = audit.filter(e => e.event_type.includes('PII')).length
  const budgetEvents = audit.filter(e => e.event_type === 'BUDGET_EXCEEDED').length
  const policyDenials = audit.filter(e => e.category === 'SECURITY' && !e.success).length
  const budgetUsed = cost ? (cost.total_cost_usd / (cost.max_cost_usd || 1) * 100) : 0

  // Compliance score
  const score = Math.max(0, Math.min(100, Math.round(
    (successRate * 0.3) +
    ((policyDenials === 0 ? 100 : Math.max(0, 100 - policyDenials * 10)) * 0.3) +
    ((budgetEvents === 0 ? 100 : 50) * 0.2) +
    ((budgetUsed < 80 ? 100 : budgetUsed < 100 ? 60 : 20) * 0.2)
  )))

  const scoreColor = score >= 80 ? 'var(--green)' : score >= 50 ? 'var(--yellow)' : 'var(--red)'
  const scoreLabel = score >= 90 ? 'Excellent' : score >= 80 ? 'Good' : score >= 60 ? 'Fair' : score >= 40 ? 'Needs Attention' : 'Critical'

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <div>
          <h2 className="text-[22px] font-semibold tracking-[-0.03em]">Governance</h2>
          <p className="text-[13px] mt-0.5" style={{ color: 'var(--text-dim)' }}>Compliance, security, and budget oversight across all agents</p>
        </div>
        <button className="px-4 py-2 rounded-lg text-[12px] font-medium" style={{ background: 'var(--bg-card)', border: '1px solid var(--border)', color: 'var(--text-secondary)' }}>
          Export Report
        </button>
      </div>

      {/* Compliance Score */}
      <div className="grid grid-cols-4 gap-4 mb-8">
        <div className="card p-6 col-span-1">
          <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-3" style={{ color: 'var(--text-dim)' }}>Compliance Score</div>
          <div className="relative w-[100px] h-[100px] mx-auto mb-3">
            <svg viewBox="0 0 100 100" className="w-full h-full -rotate-90">
              <circle cx="50" cy="50" r="42" fill="none" stroke="var(--border)" strokeWidth="8" />
              <circle cx="50" cy="50" r="42" fill="none" stroke={scoreColor} strokeWidth="8"
                strokeDasharray={`${score * 2.64} ${264 - score * 2.64}`} strokeLinecap="round" />
            </svg>
            <div className="absolute inset-0 flex items-center justify-center">
              <span className="text-[28px] font-bold tabular-nums" style={{ color: scoreColor }}>{score}</span>
            </div>
          </div>
          <div className="text-center text-[12px] font-semibold" style={{ color: scoreColor }}>{scoreLabel}</div>
        </div>

        <div className="col-span-3 grid grid-cols-3 gap-3">
          <ScoreCard label="Audit Coverage" value={`${total} events`} sub="All actions logged" status={total > 0 ? 'good' : 'warn'} />
          <ScoreCard label="Success Rate" value={`${successRate.toFixed(1)}%`} sub={`${failures} failures`} status={successRate >= 95 ? 'good' : successRate >= 80 ? 'warn' : 'bad'} />
          <ScoreCard label="Policy Denials" value={String(policyDenials)} sub="Permission violations" status={policyDenials === 0 ? 'good' : policyDenials < 5 ? 'warn' : 'bad'} />
          <ScoreCard label="PII Scans" value={String(piiScans)} sub="Privacy filter events" status={piiScans > 0 ? 'good' : 'neutral'} />
          <ScoreCard label="Budget Breaches" value={String(budgetEvents)} sub="Over-budget events" status={budgetEvents === 0 ? 'good' : 'bad'} />
          <ScoreCard label="Budget Usage" value={`${budgetUsed.toFixed(0)}%`} sub={`$${(cost?.total_cost_usd ?? 0).toFixed(2)} / $${(cost?.max_cost_usd ?? 0).toFixed(2)}`} status={budgetUsed < 50 ? 'good' : budgetUsed < 80 ? 'warn' : 'bad'} />
        </div>
      </div>

      {/* Agent Risk Matrix */}
      <div className="grid grid-cols-2 gap-4 mb-8">
        <div className="card p-5">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-4" style={{ color: 'var(--text-dim)' }}>Agent Risk Profile</div>
          <div className="space-y-2.5">
            {Object.entries(agentActivity).sort((a, b) => b[1].failures - a[1].failures).slice(0, 8).map(([name, data]) => {
              const failRate = data.runs > 0 ? (data.failures / data.runs * 100) : 0
              const risk = failRate > 20 ? 'high' : failRate > 5 ? 'medium' : 'low'
              const riskColor = risk === 'high' ? 'var(--red)' : risk === 'medium' ? 'var(--yellow)' : 'var(--green)'
              return (
                <div key={name} className="flex items-center gap-3">
                  <div className="w-[6px] h-[6px] rounded-full flex-shrink-0" style={{ background: riskColor }} />
                  <span className="text-[12px] font-medium flex-1 truncate">{name}</span>
                  <div className="flex items-center gap-2 text-[11px] tabular-nums">
                    <span style={{ color: 'var(--text-dim)' }}>{data.runs} runs</span>
                    {data.failures > 0 && <span className="font-medium px-1.5 py-0.5 rounded-full" style={{ background: 'var(--red-light)', color: 'var(--red)' }}>{data.failures} fail</span>}
                  </div>
                </div>
              )
            })}
          </div>
        </div>

        <div className="card p-5">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-4" style={{ color: 'var(--text-dim)' }}>Event Categories</div>
          <div className="space-y-2.5">
            {Object.entries(categories).sort((a, b) => b[1] - a[1]).map(([cat, count]) => (
              <div key={cat} className="flex items-center gap-3">
                <span className="text-[12px] flex-1">{cat}</span>
                <div className="w-[100px] h-[4px] rounded-full overflow-hidden" style={{ background: 'var(--bg-raised)' }}>
                  <div className="h-full rounded-full" style={{ width: `${(count / total) * 100}%`, background: cat === 'SECURITY' ? 'var(--red)' : cat === 'RESOURCE' ? 'var(--yellow)' : 'var(--accent)' }} />
                </div>
                <span className="text-[11px] tabular-nums w-8 text-right font-medium">{count}</span>
              </div>
            ))}
          </div>
        </div>
      </div>

      {/* Recent Security Events */}
      {denials.length > 0 && (
        <div className="card p-5 mb-8">
          <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-4" style={{ color: 'var(--red)' }}>Security Denials</div>
          <div className="space-y-2">
            {denials.slice(0, 10).map((e, i) => (
              <div key={i} className="flex items-center gap-3 text-[12px]">
                <span className="w-[6px] h-[6px] rounded-full flex-shrink-0" style={{ background: 'var(--red)' }} />
                <span className="tabular-nums" style={{ color: 'var(--text-dim)' }}>{new Date(e.timestamp).toLocaleString()}</span>
                <span className="mono font-medium">{e.event_type}</span>
                <span style={{ color: 'var(--text-dim)' }}>{e.agent_name}</span>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Compliance Checklist */}
      <div className="card p-5">
        <div className="text-[11px] uppercase tracking-[0.06em] font-medium mb-4" style={{ color: 'var(--text-dim)' }}>EU AI Act Compliance Checklist</div>
        <div className="space-y-2">
          {[
            { label: 'Audit logging enabled', check: total > 0, detail: `${total} events logged` },
            { label: 'PII filtering active', check: piiScans > 0 || isDemo, detail: `${piiScans} scans performed` },
            { label: 'Budget enforcement', check: (cost?.max_cost_usd ?? 0) > 0 || isDemo, detail: cost?.max_cost_usd ? `$${cost.max_cost_usd} cap` : 'No cap set' },
            { label: 'Agent sandboxing', check: true, detail: 'Kernel-level isolation' },
            { label: 'Permission gating', check: true, detail: 'Per-agent RBAC' },
            { label: 'Execution replay', check: true, detail: 'Record + replay available' },
            { label: 'Audit export (JSONL)', check: true, detail: '/api/audit/export endpoint' },
            { label: 'Human oversight capability', check: true, detail: 'Dashboard + manual triggers' },
          ].map((item, i) => (
            <div key={i} className="flex items-center gap-3">
              <div className="w-5 h-5 rounded-md flex items-center justify-center text-[11px]"
                style={{ background: item.check ? 'var(--green-light)' : 'var(--red-light)', color: item.check ? 'var(--green)' : 'var(--red)' }}>
                {item.check ? '✓' : '✗'}
              </div>
              <span className="text-[13px] flex-1">{item.label}</span>
              <span className="text-[11px]" style={{ color: 'var(--text-dim)' }}>{item.detail}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  )
}

function ScoreCard({ label, value, sub, status }: { label: string; value: string; sub: string; status: 'good' | 'warn' | 'bad' | 'neutral' }) {
  const colors = { good: 'var(--green)', warn: 'var(--yellow)', bad: 'var(--red)', neutral: 'var(--text-dim)' }
  const bgs = { good: 'var(--green-light)', warn: 'var(--yellow-light)', bad: 'var(--red-light)', neutral: 'var(--bg-raised)' }
  return (
    <div className="card p-4">
      <div className="text-[9px] uppercase tracking-[0.08em] font-medium mb-1.5" style={{ color: 'var(--text-dim)' }}>{label}</div>
      <div className="flex items-center gap-2">
        <span className="text-[17px] font-bold tabular-nums" style={{ color: colors[status] }}>{value}</span>
        <span className="text-[9px] font-semibold px-1.5 py-[2px] rounded-full" style={{ background: bgs[status], color: colors[status] }}>{status === 'good' ? 'OK' : status === 'warn' ? 'WARN' : status === 'bad' ? 'RISK' : '—'}</span>
      </div>
      <div className="text-[10px] mt-0.5" style={{ color: 'var(--text-dim)' }}>{sub}</div>
    </div>
  )
}
