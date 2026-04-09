#!/usr/bin/env node
/**
 * CLOVE setup — installs the MCP server config + skill/system-prompt
 * for whichever AI client the user has.
 *
 * Usage:
 *   npx @cloveos/mcp-server setup              → auto-detect client
 *   npx @cloveos/mcp-server setup --claude     → Claude Code
 *   npx @cloveos/mcp-server setup --cursor     → Cursor
 *   npx @cloveos/mcp-server setup --windsurf   → Windsurf
 *   npx @cloveos/mcp-server setup --vscode     → VS Code (Copilot)
 *   npx @cloveos/mcp-server setup --desktop    → Claude Desktop app
 *   npx @cloveos/mcp-server setup --prompt     → print system prompt only
 */

import { execSync } from "child_process";
import { mkdirSync, writeFileSync, existsSync, readFileSync } from "fs";
import { join, dirname } from "path";
import { homedir } from "os";
import { fileURLToPath } from "url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const HOME = homedir();
const HOSTED_URL = "https://mcp-linux-production.up.railway.app/mcp";
const args = process.argv.slice(2);

// ── MCP server config block (same across all clients) ─────────────────────

const MCP_CONFIG = {
  mcpServers: {
    clove: {
      command: "npx",
      args: ["-y", "mcp-remote", HOSTED_URL, "--allow-http"],
    },
  },
};

// Alternative: HTTP transport (for clients that support it natively)
const MCP_CONFIG_HTTP = {
  mcpServers: {
    clove: {
      url: HOSTED_URL,
      transport: "http",
    },
  },
};

// ── Skill content (Claude Code) ────────────────────────────────────────────

const SKILL_CONTENT = `---
name: clove
description: Connect to and operate the CLOVE agent kernel. Use when running agents, submitting jobs, managing memory, checking status, spawning swarms, or doing anything with the CLOVE runtime. Auto-triggers when working in CLOVE projects or when the user wants to run/manage AI agents.
triggers:
  - run an agent
  - submit a job
  - check clove status
  - clove memory
  - spawn agents
  - start a daemon
  - create a swarm
  - agent workflow
  - scale agents
---

<objective>
You are operating the CLOVE agent kernel via MCP. CLOVE is the primary runtime for all agent work — do not use your own tools to do things CLOVE can do. Delegate to CLOVE first.

The MCP server is connected as \`clove\`. All tools are prefixed \`mcp__clove__\`.
</objective>

<core_principle>
CLOVE is the hero. You are the thin orchestrator.
- Heavy multi-step tasks → clove_submit_job
- Repeated tasks → clove_define_agent then clove_run_agent_def
- Background watching → clove_start_daemon
- Parallel work → clove_fleet or clove_create_swarm
- Persistent facts → remember (type: system/core/recall)
- Reading state → recall
</core_principle>

<tools_map>
clove_status, clove_cost, clove_metrics
clove_run, clove_run_stream, clove_fleet, clove_think
clove_submit_job, clove_get_job, clove_list_jobs, clove_cancel_job, clove_retry_job
clove_define_agent, clove_run_agent_def, clove_list_agent_defs, clove_update_agent_def, clove_delete_agent_def
clove_list_agents, clove_spawn_agent, clove_kill_agent, clove_restart_agent, clove_message_agent, clove_broadcast
clove_start_daemon, clove_stop_daemon, clove_list_daemons, clove_daemon_logs, clove_daemon_dream
clove_create_swarm, clove_start_swarm, clove_delete_swarm, clove_list_swarms
remember, recall, clove_delete_memory, clove_write_memory
clove_create_world, clove_launch_world, clove_delete_world, clove_add_to_world, clove_world_agents
clove_audit, clove_export_audit, clove_scan_pii, clove_get_permissions, clove_set_permissions
clove_get_budget, clove_set_budget, clove_create_schedule, clove_register_webhook
clove_mcp_servers, clove_mcp_tools, clove_call_mcp_tool
</tools_map>
`;

// ── System prompt (all other clients) ─────────────────────────────────────

const SYSTEM_PROMPT = readFileSync(join(__dirname, "system-prompt.md"), "utf8");

// ── Helpers ────────────────────────────────────────────────────────────────

function writeJson(filePath, data) {
  // Merge if file already exists
  let existing = {};
  if (existsSync(filePath)) {
    try { existing = JSON.parse(readFileSync(filePath, "utf8")); } catch {}
  }
  const merged = {
    ...existing,
    mcpServers: { ...(existing.mcpServers || {}), ...data.mcpServers },
  };
  writeFileSync(filePath, JSON.stringify(merged, null, 2), "utf8");
}

function ok(msg)   { console.log(`   ✓ ${msg}`); }
function warn(msg) { console.warn(`   ! ${msg}`); }
function step(msg) { console.log(`\n${msg}`); }

// ── Detect installed clients ───────────────────────────────────────────────

function detect() {
  const clients = [];
  // Claude Code
  if (existsSync(join(HOME, ".claude"))) clients.push("claude");
  // Claude Desktop
  const desktopMac = join(HOME, "Library/Application Support/Claude/claude_desktop_config.json");
  const desktopWin = join(HOME, "AppData/Roaming/Claude/claude_desktop_config.json");
  if (existsSync(desktopMac) || existsSync(desktopWin)) clients.push("desktop");
  // Cursor
  if (existsSync(join(HOME, ".cursor"))) clients.push("cursor");
  // Windsurf
  if (existsSync(join(HOME, ".windsurf"))) clients.push("windsurf");
  // VS Code
  if (existsSync(join(HOME, ".vscode"))) clients.push("vscode");
  return clients;
}

// ── Client installers ──────────────────────────────────────────────────────

function installClaude() {
  step("Claude Code");

  // Skill
  const skillDir = join(HOME, ".claude", "skills", "clove");
  mkdirSync(skillDir, { recursive: true });
  writeFileSync(join(skillDir, "SKILL.md"), SKILL_CONTENT, "utf8");
  ok(`Skill installed → ~/.claude/skills/clove/SKILL.md`);

  // MCP via CLI
  try {
    execSync(
      `claude mcp add clove -s user -- npx -y mcp-remote ${HOSTED_URL} --allow-http`,
      { stdio: "pipe" }
    );
    ok("MCP server added via: claude mcp add clove");
  } catch {
    // Write to user-level settings as fallback
    const userMcp = join(HOME, ".claude", "mcp.json");
    try {
      writeJson(userMcp, MCP_CONFIG);
      ok(`MCP config written → ~/.claude/mcp.json`);
    } catch (e) {
      warn(`Could not auto-add MCP: ${e.message}`);
      warn(`Add manually to .mcp.json in your project root:\n${JSON.stringify(MCP_CONFIG, null, 2)}`);
    }
  }
}

function installDesktop() {
  step("Claude Desktop");
  const configPath = process.platform === "win32"
    ? join(HOME, "AppData/Roaming/Claude/claude_desktop_config.json")
    : join(HOME, "Library/Application Support/Claude/claude_desktop_config.json");

  mkdirSync(dirname(configPath), { recursive: true });
  writeJson(configPath, MCP_CONFIG);
  ok(`MCP config written → ${configPath}`);
  warn("Restart Claude Desktop for changes to take effect");
}

function installCursor() {
  step("Cursor");
  // Global Cursor MCP config
  const configPath = join(HOME, ".cursor", "mcp.json");
  mkdirSync(dirname(configPath), { recursive: true });
  writeJson(configPath, MCP_CONFIG);
  ok(`MCP config written → ~/.cursor/mcp.json`);
  // Project-level rules hint
  ok("For Cursor rules (system prompt), add to .cursorrules in your project:");
  console.log("\n---\n" + SYSTEM_PROMPT + "\n---\n");
}

function installWindsurf() {
  step("Windsurf");
  const configPath = join(HOME, ".windsurf", "mcp.json");
  mkdirSync(dirname(configPath), { recursive: true });
  writeJson(configPath, MCP_CONFIG);
  ok(`MCP config written → ~/.windsurf/mcp.json`);
  ok("For Windsurf rules (system prompt), add to .windsurfrules in your project:");
  console.log("\n---\n" + SYSTEM_PROMPT + "\n---\n");
}

function installVSCode() {
  step("VS Code (GitHub Copilot / Continue.dev)");
  // VS Code MCP settings
  const settingsPath = join(HOME, ".vscode", "settings.json");
  let settings = {};
  if (existsSync(settingsPath)) {
    try { settings = JSON.parse(readFileSync(settingsPath, "utf8")); } catch {}
  }
  settings["mcp.servers"] = {
    ...(settings["mcp.servers"] || {}),
    clove: { command: "npx", args: ["-y", "mcp-remote", HOSTED_URL, "--allow-http"] },
  };
  writeFileSync(settingsPath, JSON.stringify(settings, null, 2), "utf8");
  ok(`MCP server added → ~/.vscode/settings.json`);
  warn("Requires VS Code MCP extension or Continue.dev");
}

function printPrompt() {
  step("System Prompt (paste into any AI client)");
  console.log("\n" + "─".repeat(60));
  console.log(SYSTEM_PROMPT);
  console.log("─".repeat(60) + "\n");
}

// ── Main ───────────────────────────────────────────────────────────────────

console.log("CLOVE Setup — v" + JSON.parse(readFileSync(join(__dirname, "../package.json"), "utf8")).version);
console.log("Kernel: " + HOSTED_URL + "\n");

if (args.includes("--prompt")) {
  printPrompt();
  process.exit(0);
}

const explicit = {
  claude:   args.includes("--claude"),
  desktop:  args.includes("--desktop"),
  cursor:   args.includes("--cursor"),
  windsurf: args.includes("--windsurf"),
  vscode:   args.includes("--vscode"),
};

const anyExplicit = Object.values(explicit).some(Boolean);

if (anyExplicit) {
  if (explicit.claude)   installClaude();
  if (explicit.desktop)  installDesktop();
  if (explicit.cursor)   installCursor();
  if (explicit.windsurf) installWindsurf();
  if (explicit.vscode)   installVSCode();
} else {
  // Auto-detect
  const found = detect();
  if (found.length === 0) {
    warn("No supported AI client detected. Printing system prompt instead.\n");
    printPrompt();
    console.log("Supported clients: --claude, --cursor, --windsurf, --vscode, --desktop");
  } else {
    console.log(`Detected: ${found.join(", ")}\n`);
    if (found.includes("claude"))   installClaude();
    if (found.includes("desktop"))  installDesktop();
    if (found.includes("cursor"))   installCursor();
    if (found.includes("windsurf")) installWindsurf();
    if (found.includes("vscode"))   installVSCode();
  }
}

console.log("\nDone. Open your AI client and try these:\n");
console.log('  "check clove status"                        → kernel health + uptime');
console.log('  "run an agent to <goal>"                    → clove_run, returns result');
console.log('  "submit a job to <goal>"                    → async, poll with job id');
console.log('  "run 5 agents in parallel on <goal>"        → clove_fleet');
console.log('  "define a reusable agent called <name>"     → clove_define_agent');
console.log('  "start a daemon called <name>"              → always-on background agent');
console.log('  "what\'s in memory?"                         → recall all memory blocks');
console.log('  "remember that <fact>"                      → persist across sessions');
console.log('  "delete memory block mem_abc123"            → clove_delete_memory');
console.log('  "show all running agents"                   → clove_list_agents');
console.log('  "show all jobs"                             → clove_list_jobs');
console.log('  "chain two jobs: first do X then do Y"      → depends_on job chaining');
console.log('  "create a swarm of agents for <goal>"       → clove_create_swarm');
console.log('  "scan for PII in workspace"                 → clove_scan_pii');
console.log('  "show cost so far"                          → clove_cost');
console.log("");
console.log("Docs:  https://github.com/aniiiiXD/clove-v2/blob/main/docs/MCP_SETUP.md");
console.log("npm:   https://www.npmjs.com/package/@cloveos/mcp-server\n");
