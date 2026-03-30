#!/usr/bin/env node
/**
 * CLOVE MCP Server
 *
 * Exposes CLOVE's orchestration primitives (fleets, worlds, agents, cost)
 * as MCP tools that Claude Code, Cursor, or any MCP host can call.
 *
 * Usage in Claude Code's MCP config:
 *   {
 *     "mcpServers": {
 *       "clove": {
 *         "command": "npx",
 *         "args": ["@cloveos/mcp-server"]
 *       }
 *     }
 *   }
 */

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";

const KERNEL_URL = process.env.CLOVE_API_URL || "http://localhost:8080";

// ── Kernel API helper ────────────────────────────────────────────────

async function kernel<T>(path: string, method = "GET", body?: unknown): Promise<T> {
  const opts: RequestInit = {
    method,
    headers: { "Content-Type": "application/json" },
  };
  if (body) opts.body = JSON.stringify(body);
  const res = await fetch(`${KERNEL_URL}${path}`, opts);
  return (await res.json()) as T;
}

// ── Server setup ─────────────────────────────────────────────────────

const server = new Server(
  { name: "clove", version: "0.1.0" },
  { capabilities: { tools: {} } }
);

// ── Tool definitions ─────────────────────────────────────────────────

server.setRequestHandler(ListToolsRequestSchema, async () => ({
  tools: [
    {
      name: "clove_run_agent",
      description:
        "Run a single AI agent on a goal. The CLOVE kernel assigns the agent, calls tools (file I/O, shell, HTTP, search, MCP), enforces budget and permissions, and returns the result. Use this for tasks that need tool access, code execution, or web research.",
      inputSchema: {
        type: "object" as const,
        properties: {
          goal: { type: "string", description: "What the agent should accomplish" },
          budget: { type: "number", description: "Max cost in USD (default 0.50)" },
          tools: {
            type: "array",
            items: { type: "string" },
            description: "Allowed tools: read_file, write_file, exec, http, search, remember, recall, mcp_call, delegate",
          },
          model: { type: "string", description: "LLM model override (default: kernel default)" },
        },
        required: ["goal"],
      },
    },
    {
      name: "clove_launch_fleet",
      description:
        "Run multiple AI agents in parallel on the same goal. Each agent works independently, then results are synthesized. Use for research, analysis, or any task that benefits from multiple perspectives.",
      inputSchema: {
        type: "object" as const,
        properties: {
          goal: { type: "string", description: "The shared goal for all agents" },
          agents: { type: "number", description: "Number of parallel agents (default 3)" },
          budget: { type: "number", description: "Total budget in USD across all agents (default 1.00)" },
        },
        required: ["goal"],
      },
    },
    {
      name: "clove_launch_world",
      description:
        "Launch a multi-agent world from a template. A world is an isolated environment where multiple specialized agents coordinate on a complex task. Available templates: code-health (security + deps + quality review).",
      inputSchema: {
        type: "object" as const,
        properties: {
          template: { type: "string", description: "World template name (e.g., 'code-health')" },
          params: {
            type: "object",
            description: "Template parameters (e.g., {project_path: './my-app'})",
            additionalProperties: { type: "string" },
          },
        },
        required: ["template"],
      },
    },
    {
      name: "clove_list_agents",
      description: "List all currently running agents in the CLOVE kernel with their state, PID, and cost.",
      inputSchema: { type: "object" as const, properties: {} },
    },
    {
      name: "clove_list_worlds",
      description: "List all active worlds (isolated agent environments) with member counts.",
      inputSchema: { type: "object" as const, properties: {} },
    },
    {
      name: "clove_create_world",
      description: "Create a new isolated world for multi-agent coordination.",
      inputSchema: {
        type: "object" as const,
        properties: {
          name: { type: "string", description: "World name" },
        },
        required: ["name"],
      },
    },
    {
      name: "clove_get_cost",
      description: "Get current cost tracking — total spend, budget, request count.",
      inputSchema: { type: "object" as const, properties: {} },
    },
    {
      name: "clove_get_status",
      description: "Get CLOVE kernel health — version, uptime, syscall count, active agents.",
      inputSchema: { type: "object" as const, properties: {} },
    },
    {
      name: "clove_query_audit",
      description: "Query the audit log — every agent action is logged with timestamp, category, success/fail.",
      inputSchema: {
        type: "object" as const,
        properties: {
          limit: { type: "number", description: "Number of entries to return (default 20)" },
        },
      },
    },
    {
      name: "clove_list_memory",
      description: "List agent memory blocks — persistent knowledge stored by agents (system prompts, core facts, recall).",
      inputSchema: { type: "object" as const, properties: {} },
    },
    {
      name: "clove_mcp_tools",
      description: "List all MCP tools available through the CLOVE kernel's MCP bridge (external tool servers like GitHub, Slack, databases).",
      inputSchema: { type: "object" as const, properties: {} },
    },
  ],
}));

// ── Tool execution ───────────────────────────────────────────────────

server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  try {
    switch (name) {
      case "clove_run_agent": {
        const result = await kernel<{
          success: boolean;
          content: string;
          total_cost_usd: number;
          total_tokens: number;
          steps: number;
          step_log: Array<{ step: number; tool: string; args: unknown }>;
        }>("/api/run", "POST", {
          goal: (args as Record<string, unknown>).goal,
          budget: (args as Record<string, unknown>).budget ?? 0.5,
          tools: (args as Record<string, unknown>).tools,
          model: (args as Record<string, unknown>).model,
        });
        const stepsSummary = result.step_log
          ?.map((s) => `  ${s.step}. ${s.tool}(${JSON.stringify(s.args).slice(0, 60)})`)
          .join("\n");
        return {
          content: [
            {
              type: "text" as const,
              text: `${result.success ? "SUCCESS" : "FAILED"} | ${result.steps} steps | $${result.total_cost_usd.toFixed(4)} | ${result.total_tokens} tokens\n\n${result.content}${stepsSummary ? `\n\nTool calls:\n${stepsSummary}` : ""}`,
            },
          ],
        };
      }

      case "clove_launch_fleet": {
        const result = await kernel<{
          success: boolean;
          content: string;
          total_cost_usd: number;
          agent_count: number;
        }>("/api/fleet", "POST", {
          goal: (args as Record<string, unknown>).goal,
          agents: (args as Record<string, unknown>).agents ?? 3,
          budget: (args as Record<string, unknown>).budget ?? 1.0,
        });
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      }

      case "clove_launch_world": {
        const tmpl = (args as Record<string, unknown>).template as string;
        const params = ((args as Record<string, unknown>).params ?? {}) as Record<string, string>;
        // Delegate to world runner script
        const { execSync } = await import("node:child_process");
        const paramArgs = Object.entries(params)
          .map(([, v]) => v)
          .join(" ");
        try {
          const output = execSync(
            `python3 ${process.env.CLOVE_ROOT || "."}/examples/worlds/${tmpl}.py ${paramArgs}`,
            { timeout: 120000, encoding: "utf-8" }
          );
          return { content: [{ type: "text" as const, text: output }] };
        } catch (e) {
          return {
            content: [
              {
                type: "text" as const,
                text: `World template '${tmpl}' failed: ${(e as Error).message}`,
              },
            ],
            isError: true,
          };
        }
      }

      case "clove_list_agents": {
        const result = await kernel<Array<{ id: number; name: string; state: string }>>("/api/agents");
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }],
        };
      }

      case "clove_list_worlds": {
        const result = await kernel<{ worlds: Array<{ id: number; name: string; member_count: number }> }>("/api/worlds");
        return {
          content: [{ type: "text" as const, text: JSON.stringify(result.worlds, null, 2) }],
        };
      }

      case "clove_create_world": {
        const result = await kernel("/api/worlds", "POST", { name: (args as Record<string, unknown>).name });
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      }

      case "clove_get_cost": {
        const result = await kernel<{ total_cost_usd: number; max_cost_usd: number; total_requests: number }>("/api/cost");
        return {
          content: [
            {
              type: "text" as const,
              text: `Total: $${result.total_cost_usd.toFixed(4)} | Budget: $${result.max_cost_usd.toFixed(2)} | Requests: ${result.total_requests}`,
            },
          ],
        };
      }

      case "clove_get_status": {
        const health = await kernel<{ status: string; version: string; uptime_s: number; syscall_count: number }>("/api/health");
        const cost = await kernel<{ total_cost_usd: number }>("/api/cost");
        return {
          content: [
            {
              type: "text" as const,
              text: `CLOVE v${health.version} | ${health.status} | ${health.syscall_count} syscalls | uptime ${health.uptime_s}s | cost $${cost.total_cost_usd.toFixed(4)}`,
            },
          ],
        };
      }

      case "clove_query_audit": {
        const limit = ((args as Record<string, unknown>).limit as number) ?? 20;
        const entries = await kernel<Array<{ event_type: string; category: string; agent_name: string; success: boolean; timestamp: string }>>(`/api/audit?limit=${limit}`);
        const lines = entries
          .map((e) => `${new Date(e.timestamp).toLocaleTimeString()} ${e.success ? "OK" : "FAIL"} ${e.event_type.padEnd(20)} ${e.agent_name}`)
          .join("\n");
        return { content: [{ type: "text" as const, text: lines || "No audit entries" }] };
      }

      case "clove_list_memory": {
        const result = await kernel<{ blocks: Array<{ name: string; type: string; access: string; content: string }>; count: number }>("/api/memory");
        const lines = (result.blocks || [])
          .map((b) => `[${b.type}/${b.access}] ${b.name}: ${b.content.slice(0, 100)}`)
          .join("\n");
        return { content: [{ type: "text" as const, text: lines || "No memory blocks" }] };
      }

      case "clove_mcp_tools": {
        const result = await kernel<{ tools: Array<{ server_name: string; name: string; description: string }> }>("/api/mcp/tools");
        const lines = (result.tools || [])
          .map((t) => `[${t.server_name}] ${t.name} — ${t.description}`)
          .join("\n");
        return { content: [{ type: "text" as const, text: lines || "No MCP tools configured" }] };
      }

      default:
        return {
          content: [{ type: "text" as const, text: `Unknown tool: ${name}` }],
          isError: true,
        };
    }
  } catch (error) {
    return {
      content: [
        {
          type: "text" as const,
          text: `CLOVE kernel error: ${(error as Error).message}. Is the kernel running? (clove start)`,
        },
      ],
      isError: true,
    };
  }
});

// ── Start ────────────────────────────────────────────────────────────

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("CLOVE MCP Server running on stdio");
}

main().catch(console.error);
