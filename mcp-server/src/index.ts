#!/usr/bin/env node
/**
 * CLOVE MCP Server — unified, full-coverage
 *
 * Exposes every kernel API endpoint as MCP tools + resources.
 * Merged from mcp-server/ and integrations/mcp-server/.
 *
 * Modes:
 *   stdio  — local Claude Code / any MCP host (default)
 *   HTTP   — set PORT env var for remote deployment (Railway, Fly, VPS)
 *
 * Auth (HTTP mode):
 *   Authorization: Bearer <CLOVE_MCP_KEY>
 *   x-api-key: <CLOVE_MCP_KEY>
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { z } from "zod";
import http from "node:http";

const KERNEL_URL = process.env.CLOVE_KERNEL_URL || process.env.CLOVE_API_URL || "http://localhost:8080";
const PORT = process.env.PORT ? parseInt(process.env.PORT) : null;
const MCP_KEY = process.env.CLOVE_MCP_KEY ?? "";

// Warn on startup if using default localhost (likely not configured)
if (!process.env.CLOVE_KERNEL_URL && !process.env.CLOVE_API_URL && !PORT) {
  console.error(`
╔─────────────────────────────────────────────────────╗
│  CLOVE MCP Server                                   │
│                                                     │
│  No kernel URL set. Using localhost:8080.           │
│                                                     │
│  To use the hosted kernel, add to mcp.json:         │
│                                                     │
│  "env": {                                           │
│    "CLOVE_KERNEL_URL": "https://kernel-production-96de.up.railway.app",
│    "CLOVE_API_KEY":    "clove-prod-70f36e07a895a89c1fd82b84ec35a4d7"
│  }                                                  │
╚─────────────────────────────────────────────────────╝
`);
}

// ── Kernel API helper ────────────────────────────────────────────────────────

async function kernel<T>(
  path: string,
  method = "GET",
  body?: unknown
): Promise<T> {
  const apiKey = process.env.CLOVE_API_KEY ?? "";
  const opts: RequestInit = {
    method,
    headers: {
      "Content-Type": "application/json",
      ...(apiKey ? { "Authorization": `Bearer ${apiKey}` } : {}),
    },
  };
  if (body !== undefined) opts.body = JSON.stringify(body);
  const res = await fetch(`${KERNEL_URL}${path}`, opts);
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`Kernel ${method} ${path} → ${res.status}: ${text}`);
  }
  return res.json() as Promise<T>;
}

function txt(text: string) {
  return { content: [{ type: "text" as const, text }] };
}

function json(data: unknown) {
  return txt(JSON.stringify(data, null, 2));
}

// ── Server ───────────────────────────────────────────────────────────────────

function buildServer(): McpServer {
  const server = new McpServer(
    { name: "clove", version: "2.0.0" },
    { capabilities: { tools: {}, resources: {} } }
  );

  // ── STATUS & HEALTH ────────────────────────────────────────────────────────

  server.tool(
    "clove_status",
    "Get CLOVE kernel health — version, uptime, syscall count, active agents, and current cost.",
    {},
    async () => {
      const [health, cost] = await Promise.all([
        kernel<{ status: string; version: string; uptime_s: number; syscall_count: number; agent_count?: number }>("/api/health"),
        kernel<{ total_cost_usd: number; max_cost_usd: number; total_requests: number }>("/api/cost"),
      ]);
      return txt(
        `CLOVE v${health.version} | ${health.status} | uptime ${health.uptime_s}s | ` +
        `${health.syscall_count} syscalls | $${cost.total_cost_usd.toFixed(6)} spent`
      );
    }
  );

  server.tool(
    "clove_cost",
    "Get current LLM cost tracking — total spend, budget cap, and request count.",
    {},
    async () => {
      const r = await kernel<{ total_cost_usd: number; max_cost_usd: number; total_requests: number }>("/api/cost");
      return txt(
        `Total: $${r.total_cost_usd.toFixed(6)} | Budget: $${r.max_cost_usd.toFixed(2)} | Requests: ${r.total_requests}`
      );
    }
  );

  server.tool(
    "clove_metrics",
    "Get system-wide metrics from the kernel — CPU, memory, agent counts, throughput.",
    {},
    async () => json(await kernel("/api/metrics"))
  );

  // ── EXECUTION ──────────────────────────────────────────────────────────────

  server.tool(
    "clove_run",
    "Run a single AI agent on a goal. The kernel assigns the agent, calls tools (file I/O, shell, HTTP, search, MCP), enforces budget and permissions, and returns the result.",
    {
      goal: z.string().describe("What the agent should accomplish"),
      budget: z.number().optional().describe("Max cost in USD (default 0.50)"),
      tools: z.array(z.string()).optional().describe("Allowed tools: read_file, write_file, exec, http, search, remember, recall, mcp_call, delegate"),
      model: z.string().optional().describe("LLM model override"),
      agent_name: z.string().optional().describe("Named agent definition to use"),
      workspace_id: z.string().optional().describe("Workspace context"),
    },
    async ({ goal, budget, tools, model, agent_name, workspace_id }) => {
      const r = await kernel<{
        success: boolean; content: string; total_cost_usd: number;
        total_tokens: number; steps: number;
        step_log?: Array<{ step: number; tool: string; args: unknown }>;
      }>("/api/run", "POST", { goal, budget: budget ?? 0.5, tools, model, agent_name, workspace_id });
      const steps = r.step_log?.map((s) =>
        `  ${s.step}. ${s.tool}(${JSON.stringify(s.args).slice(0, 60)})`
      ).join("\n") ?? "";
      return txt(
        `${r.success ? "SUCCESS" : "FAILED"} | ${r.steps} steps | $${r.total_cost_usd.toFixed(6)} | ${r.total_tokens} tokens\n\n${r.content}` +
        (steps ? `\n\nSteps:\n${steps}` : "")
      );
    }
  );

  server.tool(
    "clove_run_stream",
    "Run an agent and get a streaming response (SSE). Returns the full buffered output when done.",
    {
      goal: z.string(),
      budget: z.number().optional(),
      tools: z.array(z.string()).optional(),
      model: z.string().optional(),
    },
    async ({ goal, budget, tools, model }) => {
      // Buffer SSE stream into a single result
      const res = await fetch(`${KERNEL_URL}/api/run/stream`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ goal, budget: budget ?? 0.5, tools, model }),
      });
      const text = await res.text();
      const lines = text.split("\n").filter((l) => l.startsWith("data:"));
      const chunks = lines.map((l) => {
        try { return JSON.parse(l.slice(5)); } catch { return null; }
      }).filter(Boolean);
      return json(chunks);
    }
  );

  server.tool(
    "clove_fleet",
    "Run multiple AI agents in parallel on the same goal. Each works independently; results are synthesized.",
    {
      goal: z.string().describe("Shared goal for all agents"),
      agents: z.number().optional().describe("Number of parallel agents (default 3)"),
      budget: z.number().optional().describe("Total budget across all agents (default 1.00)"),
    },
    async ({ goal, agents, budget }) => {
      const r = await kernel<{ success: boolean; content: string; total_cost_usd: number; agent_count: number }>(
        "/api/fleet", "POST", { goal, agents: agents ?? 3, budget: budget ?? 1.0 }
      );
      return txt(`${r.success ? "SUCCESS" : "FAILED"} | ${r.agent_count} agents | $${r.total_cost_usd.toFixed(6)}\n\n${r.content}`);
    }
  );

  server.tool(
    "clove_think",
    "Direct LLM inference call through the kernel's inference gateway (cost-tracked, policy-enforced).",
    {
      prompt: z.string().describe("The prompt to send"),
      model: z.string().optional().describe("Model override"),
      max_tokens: z.number().optional(),
    },
    async ({ prompt, model, max_tokens }) =>
      json(await kernel("/api/think", "POST", { prompt, model, max_tokens }))
  );

  // ── JOBS ───────────────────────────────────────────────────────────────────

  server.tool(
    "clove_submit_job",
    "Submit a job to the async pipeline. Jobs are queued, executed, and can depend on other jobs (for chaining pipelines). Returns a job_id immediately.",
    {
      goal: z.string().describe("What the job should accomplish"),
      agent_name: z.string().optional().describe("Named agent definition to use"),
      workspace_id: z.string().optional().describe("Workspace context"),
      priority: z.number().optional().describe("Priority 0=highest (default 0)"),
      depends_on: z.string().optional().describe("Job ID this job waits for before starting"),
      budget: z.number().optional().describe("Max cost in USD"),
    },
    async ({ goal, agent_name, workspace_id, priority, depends_on, budget }) => {
      const r = await kernel<{ job_id: string; status: string }>(
        "/api/jobs", "POST", { goal, agent_name, workspace_id, priority, depends_on, budget }
      );
      return txt(`Job submitted: ${r.job_id} | status: ${r.status}`);
    }
  );

  server.tool(
    "clove_get_job",
    "Get status and result of a job by ID. Poll this after clove_submit_job.",
    {
      job_id: z.string().describe("Job ID from clove_submit_job"),
    },
    async ({ job_id }) => json(await kernel(`/api/jobs/${job_id}`))
  );

  server.tool(
    "clove_list_jobs",
    "List jobs with optional filters.",
    {
      status: z.enum(["queued", "running", "done", "failed", "cancelled"]).optional(),
      workspace_id: z.string().optional(),
      limit: z.number().optional().describe("Max results (default 20)"),
    },
    async ({ status, workspace_id, limit }) => {
      const params = new URLSearchParams();
      if (status) params.set("status", status);
      if (workspace_id) params.set("workspace_id", workspace_id);
      if (limit) params.set("limit", String(limit));
      return json(await kernel(`/api/jobs?${params}`));
    }
  );

  server.tool(
    "clove_cancel_job",
    "Cancel a queued job.",
    { job_id: z.string() },
    async ({ job_id }) => json(await kernel(`/api/jobs/${job_id}`, "DELETE"))
  );

  server.tool(
    "clove_retry_job",
    "Re-queue a failed job.",
    { job_id: z.string() },
    async ({ job_id }) => json(await kernel(`/api/jobs/${job_id}/retry`, "POST"))
  );

  server.tool(
    "clove_get_run",
    "Fetch a completed run by chain_id.",
    { chain_id: z.string() },
    async ({ chain_id }) => json(await kernel(`/api/run/${chain_id}`))
  );

  server.tool(
    "clove_run_history",
    "Get execution history — past runs with cost and outcome.",
    { limit: z.number().optional() },
    async ({ limit }) => {
      const params = limit ? `?limit=${limit}` : "";
      return json(await kernel(`/api/history${params}`));
    }
  );

  // ── AGENTS (runtime) ───────────────────────────────────────────────────────

  server.tool(
    "clove_list_agents",
    "List all currently running agent processes with state, PID, and cost.",
    {},
    async () => json(await kernel("/api/agents"))
  );

  server.tool(
    "clove_spawn_agent",
    "Spawn a new agent process.",
    {
      name: z.string().describe("Agent name"),
      goal: z.string().optional(),
      budget: z.number().optional(),
      tools: z.array(z.string()).optional(),
    },
    async ({ name, goal, budget, tools }) =>
      json(await kernel("/api/agents", "POST", { name, goal, budget, tools }))
  );

  server.tool(
    "clove_kill_agent",
    "Terminate a running agent by ID.",
    { agent_id: z.number() },
    async ({ agent_id }) => json(await kernel(`/api/agents/${agent_id}`, "DELETE"))
  );

  server.tool(
    "clove_restart_agent",
    "Restart an agent process.",
    { agent_id: z.number() },
    async ({ agent_id }) => json(await kernel(`/api/agents/${agent_id}/restart`, "POST"))
  );

  server.tool(
    "clove_agent_metrics",
    "Get performance metrics for a specific agent.",
    { agent_id: z.number() },
    async ({ agent_id }) => json(await kernel(`/api/agents/${agent_id}/metrics`))
  );

  server.tool(
    "clove_message_agent",
    "Send a message to an agent's mailbox (IPC).",
    {
      agent_id: z.number(),
      message: z.string(),
      channel: z.string().optional(),
    },
    async ({ agent_id, message, channel }) =>
      json(await kernel(`/api/agents/${agent_id}/message`, "POST", { message, channel }))
  );

  server.tool(
    "clove_get_messages",
    "Read messages from an agent's mailbox.",
    { agent_id: z.number() },
    async ({ agent_id }) => json(await kernel(`/api/agents/${agent_id}/messages`))
  );

  server.tool(
    "clove_broadcast",
    "Broadcast a message to all running agents.",
    { message: z.string(), channel: z.string().optional() },
    async ({ message, channel }) =>
      json(await kernel("/api/broadcast", "POST", { message, channel }))
  );

  server.tool(
    "clove_sandbox_overview",
    "Get a full snapshot of all agents with their permissions and budgets.",
    {},
    async () => {
      const r = await kernel<{ agents: Array<Record<string, unknown>> }>("/api/sandbox/overview");
      const lines = (r.agents || []).map((ag) => {
        const p = ag.permissions as Record<string, unknown> ?? {};
        return `${ag.name} (${ag.state}) PID:${ag.pid} $${ag.budget_usd}\n` +
          `  perms: read=${p.can_read} write=${p.can_write} exec=${p.can_exec} http=${p.can_http}`;
      }).join("\n\n");
      return txt(lines || "No agents running.");
    }
  );

  // ── AGENT DEFINITIONS (persistent registry) ────────────────────────────────

  server.tool(
    "clove_list_agent_defs",
    "List all saved agent definitions in the registry.",
    {},
    async () => json(await kernel("/api/agent-defs"))
  );

  server.tool(
    "clove_define_agent",
    "Create or update a named, reusable agent definition with system prompt, tools, model, and budget.",
    {
      name: z.string().describe("Unique agent name (slug)"),
      system_prompt: z.string().describe("Agent's system prompt / persona"),
      tools: z.array(z.string()).optional().describe("Allowed tools"),
      model: z.string().optional().describe("LLM model"),
      budget: z.number().optional().describe("Default budget per run in USD"),
    },
    async ({ name, system_prompt, tools, model, budget }) =>
      json(await kernel("/api/agent-defs", "POST", { name, system_prompt, tools, model, budget }))
  );

  server.tool(
    "clove_get_agent_def",
    "Get a specific agent definition by name.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/agent-defs/${name}`))
  );

  server.tool(
    "clove_update_agent_def",
    "Update an existing agent definition.",
    {
      name: z.string(),
      system_prompt: z.string().optional(),
      tools: z.array(z.string()).optional(),
      model: z.string().optional(),
      budget: z.number().optional(),
    },
    async ({ name, ...updates }) =>
      json(await kernel(`/api/agent-defs/${name}`, "PUT", updates))
  );

  server.tool(
    "clove_delete_agent_def",
    "Delete an agent definition.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/agent-defs/${name}`, "DELETE"))
  );

  server.tool(
    "clove_run_agent_def",
    "Manually trigger a named agent definition on a goal.",
    {
      name: z.string().describe("Agent definition name"),
      goal: z.string(),
      budget: z.number().optional(),
    },
    async ({ name, goal, budget }) =>
      json(await kernel(`/api/agent-defs/${name}/run`, "POST", { goal, budget }))
  );

  // ── DAEMONS (always-on agents) ─────────────────────────────────────────────

  server.tool(
    "clove_list_daemons",
    "List all daemon agents (tick-based, always-on processes).",
    {},
    async () => json(await kernel("/api/daemons"))
  );

  server.tool(
    "clove_get_daemon",
    "Get state of a specific daemon.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/daemons/${name}`))
  );

  server.tool(
    "clove_start_daemon",
    "Start a daemon agent by name.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/daemons/${name}/start`, "POST"))
  );

  server.tool(
    "clove_stop_daemon",
    "Stop a running daemon agent.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/daemons/${name}/stop`, "POST"))
  );

  server.tool(
    "clove_daemon_logs",
    "Get recent logs from a daemon agent.",
    { name: z.string(), limit: z.number().optional() },
    async ({ name, limit }) => {
      const params = limit ? `?limit=${limit}` : "";
      return json(await kernel(`/api/daemons/${name}/logs${params}`));
    }
  );

  server.tool(
    "clove_daemon_dream",
    "Trigger memory consolidation for a daemon (the 'dream' cycle — compresses episodic memory into long-term).",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/daemons/${name}/dream`, "POST"))
  );

  // ── WORLDS (multi-agent simulation) ───────────────────────────────────────

  server.tool(
    "clove_list_worlds",
    "List all active worlds — isolated multi-agent coordination environments.",
    {},
    async () => {
      const r = await kernel<{ worlds: Array<{ id: number; name: string; member_count: number }> }>("/api/worlds");
      return json(r.worlds);
    }
  );

  server.tool(
    "clove_create_world",
    "Create a new isolated world for multi-agent coordination.",
    { name: z.string() },
    async ({ name }) => json(await kernel("/api/worlds", "POST", { name }))
  );

  server.tool(
    "clove_delete_world",
    "Delete a world and remove all its agents.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/worlds/${name}`, "DELETE"))
  );

  server.tool(
    "clove_world_agents",
    "List agents currently in a world.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/worlds/${name}/agents`))
  );

  server.tool(
    "clove_add_to_world",
    "Add an agent to a world.",
    { world_name: z.string(), agent_id: z.number() },
    async ({ world_name, agent_id }) =>
      json(await kernel(`/api/worlds/${world_name}/agents`, "POST", { agent_id }))
  );

  server.tool(
    "clove_launch_world",
    "Launch a multi-agent world from a template (e.g. code-health).",
    {
      template: z.string().describe("World template name"),
      params: z.record(z.string()).optional().describe("Template parameters"),
    },
    async ({ template, params }) => {
      const { execSync } = await import("node:child_process");
      const paramArgs = Object.values(params ?? {}).join(" ");
      try {
        const output = execSync(
          `python3 ${process.env.CLOVE_ROOT ?? "."}/examples/worlds/${template}.py ${paramArgs}`,
          { timeout: 120_000, encoding: "utf-8" }
        );
        return txt(output);
      } catch (e) {
        return txt(`World '${template}' failed: ${(e as Error).message}`);
      }
    }
  );

  // ── SWARMS ─────────────────────────────────────────────────────────────────

  server.tool(
    "clove_list_swarms",
    "List all swarm job specs.",
    {},
    async () => json(await kernel("/api/swarms"))
  );

  server.tool(
    "clove_create_swarm",
    "Create a new swarm — a coordinated multi-agent job spec.",
    {
      name: z.string(),
      goal: z.string(),
      agent_count: z.number().optional(),
      budget: z.number().optional(),
    },
    async ({ name, goal, agent_count, budget }) =>
      json(await kernel("/api/swarms", "POST", { name, goal, agent_count, budget }))
  );

  server.tool(
    "clove_start_swarm",
    "Start executing a swarm.",
    { swarm_id: z.string() },
    async ({ swarm_id }) => json(await kernel(`/api/swarms/${swarm_id}/start`, "POST"))
  );

  server.tool(
    "clove_delete_swarm",
    "Delete a swarm.",
    { swarm_id: z.string() },
    async ({ swarm_id }) => json(await kernel(`/api/swarms/${swarm_id}`, "DELETE"))
  );

  // ── SCHEDULES & WEBHOOKS ───────────────────────────────────────────────────

  server.tool(
    "clove_list_schedules",
    "List all cron schedules.",
    {},
    async () => json(await kernel("/api/schedules"))
  );

  server.tool(
    "clove_create_schedule",
    "Create a cron-triggered job schedule.",
    {
      name: z.string().describe("Schedule name"),
      cron: z.string().describe("Cron expression (e.g. '0 9 * * 1-5')"),
      goal: z.string().describe("Goal to run on each trigger"),
      agent_name: z.string().optional(),
      workspace_id: z.string().optional(),
      budget: z.number().optional(),
    },
    async ({ name, cron, goal, agent_name, workspace_id, budget }) =>
      json(await kernel("/api/schedules", "POST", { name, cron, goal, agent_name, workspace_id, budget }))
  );

  server.tool(
    "clove_delete_schedule",
    "Delete a schedule by name.",
    { name: z.string() },
    async ({ name }) => json(await kernel(`/api/schedules/${name}`, "DELETE"))
  );

  server.tool(
    "clove_list_webhooks",
    "List all registered webhooks.",
    {},
    async () => json(await kernel("/api/webhooks"))
  );

  server.tool(
    "clove_register_webhook",
    "Register a webhook — when called, it triggers a job in the kernel.",
    {
      url: z.string().describe("Webhook endpoint URL"),
      goal: z.string().describe("Goal to run when webhook fires"),
      agent_name: z.string().optional(),
      secret: z.string().optional().describe("Webhook secret for signature verification"),
    },
    async ({ url, goal, agent_name, secret }) =>
      json(await kernel("/api/webhooks", "POST", { url, goal, agent_name, secret }))
  );

  server.tool(
    "clove_delete_webhook",
    "Delete a webhook.",
    { webhook_id: z.string() },
    async ({ webhook_id }) => json(await kernel(`/api/webhooks/${webhook_id}`, "DELETE"))
  );

  // ── MEMORY ─────────────────────────────────────────────────────────────────

  server.tool(
    "remember",
    "Store a fact or finding in CLOVE kernel memory. Persists across runs and is shared with other agents based on access level. Types: system (pinned), core (always in context), recall (on demand).",
    {
      name: z.string().describe("Short name for this memory (e.g. 'rust-findings')"),
      content: z.string().describe("The fact or finding to remember"),
      type: z.enum(["system", "core", "recall"]).optional().describe("Memory type (default: core)"),
      access: z.enum(["private", "shared_read", "shared_readwrite"]).optional().describe("Access level (default: shared_read)"),
    },
    async ({ name, content, type, access }) => {
      const r = await kernel<{ id: string; name: string }>("/api/memory", "POST", {
        name, content, type: type ?? "core", access: access ?? "shared_read",
      });
      return txt(`Stored memory block "${r.name}" (${r.id})`);
    }
  );

  server.tool(
    "recall",
    "Retrieve facts from CLOVE kernel memory. Returns all memory blocks visible to this agent, including shared blocks from other agents.",
    {
      query: z.string().optional().describe("Optional search query for filtering"),
    },
    async ({ query }) => {
      if (query) {
        const r = await kernel<{ blocks: Array<Record<string, unknown>> }>(`/api/memory/search?q=${encodeURIComponent(query)}&limit=20`);
        const blocks = r.blocks ?? [];
        if (!blocks.length) return txt("No matching memory blocks.");
        return txt(blocks.map((b) => `[${b.type}/${b.access}] ${b.name}:\n${b.content}`).join("\n\n---\n\n"));
      }
      const r = await kernel<{ blocks: Array<{ name: string; type: string; content: string; access: string }> }>("/api/memory");
      const blocks = r.blocks ?? [];
      if (!blocks.length) return txt("No memory blocks found.");
      return txt(`${blocks.length} block(s):\n\n` + blocks.map((b) => `[${b.type}/${b.access}] ${b.name}:\n${b.content}`).join("\n\n---\n\n"));
    }
  );

  server.tool(
    "clove_write_memory",
    "Create or update a memory block by ID.",
    {
      id: z.string().optional().describe("Block ID to update (omit to create new)"),
      name: z.string(),
      content: z.string(),
      type: z.enum(["system", "core", "recall"]).optional(),
      access: z.enum(["private", "shared_read", "shared_readwrite"]).optional(),
    },
    async ({ id, name, content, type, access }) => {
      if (id) {
        return json(await kernel(`/api/memory/${id}`, "PUT", { name, content, type, access }));
      }
      return json(await kernel("/api/memory", "POST", { name, content, type: type ?? "core", access: access ?? "shared_read" }));
    }
  );

  server.tool(
    "clove_delete_memory",
    "Delete a memory block by ID.",
    { id: z.string() },
    async ({ id }) => json(await kernel(`/api/memory/${id}`, "DELETE"))
  );

  server.tool(
    "share",
    "Share a memory block with another agent by ID.",
    {
      memory_id: z.string(),
      agent_id: z.number(),
    },
    async ({ memory_id, agent_id }) => {
      const r = await kernel<{ success: boolean }>(`/api/memory/${memory_id}/share/${agent_id}`, "POST");
      return txt(r.success ? "Memory shared." : "Failed to share.");
    }
  );

  server.tool(
    "artifact",
    "Create a typed artifact in the kernel. Types: query, research, analysis, synthesis, report, note, plan. Artifacts track the provenance of AI-generated content.",
    {
      title: z.string(),
      content: z.string(),
      type: z.enum(["query", "research", "analysis", "synthesis", "report", "note", "plan"]).optional(),
    },
    async ({ title, content, type }) => {
      const r = await kernel<{ id: string }>("/api/memory", "POST", {
        name: `artifact:${type ?? "note"}:${title}`,
        content, type: "core", access: "shared_read",
      });
      return txt(`Created artifact "${title}" (${r.id})`);
    }
  );

  // ── GOVERNANCE & PERMISSIONS ───────────────────────────────────────────────

  server.tool(
    "clove_get_permissions",
    "Get an agent's RBAC permissions.",
    { agent_id: z.number() },
    async ({ agent_id }) => json(await kernel(`/api/agents/${agent_id}/permissions`))
  );

  server.tool(
    "clove_set_permissions",
    "Update an agent's RBAC permissions.",
    {
      agent_id: z.number(),
      can_read: z.boolean().optional(),
      can_write: z.boolean().optional(),
      can_exec: z.boolean().optional(),
      can_http: z.boolean().optional(),
      allowed_paths: z.array(z.string()).optional(),
      allowed_hosts: z.array(z.string()).optional(),
    },
    async ({ agent_id, ...perms }) =>
      json(await kernel(`/api/agents/${agent_id}/permissions`, "PUT", perms))
  );

  server.tool(
    "clove_get_budget",
    "Get remaining budget for an agent.",
    { agent_id: z.number() },
    async ({ agent_id }) => json(await kernel(`/api/agents/${agent_id}/budget`))
  );

  server.tool(
    "clove_set_budget",
    "Set the budget cap for an agent.",
    { agent_id: z.number(), budget_usd: z.number() },
    async ({ agent_id, budget_usd }) =>
      json(await kernel(`/api/agents/${agent_id}/budget`, "POST", { budget_usd }))
  );

  server.tool(
    "clove_scan_pii",
    "Scan text for PII (SSN, email, phone, credit card, IP). Returns findings with redaction suggestions.",
    { text: z.string() },
    async ({ text }) => json(await kernel("/api/privacy/scan", "POST", { text }))
  );

  server.tool(
    "clove_policy_recommendations",
    "Get policy recommendations based on recent permission denials.",
    {},
    async () => json(await kernel("/api/policy/recommendations"))
  );

  server.tool(
    "clove_get_inference_config",
    "Get the kernel's current inference configuration (model, provider, cost limits).",
    {},
    async () => json(await kernel("/api/inference"))
  );

  server.tool(
    "clove_set_inference_config",
    "Update inference configuration.",
    {
      model: z.string().optional(),
      provider: z.string().optional(),
      max_cost_usd: z.number().optional(),
    },
    async (config) => json(await kernel("/api/inference", "PUT", config))
  );

  // ── AUDIT LOG ──────────────────────────────────────────────────────────────

  server.tool(
    "clove_audit",
    "Query the kernel audit log — every agent action is logged with timestamp, category, and outcome.",
    {
      limit: z.number().optional().describe("Number of entries (default 20)"),
      category: z.string().optional().describe("Filter by category (llm, file_io, network, exec, state, memory, permission, audit)"),
    },
    async ({ limit, category }) => {
      const params = new URLSearchParams();
      if (limit) params.set("limit", String(limit));
      if (category) params.set("category", category);
      const entries = await kernel<Array<{ event_type: string; category: string; agent_name: string; success: boolean; timestamp: string }>>(
        `/api/audit?${params}`
      );
      const lines = entries.map((e) =>
        `${new Date(e.timestamp).toLocaleTimeString()} ${e.success ? "✓" : "✗"} [${e.category}] ${e.event_type.padEnd(20)} ${e.agent_name}`
      ).join("\n");
      return txt(lines || "No audit entries.");
    }
  );

  server.tool(
    "clove_export_audit",
    "Export the full audit log as JSONL.",
    {},
    async () => txt(await (await fetch(`${KERNEL_URL}/api/audit/export`)).text())
  );

  // ── WORKSPACES ─────────────────────────────────────────────────────────────

  server.tool(
    "clove_workspace_data",
    "Get the KV data store for a workspace.",
    { workspace_id: z.string() },
    async ({ workspace_id }) => json(await kernel(`/api/workspaces/${workspace_id}/data`))
  );

  server.tool(
    "clove_set_workspace_data",
    "Set key-value pairs in a workspace's data store.",
    {
      workspace_id: z.string(),
      data: z.record(z.string()).describe("Key-value pairs to set"),
    },
    async ({ workspace_id, data }) =>
      json(await kernel(`/api/workspaces/${workspace_id}/data`, "PUT", data))
  );

  server.tool(
    "clove_workspace_outputs",
    "Get outputs produced by a workspace.",
    { workspace_id: z.string() },
    async ({ workspace_id }) => json(await kernel(`/api/workspaces/${workspace_id}/outputs`))
  );

  server.tool(
    "clove_workspace_runs",
    "Get run history for a workspace.",
    { workspace_id: z.string() },
    async ({ workspace_id }) => json(await kernel(`/api/workspaces/${workspace_id}/runs`))
  );

  // ── MCP BRIDGE ─────────────────────────────────────────────────────────────

  server.tool(
    "clove_mcp_servers",
    "List all MCP servers registered with the kernel's MCP bridge.",
    {},
    async () => json(await kernel("/api/mcp/servers"))
  );

  server.tool(
    "clove_mcp_tools",
    "List all tools available through the kernel's MCP bridge (GitHub, Slack, Linear, databases, etc.).",
    {},
    async () => {
      const r = await kernel<{ tools: Array<{ server_name: string; name: string; description: string }> }>("/api/mcp/tools");
      const lines = (r.tools ?? []).map((t) => `[${t.server_name}] ${t.name} — ${t.description}`).join("\n");
      return txt(lines || "No MCP tools configured.");
    }
  );

  server.tool(
    "clove_call_mcp_tool",
    "Call any tool through the kernel's MCP bridge (e.g. GitHub, Slack, Linear, filesystem).",
    {
      server_name: z.string().describe("MCP server name (e.g. 'github', 'slack')"),
      tool_name: z.string().describe("Tool name"),
      args: z.record(z.unknown()).describe("Tool arguments"),
    },
    async ({ server_name, tool_name, args }) =>
      json(await kernel("/api/mcp/call", "POST", { server_name, tool_name, args }))
  );

  // ── STORE (KV) ─────────────────────────────────────────────────────────────

  server.tool(
    "clove_store_set",
    "Store a key-value pair in the kernel's global KV store.",
    { key: z.string(), value: z.string() },
    async ({ key, value }) => json(await kernel("/api/store", "POST", { key, value }))
  );

  server.tool(
    "clove_store_get",
    "Retrieve a value from the kernel's global KV store.",
    { key: z.string() },
    async ({ key }) => json(await kernel(`/api/store/${encodeURIComponent(key)}`))
  );

  // ── REPLAY ─────────────────────────────────────────────────────────────────

  server.tool(
    "clove_replay_status",
    "Get the current status of the execution replay engine.",
    {},
    async () => json(await kernel("/api/replay"))
  );

  server.tool(
    "clove_replay_start",
    "Start replaying a recorded execution.",
    { chain_id: z.string().optional() },
    async ({ chain_id }) => json(await kernel("/api/replay/start", "POST", { chain_id }))
  );

  server.tool(
    "clove_replay_stop",
    "Stop the current replay.",
    {},
    async () => json(await kernel("/api/replay/stop", "POST"))
  );

  // ── SEARCH (via kernel) ────────────────────────────────────────────────────

  server.tool(
    "search",
    "Search the web through the CLOVE kernel. Results are audited and cost-tracked.",
    { query: z.string() },
    async ({ query }) => {
      const r = await kernel<{ content: string; success: boolean }>("/api/run", "POST", {
        goal: `Search the web for: ${query}. Return the top results with sources.`,
        budget: 0.05, tools: ["search"], max_steps: 2,
      });
      return txt(r.content || "Search failed.");
    }
  );

  // ── RESOURCES ──────────────────────────────────────────────────────────────

  server.resource(
    "clove-memory",
    "clove://memory",
    { description: "All memory blocks in the CLOVE kernel" },
    async (uri) => {
      const id = uri.href.replace("clove://memory/", "");
      if (id && id !== "clove://memory") {
        const b = await kernel<{ content: string; name: string; type: string }>(`/api/memory/${id}`);
        return { contents: [{ uri: uri.href, mimeType: "text/plain", text: `[${b.type}] ${b.name}\n\n${b.content}` }] };
      }
      const r = await kernel<{ blocks: Array<{ id: string; name: string; type: string }> }>("/api/memory");
      return {
        contents: (r.blocks ?? []).map((b) => ({
          uri: `clove://memory/${b.id}`,
          mimeType: "text/plain" as const,
          text: `[${b.type}] ${b.name}`,
        })),
      };
    }
  );

  server.resource(
    "clove-trace",
    "clove://trace",
    { description: "Execution traces from the CLOVE kernel" },
    async (uri) => {
      const chain_id = uri.href.replace("clove://trace/", "");
      const history = await kernel<{ runs: Array<Record<string, unknown>> }>("/api/history");
      const run = history.runs?.find((r) => r.chain_id === chain_id);
      return {
        contents: [{
          uri: uri.href,
          mimeType: "application/json",
          text: JSON.stringify(run ?? { error: "trace not found" }, null, 2),
        }],
      };
    }
  );

  return server;
}

// ── Bootstrap ────────────────────────────────────────────────────────────────

async function main() {
  if (PORT) {
    // HTTP mode — stateless per-request transport
    const httpServer = http.createServer(async (req, res) => {
      const url = new URL(req.url ?? "/", `http://localhost:${PORT}`);

      // Health check — no auth required (Railway health checker, uptime monitors)
      if (url.pathname === "/health") {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: true, kernel: KERNEL_URL, version: "2.0.0" }));
        return;
      }

      // Auth check for all other routes
      if (MCP_KEY) {
        const auth = req.headers["authorization"] ?? req.headers["x-api-key"] ?? "";
        const token = typeof auth === "string" ? auth.replace(/^Bearer\s+/i, "") : "";
        if (token !== MCP_KEY) {
          res.writeHead(401, { "Content-Type": "application/json" });
          res.end(JSON.stringify({ error: "Unauthorized" }));
          return;
        }
      }

      if (url.pathname === "/mcp") {
        const server = buildServer();
        const transport = new StreamableHTTPServerTransport({
          sessionIdGenerator: undefined, // stateless
        });
        res.on("close", () => transport.close());
        await server.connect(transport);
        await transport.handleRequest(req, res);
        return;
      }

      res.writeHead(404);
      res.end("Not found");
    });

    httpServer.listen(PORT, () => {
      console.error(`CLOVE MCP Server (HTTP) listening on :${PORT}`);
      console.error(`Kernel: ${KERNEL_URL}`);
      console.error(`Auth:   ${MCP_KEY ? "enabled" : "disabled (set CLOVE_MCP_KEY)"}`);
    });
  } else {
    // Stdio mode — local Claude Code / MCP host
    const server = buildServer();
    const transport = new StdioServerTransport();
    await server.connect(transport);
    console.error("CLOVE MCP Server running on stdio");
    console.error(`Kernel: ${KERNEL_URL}`);
  }
}

main().catch((e) => {
  console.error("Fatal:", e);
  process.exit(1);
});
