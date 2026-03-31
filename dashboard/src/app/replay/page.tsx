'use client'
import { useEffect, useState } from 'react'
import { useDemo } from '@/lib/demo-context'

interface ReplayStatus { recording: boolean; paused: boolean; entry_count: number; started_at?: string }

const API = process.env.NEXT_PUBLIC_API_URL || ''

export default function ReplayPage() {
  const { isDemo } = useDemo()
  const [status, setStatus] = useState<ReplayStatus>({ recording: false, paused: false, entry_count: 0 })
  const [polling, setPolling] = useState(false)

  const refresh = () => {
    if (isDemo) { return }
    fetch(`${API}/api/replay`).then(r => r.json()).then(setStatus).catch(() => {})
  }

  useEffect(() => {
    if (isDemo) { setStatus({ recording: false, paused: false, entry_count: 12, started_at: new Date(Date.now() - 300000).toISOString() }); return }
    refresh()
    const i = setInterval(refresh, 2000)
    return () => clearInterval(i)
  }, [isDemo])

  const startRecording = async () => {
    if (isDemo) { setStatus({ recording: true, paused: false, entry_count: 0, started_at: new Date().toISOString() }); setPolling(true); return }
    await fetch(`${API}/api/replay/start`, { method: 'POST' }).catch(() => {})
    setPolling(true)
    refresh()
  }

  const stopRecording = async () => {
    if (isDemo) { setStatus(s => ({ ...s, recording: false })); setPolling(false); return }
    await fetch(`${API}/api/replay/stop`, { method: 'POST' }).catch(() => {})
    setPolling(false)
    refresh()
  }

  // Simulate entry count growing during recording in demo
  useEffect(() => {
    if (!isDemo || !status.recording) return
    const i = setInterval(() => { setStatus(s => ({ ...s, entry_count: s.entry_count + Math.floor(Math.random() * 3) + 1 })) }, 2000)
    return () => clearInterval(i)
  }, [isDemo, status.recording])

  const elapsed = status.started_at ? Math.floor((Date.now() - new Date(status.started_at).getTime()) / 1000) : 0
  const elapsedStr = elapsed > 3600 ? `${Math.floor(elapsed / 3600)}h ${Math.floor((elapsed % 3600) / 60)}m` : elapsed > 60 ? `${Math.floor(elapsed / 60)}m ${elapsed % 60}s` : `${elapsed}s`

  return (
    <div className="max-w-[700px]">
      <h2 className="text-[22px] font-semibold tracking-[-0.03em] mb-1">Execution Replay</h2>
      <p className="text-[13px] mb-8" style={{ color: 'var(--text-dim)' }}>
        Record all syscalls for deterministic replay. Useful for debugging, auditing, and cost analysis.
      </p>

      {/* Status card */}
      <div className="card p-6 mb-6">
        <div className="flex items-center justify-between mb-6">
          <div className="flex items-center gap-3">
            <div className={`w-4 h-4 rounded-full ${status.recording ? 'animate-pulse' : ''}`}
              style={{ background: status.recording ? 'var(--red)' : 'var(--text-dim)' }} />
            <span className="text-[16px] font-semibold">
              {status.recording ? 'Recording' : status.entry_count > 0 ? 'Stopped' : 'Idle'}
            </span>
          </div>
          {status.recording ? (
            <button onClick={stopRecording} className="px-5 py-2.5 rounded-xl text-[13px] font-semibold" style={{ background: 'var(--red-light)', color: 'var(--red)' }}>
              Stop Recording
            </button>
          ) : (
            <button onClick={startRecording} className="px-5 py-2.5 rounded-xl text-[13px] font-semibold text-white" style={{ background: 'var(--red)' }}>
              Start Recording
            </button>
          )}
        </div>

        <div className="grid grid-cols-3 gap-4">
          <div>
            <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Entries</div>
            <div className="text-[22px] font-bold tabular-nums">{status.entry_count}</div>
          </div>
          <div>
            <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>Duration</div>
            <div className="text-[22px] font-bold tabular-nums">{status.recording || status.entry_count > 0 ? elapsedStr : '—'}</div>
          </div>
          <div>
            <div className="text-[10px] uppercase tracking-[0.06em] font-medium mb-1" style={{ color: 'var(--text-dim)' }}>State</div>
            <div className="text-[22px] font-bold" style={{ color: status.recording ? 'var(--red)' : 'var(--text-dim)' }}>
              {status.recording ? 'REC' : status.entry_count > 0 ? 'DONE' : 'IDLE'}
            </div>
          </div>
        </div>

        {status.recording && (
          <div className="mt-4 h-[3px] rounded-full overflow-hidden" style={{ background: 'var(--border)' }}>
            <div className="h-full rounded-full animate-pulse" style={{ background: 'var(--red)', width: '100%' }} />
          </div>
        )}
      </div>

      {/* Info */}
      <div className="card p-5">
        <div className="text-[13px] font-semibold mb-2">How Replay Works</div>
        <div className="space-y-3 text-[12px]" style={{ color: 'var(--text-secondary)' }}>
          <div className="flex gap-3">
            <span className="w-5 h-5 rounded-md flex items-center justify-center text-[10px] font-bold flex-shrink-0" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>1</span>
            <span><strong>Record</strong> — captures every syscall (LLM calls, tool executions, state changes) with inputs and outputs.</span>
          </div>
          <div className="flex gap-3">
            <span className="w-5 h-5 rounded-md flex items-center justify-center text-[10px] font-bold flex-shrink-0" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>2</span>
            <span><strong>Stop</strong> — recording is saved to the execution log. Entries include timestamps, agent IDs, and full payloads.</span>
          </div>
          <div className="flex gap-3">
            <span className="w-5 h-5 rounded-md flex items-center justify-center text-[10px] font-bold flex-shrink-0" style={{ background: 'var(--accent-light)', color: 'var(--accent)' }}>3</span>
            <span><strong>Replay</strong> — re-execute the recorded syscalls deterministically. Useful for debugging agent behavior or auditing decisions.</span>
          </div>
        </div>
        <div className="mt-4 rounded-xl p-3 mono text-[11px]" style={{ background: 'var(--bg)', color: 'var(--text-dim)' }}>
          {`# CLI usage\ncurl -X POST localhost:8080/api/replay/start\n# ... run agents ...\ncurl -X POST localhost:8080/api/replay/stop\ncurl localhost:8080/api/replay  # check status`}
        </div>
      </div>
    </div>
  )
}
