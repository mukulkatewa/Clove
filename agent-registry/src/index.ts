#!/usr/bin/env node
/**
 * CLOVE Agent Registry + Runtime
 *
 * A sidecar service that manages persistent agents.
 * - Agent CRUD via HTTP API (port 8090)
 * - Cron trigger evaluation every 60s
 * - Webhook ingestion at POST /events/ingest
 * - Connection management
 * - Daily budget enforcement
 *
 * Usage:
 *   node dist/index.js          # start on port 8090
 *   REGISTRY_PORT=9000 node dist/index.js
 */

import { createServer, type IncomingMessage, type ServerResponse } from 'node:http'
import { Registry } from './registry.js'
import { Runtime } from './runtime.js'
import type { AgentDefinition, Connection, InboundEvent } from './types.js'

const PORT = parseInt(process.env.REGISTRY_PORT || '8090')

const registry = new Registry()
const runtime = new Runtime(registry)

// ── HTTP helpers ──────────────────────────────────────────────────

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve) => {
    let data = ''
    req.on('data', (chunk: Buffer) => { data += chunk.toString() })
    req.on('end', () => resolve(data))
  })
}

function json(res: ServerResponse, status: number, data: unknown) {
  res.writeHead(status, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' })
  res.end(JSON.stringify(data))
}

// ── HTTP server ───────────────────────────────────────────────────

const server = createServer(async (req, res) => {
  const url = new URL(req.url || '/', `http://localhost:${PORT}`)
  const path = url.pathname
  const method = req.method || 'GET'

  // CORS preflight
  if (method === 'OPTIONS') {
    res.writeHead(200, { 'Access-Control-Allow-Origin': '*', 'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE', 'Access-Control-Allow-Headers': 'Content-Type' })
    res.end()
    return
  }

  try {
    // ── Agents ────────────────────────────────────────────────

    if (path === '/agents' && method === 'GET') {
      return json(res, 200, { agents: registry.list(), count: registry.list().length })
    }

    if (path === '/agents' && method === 'POST') {
      const body = JSON.parse(await readBody(req)) as AgentDefinition
      registry.create(body)
      return json(res, 201, body)
    }

    const agentMatch = path.match(/^\/agents\/([^/]+)$/)
    if (agentMatch) {
      const name = decodeURIComponent(agentMatch[1])

      if (method === 'GET') {
        const agent = registry.get(name)
        if (!agent) return json(res, 404, { error: 'Agent not found' })
        return json(res, 200, agent)
      }

      if (method === 'PUT') {
        const updates = JSON.parse(await readBody(req)) as Partial<AgentDefinition>
        const updated = registry.update(name, updates)
        if (!updated) return json(res, 404, { error: 'Agent not found' })
        return json(res, 200, updated)
      }

      if (method === 'DELETE') {
        registry.delete(name)
        return json(res, 200, { success: true })
      }
    }

    if (path.match(/^\/agents\/([^/]+)\/enable$/) && method === 'POST') {
      const name = decodeURIComponent(path.split('/')[2])
      registry.enable(name)
      return json(res, 200, { success: true })
    }

    if (path.match(/^\/agents\/([^/]+)\/disable$/) && method === 'POST') {
      const name = decodeURIComponent(path.split('/')[2])
      registry.disable(name)
      return json(res, 200, { success: true })
    }

    if (path.match(/^\/agents\/([^/]+)\/run$/) && method === 'POST') {
      const name = decodeURIComponent(path.split('/')[2])
      const agent = registry.get(name)
      if (!agent) return json(res, 404, { error: 'Agent not found' })
      const run = await runtime.runAgent(agent, { type: 'manual' })
      return json(res, 200, run)
    }

    // ── Connections ───────────────────────────────────────────

    if (path === '/connections' && method === 'GET') {
      return json(res, 200, { connections: registry.listConnections() })
    }

    if (path === '/connections' && method === 'POST') {
      const conn = JSON.parse(await readBody(req)) as Connection
      registry.setConnection(conn)
      return json(res, 201, conn)
    }

    const connMatch = path.match(/^\/connections\/([^/]+)$/)
    if (connMatch && method === 'DELETE') {
      registry.removeConnection(decodeURIComponent(connMatch[1]))
      return json(res, 200, { success: true })
    }

    // ── Webhook Ingestion ─────────────────────────────────────

    if (path === '/events/ingest' && method === 'POST') {
      const body = JSON.parse(await readBody(req))
      const event: InboundEvent = {
        id: `evt-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
        source: (req.headers['x-event-source'] as string) || body.source || 'unknown',
        type: (req.headers['x-event-type'] as string) || body.type || 'generic',
        payload: body,
        received_at: new Date().toISOString(),
      }
      console.log(`  Event received: ${event.source}/${event.type}`)
      const runs = await runtime.handleWebhook(event)
      return json(res, 200, { event_id: event.id, agents_triggered: runs.length, runs })
    }

    // ── History ───────────────────────────────────────────────

    if (path === '/history' && method === 'GET') {
      const limit = parseInt(url.searchParams.get('limit') || '50')
      return json(res, 200, { runs: runtime.getHistory(limit) })
    }

    // ── Health ────────────────────────────────────────────────

    if (path === '/health') {
      return json(res, 200, {
        status: 'ok',
        agents: registry.list().length,
        enabled: registry.list().filter(a => a.enabled).length,
        connections: registry.listConnections().length,
      })
    }

    // 404
    json(res, 404, { error: 'Not found' })
  } catch (e) {
    json(res, 500, { error: (e as Error).message })
  }
})

// ── Start ─────────────────────────────────────────────────────────

server.listen(PORT, () => {
  console.log()
  console.log('  CLOVE Agent Registry')
  console.log('  ─────────────────────────────')
  console.log(`  API:         http://localhost:${PORT}`)
  console.log(`  Agents:      ${registry.list().length} registered`)
  console.log(`  Connections: ${registry.listConnections().length} configured`)
  console.log('  ─────────────────────────────')
  console.log()
  console.log('  Endpoints:')
  console.log('    GET/POST       /agents')
  console.log('    GET/PUT/DELETE  /agents/:name')
  console.log('    POST           /agents/:name/run')
  console.log('    POST           /agents/:name/enable')
  console.log('    POST           /agents/:name/disable')
  console.log('    GET/POST       /connections')
  console.log('    POST           /events/ingest')
  console.log('    GET            /history')
  console.log()

  // Start the runtime loop (cron evaluation)
  runtime.start().catch(console.error)
})
