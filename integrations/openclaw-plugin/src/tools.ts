/**
 * CLOVE tools for OpenClaw — registered as native LLM-callable tools.
 * Bypasses the MCP bridge. Calls CLOVE kernel REST API directly.
 */

import type { OpenClawPluginApi } from './index.js'
import { getPluginConfig } from './index.js'

// Minimal TypeBox-compatible schema objects (OpenClaw uses TypeBox internally)
const TString = (desc: string) => ({ type: 'string' as const, description: desc })
const TOptString = (desc: string) => ({ type: 'string' as const, description: desc })
const TNumber = (desc: string) => ({ type: 'number' as const, description: desc })

export function registerCloveTools(api: OpenClawPluginApi): void {
  const cfg = getPluginConfig(api)
  const apiBase = `http://localhost:${cfg.apiPort}`

  async function cloveApi<T>(path: string, method = 'GET', body?: unknown): Promise<T> {
    const opts: RequestInit = {
      method,
      headers: { 'Content-Type': 'application/json' },
    }
    if (body) opts.body = JSON.stringify(body)
    const res = await fetch(`${apiBase}${path}`, opts)
    return res.json() as Promise<T>
  }

  // Use the same registerTool pattern as openclaw-mcp-bridge
  const reg = api as unknown as {
    registerTool: (tool: {
      name: string
      label?: string
      description: string
      parameters: unknown
      execute: (toolCallId: string, params: Record<string, unknown>) => Promise<{
        content: Array<{ type: string; text: string }>
        details?: Record<string, unknown>
      }>
    }) => void
  }

  // ── recall: read all shared memory ──
  reg.registerTool({
    name: 'clove_recall',
    label: 'CLOVE: Recall shared memory',
    description: 'Recall all facts from CLOVE kernel shared memory. Other agents (researchers, writers, etc.) store findings here. Use this to access shared knowledge.',
    parameters: {
      type: 'object',
      properties: {},
      required: [],
    },
    async execute() {
      api.logger.info('[clove_recall] execute() called!')
      try {
        const result = await cloveApi<{ blocks: Array<{ name: string; type: string; content: string; access: string }>; count: number }>('/api/memory')
        api.logger.info(`[clove_recall] got ${result.count} blocks`)
        if (result.count === 0) {
          return { content: [{ type: 'text', text: 'No memory blocks in CLOVE kernel.' }] }
        }
        const text = result.blocks
          .map((b: { type: string; access: string; name: string; content: string }) => `[${b.type}/${b.access}] ${b.name}:\n${b.content}`)
          .join('\n\n---\n\n')
        return { content: [{ type: 'text', text: `Found ${result.count} memory block(s):\n\n${text}` }] }
      } catch (e) {
        api.logger.error(`[clove_recall] error: ${e}`)
        return { content: [{ type: 'text', text: `Error: ${e}` }] }
      }
    },
  })

  // ── remember: store fact in shared memory ──
  reg.registerTool({
    name: 'clove_remember',
    label: 'CLOVE: Store in shared memory',
    description: 'Store a fact or finding in CLOVE kernel shared memory. Other agents can recall this later.',
    parameters: {
      type: 'object',
      properties: {
        content: TString('The fact or finding to store'),
        name: TOptString('Short name for this memory block'),
      },
      required: ['content'],
    },
    async execute(_id, params) {
      try {
        const result = await cloveApi<{ id: string; name: string }>('/api/memory', 'POST', {
          name: (params.name as string) || 'shared-' + Date.now(),
          content: params.content || '',
          type: 'core',
          access: 'shared_readwrite',
        })
        return { content: [{ type: 'text', text: `Stored in CLOVE memory: "${result.name}" (${result.id})` }] }
      } catch (e) {
        return { content: [{ type: 'text', text: `Error: ${e}` }] }
      }
    },
  })

  // ── clove_run: delegate a task to RunEngine ──
  reg.registerTool({
    name: 'clove_run',
    label: 'CLOVE: Run agent task',
    description: 'Delegate a task to a CLOVE RunEngine agent. The agent plans, uses tools (search, file I/O, exec), and returns a result.',
    parameters: {
      type: 'object',
      properties: {
        goal: TString('What the agent should accomplish'),
        budget: TNumber('Max budget in USD (default 0.20)'),
      },
      required: ['goal'],
    },
    async execute(_id, params) {
      try {
        const result = await cloveApi<{ content: string; success: boolean; total_cost_usd: number; steps: number }>(
          '/api/run', 'POST', {
            goal: params.goal,
            budget: (params.budget as number) || 0.20,
          },
        )
        return {
          content: [{
            type: 'text',
            text: `[${result.success ? 'SUCCESS' : 'FAILED'} | ${result.steps} steps | $${result.total_cost_usd.toFixed(6)}]\n\n${result.content}`,
          }],
        }
      } catch (e) {
        return { content: [{ type: 'text', text: `Error: ${e}` }] }
      }
    },
  })

  // ── clove_cost: check cost ──
  reg.registerTool({
    name: 'clove_cost',
    label: 'CLOVE: Check cost',
    description: 'Check total LLM cost tracked by the CLOVE kernel.',
    parameters: { type: 'object', properties: {}, required: [] },
    async execute() {
      try {
        const result = await cloveApi<{ total_cost_usd: number; max_cost_usd: number }>('/api/cost')
        return {
          content: [{
            type: 'text',
            text: `Total cost: $${result.total_cost_usd.toFixed(6)}` +
              (result.max_cost_usd > 0 ? ` / $${result.max_cost_usd.toFixed(2)} budget` : ''),
          }],
        }
      } catch (e) {
        return { content: [{ type: 'text', text: `Error: ${e}` }] }
      }
    },
  })

  api.logger.info('CLOVE tools registered: clove_recall, clove_remember, clove_run, clove_cost')

  // Inject shared memory into every agent prompt via hook
  // This bypasses tool execution issues — memory is always in context
  api.on('before_prompt_build', async () => {
    try {
      const result = await cloveApi<{ blocks: Array<{ name: string; type: string; content: string }>; count: number }>('/api/memory')
      if (result.count > 0) {
        const memoryText = result.blocks
          .map((b: { type: string; name: string; content: string }) => `[${b.type}] ${b.name}: ${b.content}`)
          .join('\n\n')
        return {
          appendSystemContext: `\n\n## CLOVE Shared Memory (from other agents)\n\n${memoryText}\n\nYou can reference this shared knowledge in your responses. To store new knowledge, use the clove_remember tool.`,
        }
      }
    } catch (e) {
      api.logger.warn(`[clove] failed to inject memory: ${e}`)
    }
    return {}
  })
}
