#!/usr/bin/env node
/**
 * CLOVE MCP Server
 *
 * Exposes the CLOVE kernel's memory blocks, artifacts, traces, and governance
 * as MCP tools and resources. Any MCP client (OpenClaw, Claude Code, Cursor)
 * can connect and share context through the kernel.
 *
 * Memory system (research-backed):
 *   SYSTEM blocks — pinned at top of context (Lost in the Middle, Liu 2023)
 *   CORE blocks   — always included in assembly
 *   RECALL blocks  — included when relevant
 *
 * Artifact system (typed execution outputs):
 *   QUERY → RESEARCH → ANALYSIS → SYNTHESIS → REPORT → NOTE → PLAN
 *   States: DRAFT → IN_REVIEW → APPROVED → FINAL → ARCHIVED
 *
 * Access control:
 *   PRIVATE — only owner
 *   SHARED_READ — others read, owner writes
 *   SHARED_READWRITE — multi-agent collaboration
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js'
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js'
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  ListResourcesRequestSchema,
  ReadResourceRequestSchema,
} from '@modelcontextprotocol/sdk/types.js'

// Hardcode kernel URL — the MCP bridge spawns us as a subprocess
// and may not pass environment variables correctly
const KERNEL_URL = process.env.CLOVE_API_URL || 'http://127.0.0.1:8080'

// ── Kernel API helpers ──────────────────────────────────────────────────

async function api<T>(path: string, method = 'GET', body?: unknown): Promise<T> {
  const url = `${KERNEL_URL}${path}`
  const opts: RequestInit = {
    method,
    headers: { 'Content-Type': 'application/json' },
  }
  if (body) opts.body = JSON.stringify(body)
  try {
    const res = await fetch(url, opts)
    const data = await res.json() as T
    console.error(`[clove-mcp] ${method} ${path} → ${JSON.stringify(data).slice(0, 100)}`)
    return data
  } catch (e) {
    console.error(`[clove-mcp] ${method} ${path} FAILED: ${e}`)
    throw e
  }
}

// ── MCP Server ──────────────────────────────────────────────────────────

const server = new Server(
  { name: 'clove', version: '0.1.0' },
  { capabilities: { tools: {}, resources: {} } },
)

// ── Tools ───────────────────────────────────────────────────────────────

server.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: [
    {
      name: 'remember',
      description:
        'Store a fact or finding in CLOVE kernel memory. ' +
        'Memory persists across runs and is shared with other agents based on access level. ' +
        'Types: "system" (pinned, high priority), "core" (always in context), "recall" (on demand). ' +
        'Access: "private", "shared_read", "shared_readwrite".',
      inputSchema: {
        type: 'object' as const,
        properties: {
          name: { type: 'string', description: 'Short name for this memory (e.g. "rust-findings")' },
          content: { type: 'string', description: 'The fact or finding to remember' },
          type: { type: 'string', enum: ['system', 'core', 'recall'], description: 'Memory type (default: core)' },
          access: { type: 'string', enum: ['private', 'shared_read', 'shared_readwrite'], description: 'Access level (default: shared_read)' },
        },
        required: ['name', 'content'],
      },
    },
    {
      name: 'recall',
      description:
        'Retrieve facts from CLOVE kernel memory. ' +
        'Returns all memory blocks visible to this agent, including shared blocks from other agents.',
      inputSchema: {
        type: 'object' as const,
        properties: {
          query: { type: 'string', description: 'What to search for (used for filtering)' },
        },
        required: [],
      },
    },
    {
      name: 'share',
      description: 'Share a memory block with another agent by ID.',
      inputSchema: {
        type: 'object' as const,
        properties: {
          memory_id: { type: 'string', description: 'Memory block ID to share' },
          agent_id: { type: 'number', description: 'Agent ID to share with' },
        },
        required: ['memory_id', 'agent_id'],
      },
    },
    {
      name: 'artifact',
      description:
        'Create a typed artifact in the kernel. ' +
        'Types: query, research, analysis, synthesis, report, note, plan. ' +
        'Artifacts are tied to execution traces and can be promoted through states: draft → in_review → approved → final.',
      inputSchema: {
        type: 'object' as const,
        properties: {
          title: { type: 'string', description: 'Artifact title' },
          content: { type: 'string', description: 'Artifact content' },
          type: { type: 'string', enum: ['query', 'research', 'analysis', 'synthesis', 'report', 'note', 'plan'], description: 'Artifact type (default: note)' },
        },
        required: ['title', 'content'],
      },
    },
    {
      name: 'agents',
      description: 'List all agents running in the CLOVE kernel, their permissions, budgets, and status.',
      inputSchema: {
        type: 'object' as const,
        properties: {},
        required: [],
      },
    },
    {
      name: 'cost',
      description: 'Check total LLM cost tracked by the kernel.',
      inputSchema: {
        type: 'object' as const,
        properties: {},
        required: [],
      },
    },
    {
      name: 'search',
      description: 'Search the web through the CLOVE kernel. Results are audited and cost-tracked.',
      inputSchema: {
        type: 'object' as const,
        properties: {
          query: { type: 'string', description: 'Search query' },
        },
        required: ['query'],
      },
    },
    {
      name: 'run',
      description:
        'Delegate a task to a CLOVE RunEngine agent. ' +
        'The agent plans, uses tools, and returns a result. Useful for complex sub-tasks.',
      inputSchema: {
        type: 'object' as const,
        properties: {
          goal: { type: 'string', description: 'What the agent should accomplish' },
          budget: { type: 'number', description: 'Max budget in USD (default: 0.10)' },
          tools: {
            type: 'array',
            items: { type: 'string' },
            description: 'Tools the agent can use (default: all)',
          },
        },
        required: ['goal'],
      },
    },
  ],
}))

// ── Tool execution ──────────────────────────────────────────────────────

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params
  const a = args as Record<string, unknown>

  try {
    switch (name) {
      case 'remember': {
        const result = await api<{ id: string; name: string }>('/api/memory', 'POST', {
          name: a.name || 'unnamed',
          content: a.content || '',
          type: a.type || 'core',
          access: a.access || 'shared_read',
        })
        return {
          content: [{ type: 'text' as const, text: `Stored memory block "${result.name}" (${result.id})` }],
        }
      }

      case 'recall': {
        const result = await api<{ blocks: Array<{ name: string; type: string; content: string; access: string }> }>('/api/memory')
        const blocks = result.blocks || []
        if (blocks.length === 0) {
          return { content: [{ type: 'text' as const, text: 'No memory blocks found in CLOVE kernel.' }] }
        }
        // Return ALL blocks — let the LLM filter by relevance.
        // Filtering here loses data because block names don't always match queries.
        const text = blocks
          .map((b) => `[${b.type}/${b.access}] ${b.name}:\n${b.content}`)
          .join('\n\n---\n\n')
        return { content: [{ type: 'text' as const, text: `Found ${blocks.length} memory block(s) in CLOVE kernel:\n\n${text}` }] }
      }

      case 'share': {
        const result = await api<{ success: boolean }>(`/api/memory/${a.memory_id}/share/${a.agent_id}`, 'POST')
        return {
          content: [{ type: 'text' as const, text: result.success ? 'Memory shared.' : 'Failed to share.' }],
        }
      }

      case 'artifact': {
        // Create via the run history (artifacts are tied to traces)
        const result = await api<{ id: string }>('/api/memory', 'POST', {
          name: `artifact:${a.type || 'note'}:${a.title}`,
          content: a.content || '',
          type: 'core',
          access: 'shared_read',
        })
        return {
          content: [{ type: 'text' as const, text: `Created artifact "${a.title}" (${result.id})` }],
        }
      }

      case 'agents': {
        const result = await api<{ agents: Array<Record<string, unknown>> }>('/api/sandbox/overview')
        if (result.agents.length === 0) {
          return { content: [{ type: 'text' as const, text: 'No agents running.' }] }
        }
        const text = result.agents
          .map((ag) => {
            const perms = ag.permissions as Record<string, unknown> || {}
            return `${ag.name} (${ag.state}) — PID ${ag.pid}\n` +
              `  Budget: $${ag.budget_usd}\n` +
              `  Channels: ${JSON.stringify(ag.channels)}\n` +
              `  Permissions: read=${perms.can_read} write=${perms.can_write} exec=${perms.can_exec} http=${perms.can_http}`
          })
          .join('\n\n')
        return { content: [{ type: 'text' as const, text }] }
      }

      case 'cost': {
        const result = await api<{ total_cost_usd: number; max_cost_usd: number }>('/api/cost')
        return {
          content: [{
            type: 'text' as const,
            text: `Total cost: $${result.total_cost_usd.toFixed(6)}` +
              (result.max_cost_usd > 0 ? ` / $${result.max_cost_usd.toFixed(2)} budget` : ''),
          }],
        }
      }

      case 'search': {
        const result = await api<{ content: string; success: boolean }>('/api/run', 'POST', {
          goal: `Search the web for: ${a.query}. Return the top results.`,
          budget: 0.05,
          tools: ['search'],
          max_steps: 2,
        })
        return {
          content: [{ type: 'text' as const, text: result.content || 'Search failed.' }],
        }
      }

      case 'run': {
        const result = await api<{ content: string; success: boolean; total_cost_usd: number; steps: number }>(
          '/api/run', 'POST', {
            goal: a.goal,
            budget: a.budget || 0.10,
            tools: a.tools || [],
          },
        )
        return {
          content: [{
            type: 'text' as const,
            text: `[${result.success ? 'SUCCESS' : 'FAILED'} | ${result.steps} steps | $${result.total_cost_usd.toFixed(6)}]\n\n${result.content}`,
          }],
        }
      }

      default:
        return { content: [{ type: 'text' as const, text: `Unknown tool: ${name}` }] }
    }
  } catch (e) {
    return { content: [{ type: 'text' as const, text: `Error: ${e}` }] }
  }
})

// ── Resources ───────────────────────────────────────────────────────────

server.setRequestHandler(ListResourcesRequestSchema, async () => {
  const memory = await api<{ blocks: Array<{ id: string; name: string; type: string }> }>('/api/memory')
  const history = await api<{ runs: Array<{ chain_id: string; name: string; description: string }> }>('/api/history')

  const resources = [
    ...memory.blocks.map((b) => ({
      uri: `clove://memory/${b.id}`,
      name: `Memory: ${b.name}`,
      description: `${b.type} memory block`,
      mimeType: 'text/plain',
    })),
    ...history.runs.map((r) => ({
      uri: `clove://trace/${r.chain_id}`,
      name: `Trace: ${r.name}`,
      description: r.description,
      mimeType: 'application/json',
    })),
  ]

  return { resources }
})

server.setRequestHandler(ReadResourceRequestSchema, async (request) => {
  const uri = request.params.uri
  const [, type, id] = uri.match(/^clove:\/\/(\w+)\/(.+)$/) || []

  if (type === 'memory') {
    const block = await api<{ content: string; name: string; type: string }>(`/api/memory/${id}`)
    return {
      contents: [{
        uri,
        mimeType: 'text/plain',
        text: `[${block.type}] ${block.name}\n\n${block.content}`,
      }],
    }
  }

  if (type === 'trace') {
    const history = await api<{ runs: Array<Record<string, unknown>> }>('/api/history')
    const run = history.runs.find((r) => r.chain_id === id)
    return {
      contents: [{
        uri,
        mimeType: 'application/json',
        text: JSON.stringify(run || { error: 'trace not found' }, null, 2),
      }],
    }
  }

  return { contents: [{ uri, mimeType: 'text/plain', text: 'Unknown resource type' }] }
})

// ── Start ───────────────────────────────────────────────────────────────

async function main() {
  const transport = new StdioServerTransport()
  await server.connect(transport)
  console.error('CLOVE MCP server running on stdio')
  console.error(`Kernel: ${KERNEL_URL}`)
}

main().catch(console.error)
